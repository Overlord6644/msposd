/* px_hud.c - see px_hud.h. */
#include "px_hud.h"
#include "px_text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void label(const PxCanvas *c, int cx, int y, const char *s, int size,
	uint8_t color, uint8_t edge)
{
	int w = px_text_width(s, size);
	px_text(c, cx - w / 2, y, s, size, color, edge);
}

void px_hud_compass(const PxCanvas *c, int cx, int y, int w, float heading_deg,
	float span_deg, float step_deg, int text_size, uint8_t color,
	uint8_t accent, uint8_t edge)
{
	if (w <= 0 || span_deg <= 0.0f || step_deg <= 0.0f)
		return;
	const float px_per_deg = (float)w / span_deg;
	const int half = w / 2;
	/* Clip so ticks scrolling in from either side stop at the ribbon's edge
	 * instead of running across the frame. */
	PxCanvas rib = *c;
	px_clip(&rib, cx - half, y - text_size - 14, cx + half, y + 16);

	/* Walk absolute headings around the current one so the ticks stay pinned to
	 * the world and slide past the aircraft, which is what makes a ribbon
	 * readable - drawing ticks relative to the centre would make them stand
	 * still and the letters jump. */
	float first = heading_deg - span_deg / 2.0f;
	float start = floorf(first / step_deg) * step_deg;
	for (float a = start; a <= heading_deg + span_deg / 2.0f + step_deg; a += step_deg) {
		float delta = a - heading_deg;
		int x = cx + (int)lrintf(delta * px_per_deg);
		int deg = ((int)lrintf(a) % 360 + 360) % 360;

		int cardinal = (deg % 90) == 0;
		px_vline(&rib, x, y - (cardinal ? 12 : 7), y, color);
		if (cardinal) {
			static const char *n[] = {"N", "E", "S", "W"};
			label(&rib, x, y - 16, n[deg / 90], text_size,
				deg == 0 ? accent : color, edge);
		}
	}
	/* Fixed pointer: a filled triangle at the centre, so the ribbon reads
	 * against the aircraft rather than the frame. */
	px_fill_triangle(c, cx, y + 10, cx - 7, y + 1, cx + 7, y + 1, accent);
}

void px_hud_tape(const PxCanvas *c, int x, int y, int h, float value,
	float span, float step, int side, int text_size, uint8_t color,
	uint8_t accent, uint8_t edge)
{
	if (h <= 0 || span <= 0.0f || step <= 0.0f)
		return;
	const float px_per_unit = (float)h / span;
	const int cy = y + h / 2;
	const int tick_long = 14, tick_short = 8;
	const int dir = (side == 2) ? -1 : 1;   /* which way the ticks point */

	PxCanvas tape = *c;
	px_clip(&tape, x - 90, y, x + 90, y + h);

	/* Ticks are pinned to values, not to the widget, so they slide as the value
	 * changes - the movement is the reading. */
	float first = value - span / 2.0f;
	float start = floorf(first / step) * step;
	for (float v = start; v <= value + span / 2.0f + step; v += step) {
		int ty = cy - (int)lrintf((v - value) * px_per_unit);
		/* Label every other tick: at typical spans, labelling all of them
		 * collides with the value box. */
		int major = (fmodf(fabsf(v), step * 2.0f) < step * 0.5f);
		int len = major ? tick_long : tick_short;
		px_hline(&tape, x, x + dir * len, ty, color);
		if (major) {
			char buf[16];
			snprintf(buf, sizeof(buf), "%.0f", v);
			int tw = px_text_width(buf, text_size);
			int tx = (dir > 0) ? x + len + 6 : x - len - 6 - tw;
			px_text(&tape, tx, ty + text_size / 3, buf, text_size, color, edge);
		}
	}
	px_vline(&tape, x, y, y + h, color);

	/* Current value in a box with a nose pointing at the scale. */
	char cur[16];
	snprintf(cur, sizeof(cur), "%.0f", value);
	int cw = px_text_width(cur, text_size + 4);
	int bh = text_size + 12;
	int bx0 = (dir > 0) ? x + tick_long + 4 : x - tick_long - 4 - cw - 14;
	int bx1 = bx0 + cw + 14;
	px_fill_rect(c, bx0, cy - bh / 2, bx1, cy + bh / 2, PX_SHADE);
	px_rect(c, bx0, cy - bh / 2, bx1, cy + bh / 2, 1, accent);
	px_text(c, bx0 + 7, cy + (text_size + 4) / 3, cur, text_size + 4, accent, edge);
	int nose = (dir > 0) ? bx0 : bx1;
	px_fill_triangle(c, nose - dir * 8, cy, nose, cy - 6, nose, cy + 6, accent);
}

void px_hud_ladder(const PxCanvas *c, int cx, int cy, int w, int h,
	float roll_deg, float pitch_deg, float px_per_deg, float step_deg,
	int text_size, uint8_t color, uint8_t accent, uint8_t edge)
{
	if (w <= 0 || step_deg <= 0.0f)
		return;
	if (h <= 0)
		h = 360;
	/* Confine the ladder to its own box. Without this it runs the full height of
	 * the frame - at 8 px per degree that is over 60 degrees of graduations
	 * colliding with everything else on screen, which is not what an attitude
	 * indicator looks like. */
	PxCanvas lad = *c;
	px_clip(&lad, cx - w, cy - h / 2, cx + w, cy + h / 2);
	c = &lad;
	const float a = roll_deg * (float)M_PI / 180.0f;
	const float ca = cosf(a), sa = sinf(a);
	const int half = w / 2;
	const int gap = w / 8;   /* centre gap, so the reticle stays readable */

	/* Only the bars that can appear on screen: at 8 px per degree a 1080-tall
	 * frame holds about +/-67 degrees, and drawing beyond that is wasted work
	 * the dirty-rect cache would then have to clear. */
	/* Only the graduations that can land inside the box. */
	int reach = (int)(((float)h / 2.0f) / px_per_deg) + (int)step_deg * 2;
	for (int d = -reach; d <= reach; d += (int)step_deg) {
		/* Offset along the ladder's own axis, perpendicular to the horizon. */
		float off = ((float)d - pitch_deg) * px_per_deg;
		int ox = (int)lrintf(-sa * off);
		int oy = (int)lrintf(-ca * off);
		int bx = cx + ox, by = cy - oy;

		int arm = (d == 0) ? half : half / 2;
		int x0 = bx - (int)lrintf(ca * (float)arm);
		int y0 = by + (int)lrintf(sa * (float)arm);
		int x1 = bx + (int)lrintf(ca * (float)arm);
		int y1 = by - (int)lrintf(sa * (float)arm);
		uint8_t col = (d == 0) ? accent : color;

		if (d == 0) {
			/* The horizon is one continuous bar - it is the reference. */
			px_line_thick(c, x0, y0, x1, y1, 2, col);
		} else {
			/* Graduations are broken either side of centre, and the ends turn
			 * toward the horizon so up and down are unambiguous. */
			int gx = (int)lrintf(ca * (float)gap);
			int gy = (int)lrintf(sa * (float)gap);
			px_line(c, x0, y0, bx - gx, by + gy, col);
			px_line(c, bx + gx, by - gy, x1, y1, col);
			int tick = (d > 0) ? 7 : -7;
			px_line(c, x0, y0, x0 + (int)lrintf(sa * tick),
				y0 + (int)lrintf(ca * tick), col);
			px_line(c, x1, y1, x1 + (int)lrintf(sa * tick),
				y1 + (int)lrintf(ca * tick), col);

			char buf[8];
			snprintf(buf, sizeof(buf), "%d", d);
			int tw = px_text_width(buf, text_size);
			px_text(c, x0 - tw - 8, y0 + text_size / 3, buf, text_size, col, edge);
			px_text(c, x1 + 8, y1 + text_size / 3, buf, text_size, col, edge);
		}
	}
}

void px_hud_crosshair(const PxCanvas *c, int cx, int cy, int size,
	uint8_t color, uint8_t edge)
{
	if (size < 6)
		size = 6;
	int arm = size / 2;
	/* Corner brackets rather than a full cross: they mark the centre without
	 * covering what is at it. */
	for (int sx = -1; sx <= 1; sx += 2)
		for (int sy = -1; sy <= 1; sy += 2) {
			int x = cx + sx * arm, y = cy + sy * arm;
			px_hline(c, x, x - sx * (arm / 2), y, color);
			px_vline(c, x, y, y - sy * (arm / 2), color);
		}
	px_set(c, cx, cy, color);
	(void)edge;
}
