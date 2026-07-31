/*
 * px_text.h - TrueType text straight onto the I4 pixel canvas.
 *
 * This is what removes the character grid. The classic MSP DisplayPort OSD
 * blits fixed-size glyphs from a font PNG into 53x20 cells: size is whatever
 * the PNG is, positions snap to cells, and anything the font does not contain
 * cannot be drawn. Here a glyph is rasterised from a TTF at an arbitrary pixel
 * size and placed at an arbitrary pixel position.
 *
 * Anti-aliasing has to live within the 16-entry I4 palette, so coverage is
 * quantised onto a short ramp instead of blended. Text also gets a dark edge
 * for free out of that quantisation, which is what makes it readable over
 * moving video - the reason FPV OSDs outline their glyphs.
 */
#ifndef PX_TEXT_H
#define PX_TEXT_H

#include "px_canvas.h"

/* Load a TTF once; subsequent calls with the same path are no-ops.
 * Returns 0 on success. */
int px_font_load(const char *path);
void px_font_free(void);
int px_font_ready(void);

/* Draw `text` with its LEFT BASELINE at (x, y), at `size_px` em size.
 * Returns the advance in pixels (so callers can chain runs).
 * `color` is the glyph body; `edge` outlines it (PX_TRANSPARENT = no edge). */
int px_text(const PxCanvas *c, int x, int y, const char *text, int size_px,
	uint8_t color, uint8_t edge);

/* Advance width of `text` at `size_px`, without drawing. */
int px_text_width(const char *text, int size_px);

/* Ascent and descent in pixels at `size_px`, for vertical layout. */
void px_text_metrics(int size_px, int *ascent, int *descent);

#endif /* PX_TEXT_H */
