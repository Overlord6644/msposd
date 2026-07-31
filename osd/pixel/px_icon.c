/* px_icon.c - see px_icon.h.
 *
 * The RSSI icon carries its own word, drawn by px_text INSIDE the cell (bars
 * on top, RSSI under them): baking the word into the 40x40 bitmap turned to
 * mush once the cell was downscaled - the TTF at the final size stays crisp.
 *
 * The icons are colour bitmaps (Twemoji, quantised to the I4 palette - see
 * px_icon_data.h), not vector doodles: a house everyone recognises beats a
 * house rebuilt from four rectangles. 40x40 at 4bpp is 800 bytes per icon,
 * and the layout only ever asks for smaller, so drawing is a pure
 * nearest-neighbour downsample - no interpolation exists at 4bpp anyway.
 */
#include "px_icon.h"
#include "px_icon_data.h"
#include "px_text.h"

#include <string.h>

static const uint8_t *icon_bits(PxIconKind kind)
{
	switch (kind) {
	case PX_ICON_HOME: return px_icon_bm_home;
	case PX_ICON_SAT:  return px_icon_bm_sat;
	case PX_ICON_RSSI: return px_icon_bm_rssi;
	case PX_ICON_BATT: return px_icon_bm_batt;
	case PX_ICON_LAT:  return px_icon_bm_lat;
	case PX_ICON_LON:  return px_icon_bm_lon;
	case PX_ICON_TRIP: return px_icon_bm_trip;
	default:           return NULL;
	}
}

int px_icon_width(int h, PxIconKind kind)
{
	return kind == PX_ICON_NONE ? 0 : h; /* the emoji cell is square */
}

static void blit(const PxCanvas *c, const uint8_t *bm, int x, int y, int w,
	int h)
{
	const int S = PX_ICON_BM_SIZE;
	for (int ty = 0; ty < h; ty++) {
		int sy = (ty * S) / h;
		const uint8_t *row = bm + sy * (S / 2);
		for (int tx = 0; tx < w; tx++) {
			int sx = (tx * S) / w;
			uint8_t b = row[sx >> 1];
			uint8_t idx = (sx & 1) ? (b & 0x0F) : (b >> 4);
			if (idx != PX_TRANSPARENT)
				px_set(c, x + tx, y + ty, idx);
		}
	}
}

int px_icon(const PxCanvas *c, int x, int y, int h, PxIconKind kind,
	uint8_t color, uint8_t edge)
{
	(void)edge; /* the bitmap brings its own colours */
	const uint8_t *bm = icon_bits(kind);
	if (!c || !bm || h < 6)
		return 0;
	if (kind == PX_ICON_RSSI) {
		/* Bars over the word, both inside the cell. The word takes the
		 * widget's colour so it matches the number next to it. */
		int bars = (h * 7) / 10;
		int ts = h - bars;
		if (ts < 8)
			ts = 8; /* below this the TTF quantises to noise */
		blit(c, bm, x + (h - bars) / 2, y, bars, bars);
		int tw = px_text_width("RSSI", ts);
		px_text(c, x + (h - tw) / 2, y + h, "RSSI", ts, color,
			PX_TRANSPARENT);
		return h;
	}
	(void)color;
	blit(c, bm, x, y, h, h);
	return h;
}

PxIconKind px_icon_parse(const char *name)
{
	if (!name || !*name)
		return PX_ICON_NONE;
	if (!strcmp(name, "home")) return PX_ICON_HOME;
	if (!strcmp(name, "sat"))  return PX_ICON_SAT;
	if (!strcmp(name, "rssi")) return PX_ICON_RSSI;
	if (!strcmp(name, "lat"))  return PX_ICON_LAT;
	if (!strcmp(name, "lon"))  return PX_ICON_LON;
	if (!strcmp(name, "batt")) return PX_ICON_BATT;
	if (!strcmp(name, "trip")) return PX_ICON_TRIP;
	return PX_ICON_NONE;
}
