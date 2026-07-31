/*
 * px_layout.h - pixel-positioned OSD layout, read from a plain text file.
 *
 * Each widget is a section; keys inside it are its parameters. Positions are
 * PIXELS, not grid cells, which is the whole point:
 *
 *   [osd]
 *   font   = /usr/share/fonts/truetype/UbuntuMono-Regular.ttf
 *
 *   [alt]
 *   type   = text
 *   x      = 40
 *   y      = 80
 *   size   = 34
 *   color  = white
 *   edge   = black
 *   source = alt
 *   format = ALT %.0fm
 *
 *   [horizon]
 *   type   = horizon
 *   x      = 960
 *   y      = 520
 *   w      = 840
 *
 * Widgets are drawn in file order, so overlap is controlled by where a section
 * sits in the file - no z-index to reason about.
 *
 * INI rather than JSON on purpose: msposd already parses INI (vtxmenu.ini) and
 * has no JSON parser, so this adds no dependency to a binary that runs on a
 * camera. The existing helper cannot enumerate sections though, and widget order
 * matters here, so the reader below is its own thing.
 */
#ifndef PX_LAYOUT_H
#define PX_LAYOUT_H

#include "px_canvas.h"
#include "px_telemetry.h"

#define PX_LAYOUT_MAX_WIDGETS 48
#define PX_LAYOUT_STR 64

typedef enum {
	PX_W_NONE = 0,
	PX_W_TEXT,    /* formatted number or string */
	PX_W_BAR,     /* horizontal fill proportional to a value */
	PX_W_GAUGE,   /* arc plus needle */
	PX_W_HORIZON, /* artificial horizon at the real roll/pitch */
	PX_W_ARROW,   /* bearing pointer (rotated triangle) */
	PX_W_RECT,    /* static frame or backdrop */
} PxWidgetType;

typedef struct {
	PxWidgetType type;
	char name[PX_LAYOUT_STR];
	int  x, y, w, h;
	int  size;      /* text size in px, or radius for gauge/arrow */
	int  thickness;
	uint8_t color, edge, fill;
	char source[PX_LAYOUT_STR];
	char format[PX_LAYOUT_STR];
	char label[PX_LAYOUT_STR];
	float min, max;  /* value range for bar/gauge */
	float scale;     /* horizon: pixels per degree of pitch */
	int  align;      /* 0 left, 1 centre, 2 right */
} PxWidget;

typedef struct {
	char     font[192];
	PxWidget widgets[PX_LAYOUT_MAX_WIDGETS];
	int      count;
} PxLayout;

/* Parse `path`. Returns 0 on success, <0 if the file cannot be read.
 * Unknown keys and unknown widget types are reported on stderr and skipped, so
 * a bad layout degrades instead of taking the OSD down mid-flight. */
int px_layout_load(PxLayout *l, const char *path);

/* Draw every widget, in file order. */
void px_layout_draw(const PxLayout *l, const PxCanvas *c, const PxTelemetry *t);

#endif /* PX_LAYOUT_H */
