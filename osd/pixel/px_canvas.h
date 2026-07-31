/*
 * px_canvas.h - pixel drawing surface for the OSD.
 *
 * The point of this module is to stop thinking in character cells. The RGN
 * overlay is a real framebuffer (1920x1080, I4, direct-mapped memory), so
 * anything can be drawn at any pixel: lines at arbitrary angles, arcs, gauges,
 * an artificial horizon that is not quantised to a 53x20 grid, and TrueType
 * text instead of a fixed bitmap font.
 *
 * I4 is 4 bits per pixel: two pixels per byte, each a palette index. The palette
 * (bmp/bitmap.c, g_stPaletteTable) has 16 entries, so colour is a small fixed
 * set - which is why anti-aliasing here means picking from a short ramp rather
 * than blending freely.
 *
 * The same code renders on the camera and on the host preview harness, so
 * layout work needs neither the camera nor a flight.
 */
#ifndef PX_CANVAS_H
#define PX_CANVAS_H

#include <stdint.h>

/* Palette indices, from g_stPaletteTable in bmp/bitmap.c. Named here so call
 * sites read as intent rather than magic numbers. */
#define PX_RED         1
#define PX_GREEN       2
#define PX_BLUE        3
#define PX_YELLOW      4
#define PX_MAGENTA     5
#define PX_CYAN        6
#define PX_WHITE       7
#define PX_BLACK       8
#define PX_SHADE       9  /* semi-transparent black - backdrops */
#define PX_GRAY_LIGHT 13
#define PX_GRAY_DARK  14
#define PX_TRANSPARENT 15 /* the value msposd clears the canvas to */

/* Bounding box of the pixels a drawing pass touched, inclusive; empty when
 * x1 < x0. Held out of line so px_set() can update it through a const PxCanvas*
 * - the canvas is not being modified, its dirty accumulator is. */
typedef struct {
	int x0, y0, x1, y1;
} PxDirty;

typedef struct {
	uint8_t *data;   /* I4 pixels, two per byte */
	int      w, h;   /* pixel dimensions */
	int      stride; /* bytes per row */
	/* When set, writes land only on still-transparent pixels, so this pass
	 * renders *behind* whatever is already on the canvas. Used to keep AI
	 * detection boxes from punching through the OSD drawn before them. */
	int      behind;
	/* Clip rectangle, inclusive. Widgets set it so a gauge or a scrolling
	 * ladder cannot spill outside its own box - the same purpose as
	 * FrSky's OSD_CMD_DRAWING_CLIP_TO_RECT. */
	int      clip_x0, clip_y0, clip_x1, clip_y1;
	/* Optional write accumulator. NULL disables tracking, which costs one
	 * predictable branch per pixel; set it to learn exactly which region a
	 * pass touched and refresh only that instead of memsetting a megabyte of
	 * I4 every frame. */
	PxDirty *dirty;
} PxCanvas;

void px_canvas_init(PxCanvas *c, uint8_t *data, int w, int h, int stride);
void px_clear(const PxCanvas *c);

/* Restrict drawing to a rectangle (inclusive); px_clip_reset() reopens the
 * whole canvas. Not stacked - callers save and restore if they need nesting. */
void px_clip(PxCanvas *c, int x0, int y0, int x1, int y1);
void px_clip_reset(PxCanvas *c);

/* ---- dirty tracking ----
 * px_set() grows the dirty box on every write. Reset it, draw, then read the
 * box back to learn exactly what a widget touched. */
void px_dirty_reset(PxDirty *d);
/* 1 if anything was written since the reset, filling the (inclusive) box. */
int  px_dirty_box(const PxDirty *d, int *x0, int *y0, int *x1, int *y1);
/* Set a rectangle to transparent - clearing one widget's area rather than the
 * whole canvas. */
void px_clear_rect(const PxCanvas *c, int x0, int y0, int x1, int y1);

/* Single pixel, bounds-checked, honouring `behind`. */
void px_set(const PxCanvas *c, int x, int y, uint8_t color);
/* Palette index at (x,y), or PX_TRANSPARENT when out of bounds. */
uint8_t px_get(const PxCanvas *c, int x, int y);

void px_hline(const PxCanvas *c, int x0, int x1, int y, uint8_t color);
void px_vline(const PxCanvas *c, int x, int y0, int y1, uint8_t color);
/* Arbitrary-angle line (Bresenham) - the whole reason for a pixel OSD. */
void px_line(const PxCanvas *c, int x0, int y0, int x1, int y1, uint8_t color);
/* Same, thickened perpendicular to its direction. */
void px_line_thick(const PxCanvas *c, int x0, int y0, int x1, int y1,
	int thickness, uint8_t color);

void px_rect(const PxCanvas *c, int x0, int y0, int x1, int y1, int thickness,
	uint8_t color);
void px_fill_rect(const PxCanvas *c, int x0, int y0, int x1, int y1,
	uint8_t color);

void px_circle(const PxCanvas *c, int cx, int cy, int r, uint8_t color);
void px_disc(const PxCanvas *c, int cx, int cy, int r, uint8_t color);
/* Arc from a0 to a1 (degrees, 0 = +x axis, counter-clockwise). */
void px_arc(const PxCanvas *c, int cx, int cy, int r, float a0_deg,
	float a1_deg, uint8_t color);

/* Ellipse inscribed in the given rectangle. More general than a circle and
 * what an OSD actually needs for non-square dials. */
void px_ellipse(const PxCanvas *c, int x0, int y0, int x1, int y1,
	uint8_t color);
void px_fill_ellipse(const PxCanvas *c, int x0, int y0, int x1, int y1,
	uint8_t color);

/* Triangles - arrows, home markers, reticles. */
void px_triangle(const PxCanvas *c, int x0, int y0, int x1, int y1, int x2,
	int y2, uint8_t color);
void px_fill_triangle(const PxCanvas *c, int x0, int y0, int x1, int y1,
	int x2, int y2, uint8_t color);

#endif /* PX_CANVAS_H */
