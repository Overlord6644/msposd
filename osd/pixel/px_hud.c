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
	/* Top to bottom: heading readout, pointer aiming down at the ribbon,
	 * tick row, cardinal labels UNDER their ticks. The readout and pointer
	 * are fixed; only the ribbon slides. */
	const int tick_top = y;
	const int tick_len = 8, tick_major = 13;

	/* Clip so ticks scrolling in from either side stop at the ribbon's edge
	 * instead of running across the frame. Labels hang below the ticks. */
	PxCanvas rib = *c;
	px_clip(&rib, cx - half, tick_top, cx + half,
		tick_top + tick_major + text_size + 8);

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

		/* All eight winds get a name, not just the four cardinals: at a
		 * glance NW is a direction, 315 is arithmetic. */
		int named = (deg % 45) == 0;
		px_vline(&rib, x, tick_top, tick_top + (named ? tick_major : tick_len),
			color);
		if (named) {
			static const char *n[] = {"N", "NE", "E", "SE", "S", "SW",
				"W", "NW"};
			label(&rib, x, tick_top + tick_major + text_size + 2,
				n[deg / 45], text_size, deg == 0 ? accent : color, edge);
		}
	}

	/* Fixed pointer above the ribbon, aiming down at it. */
	px_fill_triangle(c, cx, tick_top - 2, cx - 7, tick_top - 11,
		cx + 7, tick_top - 11, accent);

	/* Heading in degrees above the pointer - the ribbon gives the feel,
	 * the number gives the answer. */
	/* \xb0 is the degree sign: px_text reads bytes as codepoints (Latin-1),
	 * so the UTF-8 two-byte form would render a stray glyph before it. */
	char buf[8];
	int deg = ((int)lrintf(heading_deg) % 360 + 360) % 360;
	snprintf(buf, sizeof(buf), "%d\xb0", deg);
	label(c, cx, tick_top - 16, buf, text_size + 2, accent, edge);
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
	/* `side` says where the READOUT sits, which is how a layout author thinks
	 * about it: a speed tape on the left of frame has its numbers on the left and
	 * its ticks facing in. So align=left puts the box left and the ticks right,
	 * align=right the other way round. */
	const int dir = (side == 2) ? 1 : -1;

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

	/* Current value in a box with a nose pointing at the scale.
	 *
	 * The backdrop is OPAQUE, not the semi-transparent shade: the scale label
	 * nearest the centre lands right behind this box, and through a translucent
	 * fill the two numbers overlap into an unreadable smudge. Opaque means the
	 * graduations genuinely pass behind the readout, which is how a real tape
	 * behaves. Drawn after the ticks for the same reason. */
	char cur[16];
	snprintf(cur, sizeof(cur), "%.0f", value);
	const int pad = 8;
	int cw = px_text_width(cur, text_size + 4);
	int bh = text_size + 14;
	const int nose_len = 10;
	int bx0 = (dir > 0) ? x + tick_long + 4 + nose_len
			   : x - tick_long - 4 - nose_len - cw - pad * 2;
	int bx1 = bx0 + cw + pad * 2;
	int ty0 = cy - bh / 2, ty1 = cy + bh / 2;
	/* Pointed flag rather than a rectangle with a triangle stuck on it: the
	 * outline follows the point, which is what makes it read as an indicator
	 * aimed at the scale instead of a label parked next to it. */
	int inner = (dir > 0) ? bx0 : bx1;          /* edge facing the scale */
	int apex  = inner - dir * nose_len;
	px_fill_rect(c, bx0, ty0, bx1, ty1, PX_BLACK);
	px_fill_triangle(c, apex, cy, inner, ty0, inner, ty1, PX_BLACK);
	/* Outline, five edges, skipping the one the nose replaces. */
	px_hline(c, bx0, bx1, ty0, accent);
	px_hline(c, bx0, bx1, ty1, accent);
	px_vline(c, (dir > 0) ? bx1 : bx0, ty0, ty1, accent);
	px_line(c, inner, ty0, apex, cy, accent);
	px_line(c, inner, ty1, apex, cy, accent);
	/* Baseline centred on the box, so digits sit in the middle at any size. No
	 * edge colour: the opaque backdrop already provides the contrast, and an
	 * outline inside a small box only thickens the glyphs. */
	px_text(c, bx0 + pad, cy + (text_size + 4) / 3, cur, text_size + 4,
		accent, PX_TRANSPARENT);
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
		/* Offset along the ladder's own axis, perpendicular to the horizon.
		 * The zero bar gets NO offset: it is the reference, pinned to the
		 * reticle - its centre IS the crosshair, always. Roll turns it,
		 * pitch slides the graduations past it. */
		float off = (d == 0) ? 0.0f : ((float)d - pitch_deg) * px_per_deg;
		/* Displace along (sa, ca): the true perpendicular of the rolled bar
		 * (its direction is (ca, -sa), dot product zero). Anything else and
		 * the graduations drift off the ladder's axis as roll grows, which
		 * reads as the bars being centred on the screen vertical instead of
		 * stacked square above and below the roll bar. */
		int bx = cx - (int)lrintf(sa * off);
		int by = cy - (int)lrintf(ca * off);

		int arm = (d == 0) ? half : half / 2;
		int x0 = bx - (int)lrintf(ca * (float)arm);
		int y0 = by + (int)lrintf(sa * (float)arm);
		int x1 = bx + (int)lrintf(ca * (float)arm);
		int y1 = by - (int)lrintf(sa * (float)arm);
		uint8_t col = (d == 0) ? accent : color;

		/* Both the horizon and the graduations are two segments with a gap in
		 * the middle: the aircraft reference belongs IN that gap, not under a
		 * line drawn across it. The reference bar hugs the pip tighter than
		 * the graduations do. */
		int g = (d == 0) ? gap / 2 : gap;
		int gx = (int)lrintf(ca * (float)g);
		int gy = (int)lrintf(sa * (float)g);
		int ix0 = bx - gx, iy0 = by + gy;   /* inner end, left segment  */
		int ix1 = bx + gx, iy1 = by - gy;   /* inner end, right segment */

		if (d == 0) {
			/* The reference bar wears an outline so it holds against
			 * bright sky, like every readout does. */
			if (edge != PX_TRANSPARENT) {
				px_line_thick(c, x0, y0, ix0, iy0, 4, edge);
				px_line_thick(c, ix1, iy1, x1, y1, 4, edge);
			}
			px_line_thick(c, x0, y0, ix0, iy0, 2, col);
			px_line_thick(c, ix1, iy1, x1, y1, 2, col);
			continue;
		}

		px_line(c, x0, y0, ix0, iy0, col);
		px_line(c, ix1, iy1, x1, y1, col);

		/* Perpendicular tick at the INNER end, pointing back toward the
		 * horizon: on a graduation above you it hangs down, below you it stands
		 * up, so which side of level you are on is readable without finding the
		 * sign of the label. */
		int tick = (d > 0) ? 9 : -9;
		int tx = (int)lrintf(sa * tick), ty = (int)lrintf(ca * tick);
		px_line(c, ix0, iy0, ix0 + tx, iy0 + ty, col);
		px_line(c, ix1, iy1, ix1 + tx, iy1 + ty, col);

		/* Label at the outer end of each segment, where nothing else is.
		 * Signed: below the horizon reads -10, not a bare 10 - the sign is
		 * half the information. */
		char buf[8];
		snprintf(buf, sizeof(buf), "%d", d);
		int tw = px_text_width(buf, text_size);
		px_text(c, x0 - tw - 8, y0 + text_size / 3, buf, text_size, col, edge);
		px_text(c, x1 + 8, y1 + text_size / 3, buf, text_size, col, edge);
	}
}

void px_hud_crosshair(const PxCanvas *c, int cx, int cy, int size,
	uint8_t color, uint8_t edge)
{
	if (size < 8)
		size = 8;
	const int r = size / 3;          /* ring radius   */
	const int arm = size / 2;        /* spike reach   */

	/* A small ringed pip with three ticks - up, left, right - the reticle the
	 * roll bar is centred on. No tick below: the ground side stays open, one
	 * more cue for which way is down. The ring keeps the exact centre visible
	 * against both sky and ground without a solid mass hiding what the
	 * aircraft points at. */
	if (edge != PX_TRANSPARENT) {
		px_circle(c, cx, cy, r + 1, edge);   /* dark halo, for light ground */
		px_circle(c, cx, cy, r - 1, edge);
	}
	px_circle(c, cx, cy, r, color);

	px_vline(c, cx, cy - arm - 4, cy - r - 1, color);   /* up    */
	px_hline(c, cx - arm - 4, cx - r - 1, cy, color);   /* left  */
	px_hline(c, cx + r + 1, cx + arm + 4, cy, color);   /* right */
	px_set(c, cx, cy, color);
}
