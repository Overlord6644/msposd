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
	/* Instrument widgets: what a HUD is made of, and what a character grid
	 * cannot draw - see px_hud.h. */
	PX_W_COMPASS,   /* scrolling heading ribbon with cardinal letters */
	PX_W_TAPE,      /* vertical speed/altitude ladder with a value box */
	PX_W_LADDER,    /* pitch ladder rotated to the current roll */
	PX_W_CROSSHAIR, /* fixed aircraft reference */
	PX_W_VARIO,     /* stacked climb/descend arrows plus the rate */
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
	float min, max;    /* value range for bar/gauge */
	float pitch_scale; /* horizon and ladder: pixels per degree of pitch */
	/* Instruments show a moving window rather than a fixed range: `span` is how
	 * much of the scale is visible (degrees for a compass, units for a tape) and
	 * `step` is the tick interval. */
	float span;
	float step;
	uint8_t accent;    /* highlight: value box, pointer, north, horizon bar */
	/* Draw depth. Lower is further back; equal depths keep file order. This is
	 * what puts the artificial horizon behind everything and the readouts in
	 * front of it, without having to reorder the file. */
	int z;
	/* Which visibility set this widget belongs to, 0..7 - a different idea from
	 * `z`. A layout can hold several sets (a minimal one for racing, a full one
	 * for cruising) and show whichever the pilot selects, instead of needing
	 * separate files and a restart. */
	int layer;
	int  align;      /* 0 left, 1 centre, 2 right */
	/* Text widgets only: a small vector icon drawn before the string, sized
	 * to the text (PxIconKind, see px_icon.h). */
	uint8_t icon;
} PxWidget;

typedef struct {
	char     font[192];
	PxWidget widgets[PX_LAYOUT_MAX_WIDGETS];
	int      count;
	/* Global geometry multiplier, applied at draw time to every position, size
	 * and thickness (value ranges are data and are left alone).
	 *
	 * A layout is written in pixels and the overlay canvas is whatever the
	 * encoder is set to, so the same file at 4K would draw a 1080p-sized OSD in
	 * one corner. `scale = auto' derives the factor from the canvas height
	 * against PX_LAYOUT_REF_HEIGHT, so one file follows the stream resolution;
	 * an explicit number overrides it.
	 *
	 * Applied at draw time rather than baked in at load, because `auto' cannot
	 * know the canvas size until there is a canvas. */
	float    scale;
	int      scale_auto;
	/* Bitmask of visible layers; bit N is layer N. Default 0x01, so a layout
	 * that never mentions layers behaves exactly as before. */
	unsigned layer_mask;
	/* RC channel (1..16) whose stick position picks the layer, 0 = off. Three
	 * positions map to layers 0/1/2, which is what a three-way switch gives. */
	int      layer_channel;
} PxLayout;

/* Layouts are authored against this height; `scale = auto' is canvas_h / this. */
#define PX_LAYOUT_REF_HEIGHT 1080

/* Parse `path`. Returns 0 on success, <0 if the file cannot be read.
 * Unknown keys and unknown widget types are reported on stderr and skipped, so
 * a bad layout degrades instead of taking the OSD down mid-flight. */
int px_layout_load(PxLayout *l, const char *path);

/* Draw every widget, in file order, with the layout's scale applied. Widgets
 * whose layer is not in `layer_mask` are skipped. */
void px_layout_draw(const PxLayout *l, const PxCanvas *c, const PxTelemetry *t);

/* Show exactly the layers in `mask` (bit N = layer N). A mask of 0 is ignored
 * rather than blanking the OSD, which is never what anyone wants mid-flight. */
void px_layout_set_layers(PxLayout *l, unsigned mask);

/* Map a raw RC channel value (microseconds, ~1000..2000) to a layer mask and
 * apply it. Returns 1 if the visible set changed, so a caller can invalidate a
 * dirty-rect cache - stale widgets from the previous layer would otherwise stay
 * on screen. Does nothing when layer_channel is 0. */
int px_layout_apply_rc(PxLayout *l, int channel_us);

/* The factor px_layout_draw would use for this canvas. Exposed so a caller can
 * report what it rendered at. */
float px_layout_scale_for(const PxLayout *l, const PxCanvas *c);

/* ---- incremental drawing ----
 * Redrawing every widget every frame means clearing the whole canvas first: at
 * 1920x1080 that is a megabyte of I4 memset plus every glyph re-rasterised, for
 * values that mostly did not change. With a cache, a widget is redrawn only when
 * its displayed content changes, and only its own rectangle is cleared.
 *
 * This is only sound when the pixel OSD OWNS the canvas. If the character OSD is
 * also drawing, msposd clears the whole surface each frame and the cached pixels
 * are gone - use px_layout_draw() there.
 *
 * The cache is the caller's, so the layout itself stays const and one layout can
 * be drawn to several canvases. */
typedef struct {
	uint32_t sig[PX_LAYOUT_MAX_WIDGETS];  /* what was drawn last time */
	PxDirty  box[PX_LAYOUT_MAX_WIDGETS];  /* where it landed */
	int      primed;                      /* 0 until the first full pass */
} PxLayoutCache;

void px_layout_cache_reset(PxLayoutCache *cache);

/* Returns the number of widgets actually redrawn, so a caller can log how much
 * work a frame cost. */
int px_layout_draw_cached(const PxLayout *l, const PxCanvas *c,
	const PxTelemetry *t, PxLayoutCache *cache);

/* Same, with an extra rectangle the caller wants cleared and repaired this
 * frame - for content it draws onto the canvas OUTSIDE the layout (detection
 * boxes). The union is cleared, widgets intersecting it are redrawn in depth
 * order, and the caller then repaints its own content on top. Without this,
 * whatever the caller drew last frame would simply stay on screen: nothing in
 * the cache knows to erase it. NULL or an empty rect means none. */
int px_layout_draw_cached_ex(const PxLayout *l, const PxCanvas *c,
	const PxTelemetry *t, PxLayoutCache *cache, const PxDirty *extra);

#endif /* PX_LAYOUT_H */
