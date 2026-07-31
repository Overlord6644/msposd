/* px_icon.c - see px_icon.h.
 *
 * The icons are colour bitmaps (Twemoji, quantised to the I4 palette - see
 * px_icon_data.h), not vector doodles: a house everyone recognises beats a
 * house rebuilt from four rectangles. 40x40 at 4bpp is 800 bytes per icon,
 * and the layout only ever asks for smaller, so drawing is a pure
 * nearest-neighbour downsample - no interpolation exists at 4bpp anyway.
 */
#include "px_icon.h"
#include "px_icon_data.h"

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
	default:           return NULL;
	}
}

int px_icon_width(int h, PxIconKind kind)
{
	return kind == PX_ICON_NONE ? 0 : h; /* the emoji cell is square */
}

int px_icon(const PxCanvas *c, int x, int y, int h, PxIconKind kind,
	uint8_t color, uint8_t edge)
{
	(void)color;
	(void)edge; /* the bitmap brings its own colours */
	const uint8_t *bm = icon_bits(kind);
	if (!c || !bm || h < 6)
		return 0;
	const int S = PX_ICON_BM_SIZE;
	for (int ty = 0; ty < h; ty++) {
		int sy = (ty * S) / h;
		const uint8_t *row = bm + sy * (S / 2);
		for (int tx = 0; tx < h; tx++) {
			int sx = (tx * S) / h;
			uint8_t b = row[sx >> 1];
			uint8_t idx = (sx & 1) ? (b & 0x0F) : (b >> 4);
			if (idx != PX_TRANSPARENT)
				px_set(c, x + tx, y + ty, idx);
		}
	}
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
	return PX_ICON_NONE;
}
