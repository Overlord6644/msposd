/*
 * det_sidecar.c - subscribe to waybeam's RTP sidecar and keep the latest
 * detection snapshot for the overlay to draw.
 *
 * Why this exists: the AI detections are produced by the streamer (waybeam
 * venc) running a detector plugin, but this platform cannot composite two
 * overlapping full-screen RGN regions - whichever region is on top replaces the
 * one below, transparent pixels included. So msposd owns the single region and
 * draws both the OSD and the boxes, which means it has to *receive* the boxes.
 *
 * The sidecar is the documented channel for that (see waybeam's
 * documentation/RTP_SIDECAR_PROTOCOL.md): UDP, one datagram per encoded frame,
 * detections riding in a trailer so they stay frame-correlated and cost no
 * extra packets. It replaces an earlier home-grown /tmp file.
 *
 * Wire notes that matter here:
 *   - flags live at byte 7 (byte 5 is msg_type - a documented probe-writing trap)
 *   - trailers appear in flag-bit order right after the 52-byte base, each
 *     sliding up when a lower-bit trailer is absent, so offsets must be walked
 *   - box coords are u16 normalized 0..65535 of frame W/H, NOT model pixels,
 *     so no model geometry is needed to scale them
 *   - unknown flag bits and unknown TLV tags must be ignored, which is what
 *     makes new trailers non-breaking
 */
#include "det_sidecar.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define SC_MAGIC        0x52545053u /* "RTPS" */
#define SC_VERSION      1
#define SC_MSG_SUBSCRIBE 1
#define SC_MSG_FRAME     2

#define SC_BASE_LEN     52
#define SC_FLAG_ENC_INFO       0x02
#define SC_FLAG_TRANSPORT_INFO 0x04
#define SC_FLAG_ATTITUDE       0x08
#define SC_FLAG_DETECT         0x10
#define SC_ENC_INFO_LEN        12
#define SC_TRANSPORT_INFO_LEN  16
#define SC_ATTITUDE_LEN        12
#define SC_DETECT_HDR_LEN      16
#define SC_TAG_BOX             0x01
#define SC_TAG_BOX_LEN         10

/* Resend the subscription well inside the 5 s server-side TTL. */
#define SC_RESUB_MS 2000

static int g_fd = -1;
static struct sockaddr_in g_peer;
static uint64_t g_last_sub_ms;
static DetSnapshot g_snap;

static uint64_t now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000ull + (uint64_t)tv.tv_usec / 1000ull;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void send_subscribe(void)
{
	uint8_t msg[8] = {0};
	msg[0] = (uint8_t)(SC_MAGIC >> 24);
	msg[1] = (uint8_t)(SC_MAGIC >> 16);
	msg[2] = (uint8_t)(SC_MAGIC >> 8);
	msg[3] = (uint8_t)SC_MAGIC;
	msg[4] = SC_VERSION;
	msg[5] = SC_MSG_SUBSCRIBE;
	sendto(g_fd, msg, sizeof(msg), 0, (struct sockaddr *)&g_peer, sizeof(g_peer));
	g_last_sub_ms = now_ms();
}

int det_sidecar_open(const char *host, int port)
{
	if (g_fd >= 0)
		return 0;
	if (port <= 0)
		return -1;

	g_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_fd < 0)
		return -1;
	/* Non-blocking: this is polled from the render loop, which must never
	 * stall waiting for a datagram. */
	fcntl(g_fd, F_SETFL, fcntl(g_fd, F_GETFL, 0) | O_NONBLOCK);

	memset(&g_peer, 0, sizeof(g_peer));
	g_peer.sin_family = AF_INET;
	g_peer.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host && *host ? host : "127.0.0.1",
		    &g_peer.sin_addr) != 1) {
		close(g_fd);
		g_fd = -1;
		return -1;
	}
	memset(&g_snap, 0, sizeof(g_snap));
	send_subscribe();
	printf("[det-sidecar] subscribed to %s:%d\n",
		host && *host ? host : "127.0.0.1", port);
	return 0;
}

void det_sidecar_close(void)
{
	if (g_fd >= 0)
		close(g_fd);
	g_fd = -1;
	memset(&g_snap, 0, sizeof(g_snap));
}

/* Parse one FRAME datagram; returns 1 if it carried detections. */
static int parse_frame(const uint8_t *d, size_t len)
{
	if (len < SC_BASE_LEN || rd32(d) != SC_MAGIC || d[4] != SC_VERSION ||
	    d[5] != SC_MSG_FRAME)
		return 0;

	uint8_t flags = d[7]; /* byte 7, not byte 5 */
	size_t off = SC_BASE_LEN;

	/* Walk the fixed trailers in flag-bit order, guarding every step. */
	if (flags & SC_FLAG_ENC_INFO) {
		if (len < off + SC_ENC_INFO_LEN)
			return 0;
		off += SC_ENC_INFO_LEN;
	}
	if (flags & SC_FLAG_TRANSPORT_INFO) {
		if (len < off + SC_TRANSPORT_INFO_LEN)
			return 0;
		off += SC_TRANSPORT_INFO_LEN;
	}
	if (flags & SC_FLAG_ATTITUDE) {
		if (len < off + SC_ATTITUDE_LEN)
			return 0;
		off += SC_ATTITUDE_LEN;
	}
	if (!(flags & SC_FLAG_DETECT))
		return 0;
	if (len < off + SC_DETECT_HDR_LEN)
		return 0;

	const uint8_t *h = d + off;
	uint16_t model_id = rd16(h + 0);
	uint16_t count = rd16(h + 4);
	uint32_t seq = rd32(h + 8);
	uint16_t payload_len = rd16(h + 12);
	off += SC_DETECT_HDR_LEN;
	if (len < off + payload_len)
		return 0;

	/* Older snapshots can arrive out of order after a stall; keep the newest. */
	if (g_snap.valid && seq < g_snap.seq)
		return 0;

	const uint8_t *p = d + off;
	const uint8_t *end = p + payload_len;
	int n = 0;
	while (p + 2 <= end && n < DET_SIDECAR_MAX) {
		uint8_t tag = p[0], tlen = p[1];
		if (p + 2 + tlen > end)
			break;
		if (tag == SC_TAG_BOX && tlen == SC_TAG_BOX_LEN) {
			const uint8_t *v = p + 2;
			DetBox *b = &g_snap.boxes[n++];
			/* u16 normalized -> 0..1; the tap scales the frame
			 * linearly per axis, so this is valid in display space
			 * too and needs no model geometry. */
			b->x1 = (float)rd16(v + 0) / 65535.0f;
			b->y1 = (float)rd16(v + 2) / 65535.0f;
			b->x2 = (float)rd16(v + 4) / 65535.0f;
			b->y2 = (float)rd16(v + 6) / 65535.0f;
			b->score_pct = (int)((v[8] * 100 + 127) / 255);
			b->cls = v[9];
		}
		p += 2 + tlen; /* skip unknown tags by length */
	}

	g_snap.count = n;
	g_snap.seq = seq;
	g_snap.model_id = model_id;
	g_snap.updated_ms = now_ms();
	g_snap.valid = 1;
	(void)count;
	return 1;
}

int det_sidecar_poll(void)
{
	if (g_fd < 0)
		return -1;

	if (now_ms() - g_last_sub_ms >= SC_RESUB_MS)
		send_subscribe();

	/* Drain everything queued and keep only the newest snapshot: at 90 fps
	 * many datagrams pile up between OSD refreshes, and drawing a stale one
	 * would lag the boxes behind the video. */
	uint8_t buf[1024];
	int got = 0;
	for (;;) {
		ssize_t r = recv(g_fd, buf, sizeof(buf), 0);
		if (r <= 0) {
			if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
				return -1;
			break;
		}
		if (parse_frame(buf, (size_t)r))
			got = 1;
	}
	return got;
}

const DetSnapshot *det_sidecar_snapshot(unsigned max_age_ms)
{
	if (!g_snap.valid)
		return NULL;
	if (max_age_ms && now_ms() - g_snap.updated_ms > max_age_ms)
		return NULL;
	return &g_snap;
}
