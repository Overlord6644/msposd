/*
 * det_sidecar.h - consume AI detections from waybeam venc's RTP sidecar.
 *
 * The streamer runs the detector and publishes one datagram per encoded frame;
 * msposd subscribes and draws the boxes into its own (single) RGN region. See
 * det_sidecar.c for the wire details and why the drawing lives here.
 */
#ifndef DET_SIDECAR_H
#define DET_SIDECAR_H

#include <stdint.h>

#define DET_SIDECAR_MAX 24 /* RTP_SIDECAR_DETECT_MAX */

/* One detection, coordinates normalized 0..1 of the frame (valid in display
 * space as well - the detection tap scales linearly per axis). */
typedef struct {
	float x1, y1, x2, y2;
	int   score_pct; /* 0..100 */
	int   cls;       /* label index, interpret via model_id */
} DetBox;

typedef struct {
	DetBox   boxes[DET_SIDECAR_MAX];
	int      count;
	uint32_t seq;        /* monotonic inference id from the sender */
	uint16_t model_id;   /* 0 = VisDrone-10, 1 = person-only */
	uint64_t updated_ms; /* local receive time */
	int      valid;
} DetSnapshot;

/* Open the socket and subscribe. host NULL/empty means 127.0.0.1.
 * Returns 0 on success, <0 on failure. Idempotent. */
int det_sidecar_open(const char *host, int port);

void det_sidecar_close(void);

/* Drain pending datagrams, refresh the subscription when due, and keep the
 * newest snapshot. Call from the render loop. Returns 1 if detections were
 * updated, 0 if nothing new, <0 on socket error. */
int det_sidecar_poll(void);

/* Latest snapshot, or NULL when there is none or it is older than
 * max_age_ms (0 = no age limit). */
const DetSnapshot *det_sidecar_snapshot(unsigned max_age_ms);

#endif /* DET_SIDECAR_H */
