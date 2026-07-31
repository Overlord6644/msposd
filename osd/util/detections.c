/* detections.c - see detections.h. Reads the worker's detection file and
 * draws rectangle outlines into the I4 overlay canvas using the msposd
 * palette (index 2 = green, index 8 = black for the drop shadow). */
#include "detections.h"
#include "det_sidecar.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#define I4_TRANSPARENT 0x0F /* msposd clears the I4 canvas to 0xFF */

/* Write a pixel ONLY where the canvas is still transparent, so the boxes never
 * overwrite OSD glyphs already drawn - i.e. they render *behind* the OSD.
 * Nibble order matches setPixelI4 in bmp/bitmap.c (SigmaStar: odd x = high). */
static inline void px_behind(
	uint8_t *bmp, uint32_t w, uint32_t h, uint32_t rs, int x, int y, uint8_t color) {
	if (x < 0 || y < 0 || x >= (int)w || y >= (int)h)
		return;
	uint8_t *p = bmp + (size_t)y * rs + (size_t)(x >> 1);
	if (x & 1) {
		if ((*p >> 4) != I4_TRANSPARENT)
			return; /* OSD pixel here - stay behind it */
		*p = (uint8_t)((*p & 0x0F) | (color << 4));
	} else {
		if ((*p & 0x0F) != I4_TRANSPARENT)
			return;
		*p = (uint8_t)((*p & 0xF0) | (color & 0x0F));
	}
}

/* msposd palette (bmp/bitmap.c): 1=red, 2=green, 8=black, 9=semi-transparent
 * black, 15=transparent. Box red + green text = the original worker's look. */
#define DET_BOX 1	  /* palette red - box outline */
#define DET_TEXT 2	  /* palette green - label text */
#define DET_SHADOW 9  /* semi-transparent black - halo and label backdrop */
#define DET_THICK 1		  /* hairline box - deliberately unobtrusive */
#define DET_TEXT_SCALE 2  /* small label (14 px tall) so it stays discreet */

/* 5x7 font, lifted from the worker (glyph set " .%0123456789A-Z"). Seven bytes
 * per glyph, one per row, 5 significant bits, LSB = leftmost. */
#define GW 5
#define GH 7
static const unsigned char font5x7[39 * GH] = {
	/* ' ' */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	/* '.' */ 0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,
	/* '%' */ 0x19,0x19,0x02,0x04,0x08,0x13,0x13,
	/* '0' */ 0x0e,0x11,0x13,0x15,0x19,0x11,0x0e,
	/* '1' */ 0x04,0x0c,0x04,0x04,0x04,0x04,0x0e,
	/* '2' */ 0x0e,0x11,0x01,0x02,0x04,0x08,0x1f,
	/* '3' */ 0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e,
	/* '4' */ 0x02,0x06,0x0a,0x12,0x1f,0x02,0x02,
	/* '5' */ 0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e,
	/* '6' */ 0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e,
	/* '7' */ 0x1f,0x01,0x02,0x04,0x08,0x08,0x08,
	/* '8' */ 0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e,
	/* '9' */ 0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e,
	/* 'A' */ 0x0e,0x11,0x11,0x1f,0x11,0x11,0x11,
	/* 'B' */ 0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e,
	/* 'C' */ 0x0e,0x11,0x10,0x10,0x10,0x11,0x0e,
	/* 'D' */ 0x1c,0x12,0x11,0x11,0x11,0x12,0x1c,
	/* 'E' */ 0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f,
	/* 'F' */ 0x1f,0x10,0x10,0x1e,0x10,0x10,0x10,
	/* 'G' */ 0x0e,0x11,0x10,0x10,0x13,0x11,0x0f,
	/* 'H' */ 0x11,0x11,0x11,0x1f,0x11,0x11,0x11,
	/* 'I' */ 0x0e,0x04,0x04,0x04,0x04,0x04,0x0e,
	/* 'J' */ 0x01,0x01,0x01,0x01,0x11,0x11,0x0e,
	/* 'K' */ 0x11,0x12,0x14,0x18,0x14,0x12,0x11,
	/* 'L' */ 0x10,0x10,0x10,0x10,0x10,0x10,0x1f,
	/* 'M' */ 0x11,0x1b,0x15,0x15,0x11,0x11,0x11,
	/* 'N' */ 0x11,0x11,0x19,0x15,0x13,0x11,0x11,
	/* 'O' */ 0x0e,0x11,0x11,0x11,0x11,0x11,0x0e,
	/* 'P' */ 0x1e,0x11,0x11,0x1e,0x10,0x10,0x10,
	/* 'Q' */ 0x0e,0x11,0x11,0x11,0x15,0x12,0x0d,
	/* 'R' */ 0x1e,0x11,0x11,0x1e,0x14,0x12,0x11,
	/* 'S' */ 0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e,
	/* 'T' */ 0x1f,0x04,0x04,0x04,0x04,0x04,0x04,
	/* 'U' */ 0x11,0x11,0x11,0x11,0x11,0x11,0x0e,
	/* 'V' */ 0x11,0x11,0x11,0x11,0x11,0x0a,0x04,
	/* 'W' */ 0x11,0x11,0x11,0x15,0x15,0x15,0x0a,
	/* 'X' */ 0x11,0x11,0x0a,0x04,0x0a,0x11,0x11,
	/* 'Y' */ 0x11,0x11,0x0a,0x04,0x04,0x04,0x04,
	/* 'Z' */ 0x1f,0x01,0x02,0x04,0x08,0x10,0x1f,
};

/* VisDrone class names, same order the model emits. */
static const char *const det_names[] = {
	"PEDESTRIAN", "PEOPLE", "BICYCLE", "CAR", "VAN",
	"TRUCK", "TRICYCLE", "AWNING", "BUS", "MOTOR",
};

static int glyph_index(char ch) {
	if (ch == ' ') return 0;
	if (ch == '.') return 1;
	if (ch == '%') return 2;
	if (ch >= '0' && ch <= '9') return 3 + (ch - '0');
	if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
	if (ch >= 'A' && ch <= 'Z') return 13 + (ch - 'A');
	return 0;
}

typedef struct {
	float x1, y1, x2, y2;
	int cls, pct;
} det_t;

static uint64_t now_ms(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000ull + tv.tv_usec / 1000ull;
}

/* Detections arrive either from waybeam venc's RTP sidecar (the documented
 * channel, preferred) or from a file written by a standalone worker (the older
 * arrangement, kept as a fallback so a camera running either stack works). */
static int load_from_sidecar(det_t *out, int max) {
	const DetSnapshot *s = det_sidecar_snapshot(DET_STALE_MS);
	if (!s)
		return 0;
	int n = s->count < max ? s->count : max;
	for (int i = 0; i < n; i++) {
		out[i].x1 = s->boxes[i].x1;
		out[i].y1 = s->boxes[i].y1;
		out[i].x2 = s->boxes[i].x2;
		out[i].y2 = s->boxes[i].y2;
		out[i].pct = s->boxes[i].score_pct;
		out[i].cls = s->boxes[i].cls;
	}
	return n;
}

static int load_dets(det_t *out, int max) {
	/* Diagnostic: `touch /tmp/yolo.test` draws one fixed centre box, which
	 * proves the msposd side of the pipeline independently of the worker. */
	struct stat ts;
	if (stat(DET_TEST_FILE, &ts) == 0 && max > 0) {
		out[0].x1 = 0.25f; out[0].y1 = 0.25f;
		out[0].x2 = 0.75f; out[0].y2 = 0.75f;
		out[0].cls = 0; out[0].pct = 99;
		return 1;
	}
	/* Poll first so a live stream always wins over a stale file. */
	det_sidecar_poll();
	int n_sc = load_from_sidecar(out, max);
	if (n_sc > 0)
		return n_sc;

	FILE *f = fopen(DET_FILE, "r");
	if (!f)
		return 0;
	unsigned long long stamp = 0;
	int count = 0, n = 0;
	if (fscanf(f, "%llu %d", &stamp, &count) != 2) {
		fclose(f);
		return 0;
	}
	/* Staleness from the in-file millisecond stamp (st_mtime is only
	 * second-resolution, which false-positives at high frame rates). */
	if (stamp && now_ms() > stamp + DET_STALE_MS) {
		fclose(f);
		return 0;
	}
	for (int i = 0; i < count && n < max; i++) {
		det_t d;
		if (fscanf(f, "%d %d %f %f %f %f", &d.cls, &d.pct, &d.x1, &d.y1, &d.x2, &d.y2) != 6)
			break;
		out[n++] = d;
	}
	fclose(f);
	return n;
}

static void hline(uint8_t *bmp, uint32_t w, uint32_t h, uint32_t rs, int x0, int x1, int y,
	uint8_t color) {
	if (y < 0 || y >= (int)h)
		return;
	if (x0 < 0)
		x0 = 0;
	if (x1 >= (int)w)
		x1 = (int)w - 1;
	for (int x = x0; x <= x1; x++)
		px_behind(bmp, w, h, rs, x, y, color);
}

static void vline(uint8_t *bmp, uint32_t w, uint32_t h, uint32_t rs, int x, int y0, int y1,
	uint8_t color) {
	if (x < 0 || x >= (int)w)
		return;
	if (y0 < 0)
		y0 = 0;
	if (y1 >= (int)h)
		y1 = (int)h - 1;
	for (int y = y0; y <= y1; y++)
		px_behind(bmp, w, h, rs, x, y, color);
}

static void rect(uint8_t *bmp, uint32_t w, uint32_t h, uint32_t rs, int x1, int y1, int x2,
	int y2, int thick, uint8_t color) {
	for (int t = 0; t < thick; t++) {
		hline(bmp, w, h, rs, x1, x2, y1 + t, color);
		hline(bmp, w, h, rs, x1, x2, y2 - t, color);
		vline(bmp, w, h, rs, x1 + t, y1, y2, color);
		vline(bmp, w, h, rs, x2 - t, y1, y2, color);
	}
}

static void fill_rect(uint8_t *bmp, uint32_t w, uint32_t h, uint32_t rs, int x1, int y1, int x2,
	int y2, uint8_t color) {
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= (int)w) x2 = (int)w - 1;
	if (y2 >= (int)h) y2 = (int)h - 1;
	for (int y = y1; y <= y2; y++)
		for (int x = x1; x <= x2; x++)
			px_behind(bmp, w, h, rs, x, y, color);
}

static int text_width(const char *s, int scale) {
	int n = (int)strlen(s);
	return n > 0 ? n * (GW + 1) * scale - scale : 0;
}

static void draw_text(uint8_t *bmp, uint32_t w, uint32_t h, uint32_t rs, int x, int y,
	const char *s, uint8_t color, int scale) {
	for (int ci = 0; s[ci]; ci++) {
		const unsigned char *g = &font5x7[glyph_index(s[ci]) * GH];
		int gx = x + ci * (GW + 1) * scale;
		for (int row = 0; row < GH; row++)
			for (int col = 0; col < GW; col++)
				/* MSB is the LEFTMOST pixel of the glyph row (verified on '1'
				 * and 'C'); reading it LSB-first renders the text mirrored. */
				if (g[row] & (1u << (GW - 1 - col)))
					fill_rect(bmp, w, h, rs, gx + col * scale, y + row * scale,
						gx + col * scale + scale - 1, y + row * scale + scale - 1, color);
	}
}

extern bool verbose;

/* One set per frame: detections_refresh() loads and measures, the draw call
 * paints the same set. Loading twice would double-parse the sidecar and could
 * even disagree with itself if a packet landed in between. */
static det_t g_dets[DET_MAX];
static int g_ndets;

int detections_refresh(uint32_t w, uint32_t h, int *bx0, int *by0, int *bx1,
	int *by1) {
	g_ndets = load_dets(g_dets, DET_MAX);
	int have = 0;
	*bx0 = *by0 = 0;
	*bx1 = *by1 = -1;
	for (int i = 0; i < g_ndets; i++) {
		int x1 = (int)(g_dets[i].x1 * (float)w);
		int y1 = (int)(g_dets[i].y1 * (float)h);
		int x2 = (int)(g_dets[i].x2 * (float)w);
		int y2 = (int)(g_dets[i].y2 * (float)h);
		if (x2 - x1 < 4 || y2 - y1 < 4)
			continue;
		/* The label extends the box: same maths as the draw pass below. */
		int lh = GH * DET_TEXT_SCALE;
		int ty = (y1 - lh - 2 >= 0) ? y1 - lh - 2 : y1 + 2;
		int lx1 = x1 + 6 * DET_TEXT_SCALE * 12; /* worst-case label width */
		if (ty < y1)
			y1 = ty;
		if (lx1 > x2)
			x2 = lx1;
		if (!have) {
			*bx0 = x1; *by0 = y1; *bx1 = x2; *by1 = y2;
			have = 1;
		} else {
			if (x1 < *bx0) *bx0 = x1;
			if (y1 < *by0) *by0 = y1;
			if (x2 > *bx1) *bx1 = x2;
			if (y2 > *by1) *by1 = y2;
		}
	}
	return g_ndets;
}

int draw_detections_i4(uint8_t *bmpData, uint32_t w, uint32_t h, uint32_t rowStride) {
	det_t *dets = g_dets;
	int n = g_ndets;
	static int last_logged = -1;
	if (verbose && n != last_logged) {
		printf("[FUSION] drawing %d box(es) on %ux%u stride %u\n", n, w, h, rowStride);
		last_logged = n;
	}
	for (int i = 0; i < n; i++) {
		int x1 = (int)(dets[i].x1 * (float)w);
		int y1 = (int)(dets[i].y1 * (float)h);
		int x2 = (int)(dets[i].x2 * (float)w);
		int y2 = (int)(dets[i].y2 * (float)h);
		if (x2 - x1 < 4 || y2 - y1 < 4)
			continue;
		/* Thin red outline only - no halo, exactly like the original worker */
		rect(bmpData, w, h, rowStride, x1, y1, x2, y2, DET_THICK, DET_BOX);

		/* Label "NAME NN%" above the box (inside the top edge if there is no
		 * room), on a dark backdrop so it reads over any scene. */
		char label[40];
		const char *nm = (dets[i].cls >= 0 && dets[i].cls < (int)(sizeof(det_names) / sizeof(det_names[0])))
							 ? det_names[dets[i].cls] : "?";
		snprintf(label, sizeof(label), "%s %d%%", nm, dets[i].pct);
		int lh = GH * DET_TEXT_SCALE;
		int ty = (y1 - lh - 2 >= 0) ? y1 - lh - 2 : y1 + 2;
		/* No backdrop: px_behind only writes transparent pixels, so a backdrop
		 * drawn first would block the glyphs on top of it. Plain text it is. */
		draw_text(bmpData, w, h, rowStride, x1, ty, label, DET_TEXT, DET_TEXT_SCALE);
	}
	return n;
}
