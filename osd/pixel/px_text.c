/* px_text.c - see px_text.h. */
#include "px_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../bmp/lib/schrift.h"

/* One cached font for the whole process: the OSD draws from a single typeface,
 * and reloading per string would dominate the frame budget. */
static SFT_Font *g_font;
static char g_font_path[256];

/* A `size` in a layout has to mean a height on screen, not an em size.
 *
 * Faces disagree wildly about how much of the em the design fills: JetBrains
 * Mono puts a capital at 0.73 em, UAV OSD Mono - drawn from a Reaper display,
 * where the glyphs ARE the cell - fills it completely. Asking both for "size
 * 30" gives caps of 22 and 30 px, and text 44% wider in the second, so
 * swapping the font would silently invalidate every coordinate in the layout.
 *
 * So the em is scaled per face until a capital fills PX_CAP_RATIO of the
 * requested size - the ratio a typical text face has, which keeps layouts
 * tuned before this existed reading exactly as they did. */
#define PX_CAP_RATIO 0.70
#define PX_CAP_PROBE 64 /* em size used for the one-off measurement */
static double g_cap_k = 1.0;

/* Coverage thresholds that turn schrift's 8-bit alpha into palette indices.
 * Only the well-covered interior gets the body colour; partially covered edge
 * pixels get the edge colour. That yields a dark outline as a side effect of
 * quantising, which is exactly what OSD text needs over video - and it costs
 * nothing extra, unlike rendering the glyph twice at an offset. */
#define PX_COV_BODY 150 /* >= this -> body colour   */
#define PX_COV_EDGE 24  /* >= this -> edge colour   */

/* Rendering one glyph at a time into a scratch bitmap avoids allocating a
 * full-string surface. 128x128 covers sizes well past anything readable at
 * 1080p; larger requests are clamped rather than truncated silently. */
#define PX_GLYPH_MAX 128

/* ---- glyph cache ----
 *
 * Rasterising TrueType outlines is where the OSD's CPU time actually goes:
 * profiled on the flight layout, schrift's render_outline alone was ~60% of
 * the whole frame, because every "128", every "%", every coordinate digit was
 * re-rendered from its outline every frame it changed. An OSD draws the same
 * few dozen (codepoint, size) pairs forever, so the coverage bitmaps are
 * cached on first use and every draw after that is a blit.
 *
 * Direct-mapped by hash, one entry per slot: a collision re-renders and
 * replaces, which is correct just slower, and with ~100 live glyphs in a
 * 1024-slot table collisions are rare. The cache is flushed with the font. */
#define PX_GC_SLOTS 1024

typedef struct {
	SFT_Glyph gid;
	int       size;         /* requested size_px; 0 = slot empty */
	int       w, h;         /* coverage bitmap, w*h bytes; w==0 for blank */
	int       yoff;         /* gm.yOffset */
	double    lsb;          /* leftSideBearing */
	double    adv;          /* advanceWidth */
	/* Ink bounds within the bitmap (cov >= PX_COV_EDGE), so the dirty
	 * rectangle covers only real pixels; wi < 0 when the glyph is blank. */
	int       ix0, iy0, ix1, iy1;
	uint8_t  *cov;
} PxGlyphSlot;

static PxGlyphSlot g_gc[PX_GC_SLOTS];

static void gc_flush(void)
{
	for (int i = 0; i < PX_GC_SLOTS; i++) {
		free(g_gc[i].cov);
		g_gc[i].cov = NULL;
		g_gc[i].size = 0;
	}
}

int px_font_load(const char *path)
{
	if (!path || !*path)
		return -1;
	if (g_font && strcmp(g_font_path, path) == 0)
		return 0;
	SFT_Font *f = sft_loadfile(path);
	if (!f) {
		fprintf(stderr, "[px_text] cannot load font %s\n", path);
		return -1;
	}
	if (g_font)
		sft_freefont(g_font);
	g_font = f;
	gc_flush();
	snprintf(g_font_path, sizeof(g_font_path), "%s", path);

	/* Measure how much of the em a capital actually fills, once per load.
	 * 'H' is flat on both ends, so its ink height IS the cap height - no
	 * overshoot to round off, unlike 'O'. */
	g_cap_k = 1.0;
	SFT sft;
	sft.font = g_font;
	sft.xScale = (double)PX_CAP_PROBE;
	sft.yScale = (double)PX_CAP_PROBE;
	sft.xOffset = 0.0;
	sft.yOffset = 0.0;
	sft.flags = SFT_DOWNWARD_Y;
	SFT_Glyph gid;
	SFT_GMetrics gm;
	if (sft_lookup(&sft, 'H', &gid) == 0 && sft_gmetrics(&sft, gid, &gm) == 0 &&
		gm.minHeight > 0) {
		double cap = (double)gm.minHeight / (double)PX_CAP_PROBE;
		if (cap > 0.3 && cap < 1.2)
			g_cap_k = PX_CAP_RATIO / cap;
	}
	return 0;
}

void px_font_free(void)
{
	if (g_font)
		sft_freefont(g_font);
	g_font = NULL;
	gc_flush();
	g_font_path[0] = '\0';
}

/* The cached raster of one (glyph, size) pair, rendering it on first use.
 * NULL only when schrift itself fails on the glyph. */
static PxGlyphSlot *gc_get(const SFT *sft, SFT_Glyph gid, int size_px)
{
	unsigned idx = ((unsigned)gid * 2654435761u ^ (unsigned)size_px * 40503u)
		% PX_GC_SLOTS;
	PxGlyphSlot *s = &g_gc[idx];
	if (s->size == size_px && s->gid == gid && s->size != 0)
		return s;

	SFT_GMetrics gm;
	if (sft_gmetrics(sft, gid, &gm) < 0)
		return NULL;
	free(s->cov);
	memset(s, 0, sizeof(*s));
	s->gid = gid;
	s->size = size_px;
	s->yoff = gm.yOffset;
	s->lsb = gm.leftSideBearing;
	s->adv = gm.advanceWidth;
	s->ix0 = s->iy0 = 0;
	s->ix1 = s->iy1 = -1;

	int gw = gm.minWidth, gh = gm.minHeight;
	if (gw <= 0 || gh <= 0 || gw > PX_GLYPH_MAX || gh > PX_GLYPH_MAX)
		return s; /* blank (space) or oversized: advance only, no pixels */

	uint8_t *cov = malloc((size_t)gw * (size_t)gh);
	if (!cov)
		return s;
	memset(cov, 0, (size_t)gw * (size_t)gh);
	SFT_Image img = { .pixels = cov, .width = gw, .height = gh };
	if (sft_render(sft, gid, img) != 0) {
		free(cov);
		return s;
	}
	s->cov = cov;
	s->w = gw;
	s->h = gh;
	/* Ink bounds: what the dirty tracker should see, rather than the whole
	 * (padded) bitmap. */
	int x0 = gw, y0 = gh, x1 = -1, y1 = -1;
	for (int y = 0; y < gh; y++) {
		const uint8_t *row = cov + (size_t)y * gw;
		for (int x = 0; x < gw; x++)
			if (row[x] >= PX_COV_EDGE) {
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				y1 = y;
			}
	}
	s->ix0 = x0; s->iy0 = y0; s->ix1 = x1; s->iy1 = y1;
	return s;
}

/* Paint a cached coverage bitmap. The nibble writes go straight to the row -
 * clip is applied to the loop bounds once and the dirty box grows once per
 * glyph, instead of paying both per pixel in px_set. `behind` canvases (the
 * detection-box pass) never draw text, so that flag keeps the slow path. */
static void gc_blit(const PxCanvas *c, int ox, int oy, const PxGlyphSlot *s,
	uint8_t color, uint8_t edge)
{
	if (s->ix1 < s->ix0)
		return; /* blank */
	int x0 = ox + s->ix0, y0 = oy + s->iy0;
	int x1 = ox + s->ix1, y1 = oy + s->iy1;
	if (x0 < c->clip_x0) x0 = c->clip_x0;
	if (y0 < c->clip_y0) y0 = c->clip_y0;
	if (x1 > c->clip_x1) x1 = c->clip_x1;
	if (y1 > c->clip_y1) y1 = c->clip_y1;
	if (x1 < x0 || y1 < y0)
		return;
	if (c->dirty) {
		PxDirty *d = c->dirty;
		if (d->x1 < d->x0) {
			d->x0 = x0; d->y0 = y0; d->x1 = x1; d->y1 = y1;
		} else {
			if (x0 < d->x0) d->x0 = x0;
			if (y0 < d->y0) d->y0 = y0;
			if (x1 > d->x1) d->x1 = x1;
			if (y1 > d->y1) d->y1 = y1;
		}
	}
	for (int y = y0; y <= y1; y++) {
		const uint8_t *crow = s->cov + (size_t)(y - oy) * s->w + (x0 - ox);
		uint8_t *row = c->data + (size_t)y * c->stride;
		for (int x = x0; x <= x1; x++, crow++) {
			uint8_t cv = *crow;
			uint8_t col;
			if (cv >= PX_COV_BODY)
				col = color;
			else if (cv >= PX_COV_EDGE && edge != PX_TRANSPARENT)
				col = edge;
			else
				continue;
			if (c->behind) {
				px_set(c, x, y, col);
				continue;
			}
			uint8_t *p = row + (x >> 1);
			if (x & 1)
				*p = (uint8_t)((*p & 0x0F) | (col << 4));
			else
				*p = (uint8_t)((*p & 0xF0) | (col & 0x0F));
		}
	}
}

int px_font_ready(void)
{
	return g_font != NULL;
}

static void sft_for_size(SFT *sft, int size_px)
{
	sft->font = g_font;
	/* Normalised so `size` buys the same cap height in every face. */
	sft->xScale = (double)size_px * g_cap_k;
	sft->yScale = (double)size_px * g_cap_k;
	sft->xOffset = 0.0;
	sft->yOffset = 0.0;
	/* Y grows downward here, matching the canvas. */
	sft->flags = SFT_DOWNWARD_Y;
}

void px_text_metrics(int size_px, int *ascent, int *descent)
{
	if (ascent)
		*ascent = 0;
	if (descent)
		*descent = 0;
	if (!g_font || size_px <= 0)
		return;
	SFT sft;
	SFT_LMetrics lm;
	sft_for_size(&sft, size_px);
	if (sft_lmetrics(&sft, &lm) < 0)
		return;
	if (ascent)
		*ascent = (int)(lm.ascender + 0.5);
	if (descent)
		*descent = (int)(-lm.descender + 0.5);
}

/* One codepoint from a byte stream: two-byte UTF-8 sequences decode (that is
 * where the degree sign lives, and layout files are written in UTF-8 by every
 * editor on earth); a bare high byte falls back to Latin-1 rather than
 * rendering a replacement. Longer sequences yield codepoints the OSD font
 * does not have and get skipped by lookup, which is the right failure. */
static uint32_t next_cp(const unsigned char **s)
{
	uint32_t c = *(*s)++;
	if ((c & 0xE0) == 0xC0 && ((**s) & 0xC0) == 0x80) {
		c = ((c & 0x1F) << 6) | (*(*s)++ & 0x3F);
	}
	return c;
}

int px_text_width(const char *text, int size_px)
{
	if (!g_font || !text || size_px <= 0)
		return 0;
	SFT sft;
	sft_for_size(&sft, size_px);
	double pen = 0.0;
	const unsigned char *s = (const unsigned char *)text;
	while (*s) {
		uint32_t cp = next_cp(&s);
		SFT_Glyph gid;
		if (sft_lookup(&sft, cp, &gid) < 0)
			continue;
		const PxGlyphSlot *gs = gc_get(&sft, gid, size_px);
		if (gs)
			pen += gs->adv;
	}
	return (int)(pen + 0.5);
}

int px_text(const PxCanvas *c, int x, int y, const char *text, int size_px,
	uint8_t color, uint8_t edge)
{
	if (!g_font || !text || size_px <= 0)
		return 0;

	SFT sft;
	sft_for_size(&sft, size_px);

	double pen = (double)x;
	const unsigned char *s = (const unsigned char *)text;
	while (*s) {
		uint32_t cp = next_cp(&s);
		SFT_Glyph gid;
		if (sft_lookup(&sft, cp, &gid) < 0)
			continue;
		const PxGlyphSlot *gs = gc_get(&sft, gid, size_px);
		if (!gs)
			continue;
		if (gs->cov) {
			int ox = (int)(pen + gs->lsb + 0.5);
			int oy = y + gs->yoff;
			gc_blit(c, ox, oy, gs, color, edge);
		}
		pen += gs->adv;
	}

	return (int)(pen + 0.5) - x;
}
