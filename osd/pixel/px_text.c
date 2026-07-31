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
	g_font_path[0] = '\0';
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

int px_text_width(const char *text, int size_px)
{
	if (!g_font || !text || size_px <= 0)
		return 0;
	SFT sft;
	sft_for_size(&sft, size_px);
	double pen = 0.0;
	for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
		SFT_Glyph gid;
		SFT_GMetrics gm;
		if (sft_lookup(&sft, *s, &gid) < 0)
			continue;
		if (sft_gmetrics(&sft, gid, &gm) < 0)
			continue;
		pen += gm.advanceWidth;
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

	uint8_t *bmp = malloc(PX_GLYPH_MAX * PX_GLYPH_MAX);
	if (!bmp)
		return 0;

	double pen = (double)x;
	for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
		SFT_Glyph gid;
		SFT_GMetrics gm;
		if (sft_lookup(&sft, *s, &gid) < 0)
			continue;
		if (sft_gmetrics(&sft, gid, &gm) < 0)
			continue;

		int gw = gm.minWidth, gh = gm.minHeight;
		if (gw > 0 && gh > 0 && gw <= PX_GLYPH_MAX && gh <= PX_GLYPH_MAX) {
			SFT_Image img;
			img.pixels = bmp;
			img.width = gw;
			img.height = gh;
			memset(bmp, 0, (size_t)gw * (size_t)gh);
			if (sft_render(&sft, gid, img) == 0) {
				int ox = (int)(pen + gm.leftSideBearing + 0.5);
				int oy = y + gm.yOffset;
				/* Edge pass first, body second, so the body is
				 * never eaten by the outline of its own glyph. */
				if (edge != PX_TRANSPARENT) {
					for (int gy = 0; gy < gh; gy++)
						for (int gx = 0; gx < gw; gx++) {
							uint8_t cov = bmp[gy * gw + gx];
							if (cov >= PX_COV_EDGE && cov < PX_COV_BODY)
								px_set(c, ox + gx, oy + gy, edge);
						}
				}
				for (int gy = 0; gy < gh; gy++)
					for (int gx = 0; gx < gw; gx++)
						if (bmp[gy * gw + gx] >= PX_COV_BODY)
							px_set(c, ox + gx, oy + gy, color);
			}
		}
		pen += gm.advanceWidth;
	}

	free(bmp);
	return (int)(pen + 0.5) - x;
}
