/*
 * px_hud.h - the instrument widgets a flight HUD is actually made of.
 *
 * A character-grid OSD can print numbers; it cannot draw a heading ribbon that
 * scrolls smoothly, a speed tape whose ticks slide by, or a pitch ladder rotated
 * to the current roll. Those need arbitrary pixels, which is the whole reason
 * this layer exists - so they live here rather than being approximated with
 * text.
 *
 * Each takes the widget geometry it was given and the one telemetry value it
 * displays, and draws into the canvas. No state: everything is derived per frame
 * from the value, which keeps them compatible with the dirty-rectangle cache.
 */
#ifndef PX_HUD_H
#define PX_HUD_H

#include "px_canvas.h"

/* Heading ribbon: ticks every `step` degrees across `w` pixels spanning `span`
 * degrees, cardinal letters where they fall, and a fixed pointer at the centre.
 * Centred on (cx, y). */
void px_hud_compass(const PxCanvas *c, int cx, int y, int w, float heading_deg,
	float span_deg, float step_deg, int text_size, uint8_t color,
	uint8_t accent, uint8_t edge);

/* Vertical tape: a moving window of `span` units over `h` pixels, ticks every
 * `step`, labels every other tick, and the current value in a box that points
 * at the scale. `side` 0 puts the box on the left of the ticks (a speed tape on
 * the left of frame), 2 on the right (an altitude tape on the right). */
void px_hud_tape(const PxCanvas *c, int x, int y, int h, float value,
	float span, float step, int side, int text_size, uint8_t color,
	uint8_t accent, uint8_t edge);

/* Pitch ladder: horizon bars every `step` degrees, rotated by roll and shifted
 * by pitch, each labelled with its angle. The zero bar is drawn solid and the
 * others broken, the way an attitude indicator distinguishes the horizon from
 * the graduations. */
void px_hud_ladder(const PxCanvas *c, int cx, int cy, int w, int h,
	float roll_deg, float pitch_deg, float px_per_deg, float step_deg,
	int text_size, uint8_t color, uint8_t accent, uint8_t edge);

/* Fixed aircraft reference at the centre of frame. */
void px_hud_crosshair(const PxCanvas *c, int cx, int cy, int size,
	uint8_t color, uint8_t edge);

#endif /* PX_HUD_H */
