/*
 * px_icon.h - tiny vector icons for text widgets.
 *
 * Icons drawn from the same primitives as everything else, not bitmaps: they
 * scale with the layout like the text they sit next to, and cost nothing in
 * flash - a sprite sheet would need one image per size per colour.
 */
#ifndef PX_ICON_H
#define PX_ICON_H

#include "px_canvas.h"

typedef enum {
	PX_ICON_NONE = 0,
	PX_ICON_HOME,   /* house                     */
	PX_ICON_SAT,    /* satellite, panels out     */
	PX_ICON_RSSI,   /* ascending signal bars     */
	PX_ICON_LAT,    /* globe, parallels          */
	PX_ICON_LON,    /* globe, meridians          */
	PX_ICON_BATT,   /* battery, nub right        */
	PX_ICON_TRIP,   /* road - distance travelled */
} PxIconKind;

/* Draw icon `kind` with its top-left at (x, y), `h` pixels tall. Returns the
 * width actually used, so the caller can place text after it. (x, y - h) with
 * y a text baseline lines the icon up with the capitals next to it. */
int px_icon(const PxCanvas *c, int x, int y, int h, PxIconKind kind,
	uint8_t color, uint8_t edge);

/* Width without drawing, for alignment math. */
int px_icon_width(int h, PxIconKind kind);

/* Layout-file name ("home", "sat", ...) to kind; PX_ICON_NONE if unknown. */
PxIconKind px_icon_parse(const char *name);

#endif /* PX_ICON_H */
