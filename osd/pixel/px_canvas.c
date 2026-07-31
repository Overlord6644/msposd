/* px_canvas.c - see px_canvas.h. */
#include "px_canvas.h"

#include <math.h>
#include <stdlib.h>   /* abs */
#include <string.h>

void px_canvas_init(PxCanvas *c, uint8_t *data, int w, int h, int stride)
{
	c->data = data;
	c->w = w;
	c->h = h;
	c->stride = stride;
	c->behind = 0;
	c->dirty = NULL;
	px_clip_reset(c);
}

void px_clip(PxCanvas *c, int x0, int y0, int x1, int y1)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	c->clip_x0 = x0 < 0 ? 0 : x0;
	c->clip_y0 = y0 < 0 ? 0 : y0;
	c->clip_x1 = x1 >= c->w ? c->w - 1 : x1;
	c->clip_y1 = y1 >= c->h ? c->h - 1 : y1;
}

void px_clip_reset(PxCanvas *c)
{
	c->clip_x0 = 0;
	c->clip_y0 = 0;
	c->clip_x1 = c->w - 1;
	c->clip_y1 = c->h - 1;
}

void px_dirty_reset(PxDirty *d)
{
	/* Empty box: x1 < x0 means "nothing written yet". */
	d->x0 = d->y0 = 1;
	d->x1 = d->y1 = 0;
}

int px_dirty_box(const PxDirty *d, int *x0, int *y0, int *x1, int *y1)
{
	if (!d || d->x1 < d->x0)
		return 0;
	if (x0) *x0 = d->x0;
	if (y0) *y0 = d->y0;
	if (x1) *x1 = d->x1;
	if (y1) *y1 = d->y1;
	return 1;
}

void px_clear_rect(const PxCanvas *c, int x0, int y0, int x1, int y1)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= c->w) x1 = c->w - 1;
	if (y1 >= c->h) y1 = c->h - 1;
	if (x1 < x0 || y1 < y0)
		return;
	/* Whole bytes get a memset; the two edge pixels, when the span starts or
	 * ends mid-byte, are patched nibble-wise so a neighbour is not erased. */
	for (int y = y0; y <= y1; y++) {
		uint8_t *row = c->data + (size_t)y * c->stride;
		int bx0 = x0 >> 1, bx1 = x1 >> 1;
		if (x0 & 1) {
			row[bx0] |= 0xF0; /* odd pixel = high nibble */
			bx0++;
		}
		if (!(x1 & 1) && bx1 >= bx0) {
			row[bx1] |= 0x0F;
			bx1--;
		}
		if (bx1 >= bx0)
			memset(row + bx0, 0xFF, (size_t)(bx1 - bx0 + 1));
	}
}

void px_clear(const PxCanvas *c)
{
	/* 0xFF = two transparent pixels; this is what msposd memsets. */
	memset(c->data, 0xFF, (size_t)c->stride * (size_t)c->h);
}

/* Nibble order matches setPixelI4 in bmp/bitmap.c on SigmaStar: the ODD x sits
 * in the high nibble. Getting this backwards mirrors every pixel pair, which
 * looks like a subtly corrupted image rather than an obvious bug. */
void px_set(const PxCanvas *c, int x, int y, uint8_t color)
{
	if (x < c->clip_x0 || y < c->clip_y0 || x > c->clip_x1 || y > c->clip_y1)
		return;
	if (c->dirty) {
		PxDirty *d = c->dirty;
		if (d->x1 < d->x0) { /* first write since the reset */
			d->x0 = d->x1 = x;
			d->y0 = d->y1 = y;
		} else {
			if (x < d->x0) d->x0 = x;
			if (x > d->x1) d->x1 = x;
			if (y < d->y0) d->y0 = y;
			if (y > d->y1) d->y1 = y;
		}
	}
	uint8_t *p = c->data + (size_t)y * c->stride + (size_t)(x >> 1);
	if (x & 1) {
		if (c->behind && (*p >> 4) != PX_TRANSPARENT)
			return;
		*p = (uint8_t)((*p & 0x0F) | (color << 4));
	} else {
		if (c->behind && (*p & 0x0F) != PX_TRANSPARENT)
			return;
		*p = (uint8_t)((*p & 0xF0) | (color & 0x0F));
	}
}

uint8_t px_get(const PxCanvas *c, int x, int y)
{
	if (x < 0 || y < 0 || x >= c->w || y >= c->h)
		return PX_TRANSPARENT;
	uint8_t b = c->data[(size_t)y * c->stride + (size_t)(x >> 1)];
	return (x & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
}

void px_hline(const PxCanvas *c, int x0, int x1, int y, uint8_t color)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	for (int x = x0; x <= x1; x++)
		px_set(c, x, y, color);
}

void px_vline(const PxCanvas *c, int x, int y0, int y1, uint8_t color)
{
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	for (int y = y0; y <= y1; y++)
		px_set(c, x, y, color);
}

void px_line(const PxCanvas *c, int x0, int y0, int x1, int y1, uint8_t color)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		px_set(c, x0, y0, color);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

void px_line_thick(const PxCanvas *c, int x0, int y0, int x1, int y1,
	int thickness, uint8_t color)
{
	if (thickness < 2) {
		px_line(c, x0, y0, x1, y1, color);
		return;
	}
	/* Offset copies along the normal. Cheap, and good enough for OSD strokes:
	 * a proper polygon fill would cost more than it buys at 1-3 px widths. */
	float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.5f) {
		px_set(c, x0, y0, color);
		return;
	}
	float nx = -dy / len, ny = dx / len;
	for (int i = 0; i < thickness; i++) {
		float o = (float)i - (float)(thickness - 1) / 2.0f;
		int ox = (int)lrintf(nx * o), oy = (int)lrintf(ny * o);
		px_line(c, x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
	}
}

void px_rect(const PxCanvas *c, int x0, int y0, int x1, int y1, int thickness,
	uint8_t color)
{
	if (thickness < 1)
		thickness = 1;
	for (int t = 0; t < thickness; t++) {
		px_hline(c, x0 + t, x1 - t, y0 + t, color);
		px_hline(c, x0 + t, x1 - t, y1 - t, color);
		px_vline(c, x0 + t, y0 + t, y1 - t, color);
		px_vline(c, x1 - t, y0 + t, y1 - t, color);
	}
}

void px_fill_rect(const PxCanvas *c, int x0, int y0, int x1, int y1,
	uint8_t color)
{
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	for (int y = y0; y <= y1; y++)
		px_hline(c, x0, x1, y, color);
}

void px_circle(const PxCanvas *c, int cx, int cy, int r, uint8_t color)
{
	if (r <= 0)
		return;
	int x = r, y = 0, err = 1 - r;
	while (x >= y) {
		px_set(c, cx + x, cy + y, color);
		px_set(c, cx + y, cy + x, color);
		px_set(c, cx - y, cy + x, color);
		px_set(c, cx - x, cy + y, color);
		px_set(c, cx - x, cy - y, color);
		px_set(c, cx - y, cy - x, color);
		px_set(c, cx + y, cy - x, color);
		px_set(c, cx + x, cy - y, color);
		y++;
		if (err < 0) {
			err += 2 * y + 1;
		} else {
			x--;
			err += 2 * (y - x) + 1;
		}
	}
}

void px_disc(const PxCanvas *c, int cx, int cy, int r, uint8_t color)
{
	for (int y = -r; y <= r; y++) {
		int dx = (int)lrintf(sqrtf((float)(r * r - y * y)));
		px_hline(c, cx - dx, cx + dx, cy + y, color);
	}
}

void px_arc(const PxCanvas *c, int cx, int cy, int r, float a0_deg,
	float a1_deg, uint8_t color)
{
	if (r <= 0)
		return;
	if (a1_deg < a0_deg) { float t = a0_deg; a0_deg = a1_deg; a1_deg = t; }
	/* One step per pixel of arc length keeps the stroke continuous without
	 * drawing the same pixel dozens of times on large radii. */
	float span = (a1_deg - a0_deg) * (float)M_PI / 180.0f;
	int steps = (int)(fabsf(span) * (float)r) + 2;
	for (int i = 0; i <= steps; i++) {
		float a = a0_deg * (float)M_PI / 180.0f +
			span * (float)i / (float)steps;
		px_set(c, cx + (int)lrintf(cosf(a) * (float)r),
			cy - (int)lrintf(sinf(a) * (float)r), color);
	}
}

/* Midpoint ellipse, both quadrant loops, so the outline stays 1 px whatever the
 * aspect ratio - stepping one axis only leaves gaps on elongated ellipses. */
void px_ellipse(const PxCanvas *c, int x0, int y0, int x1, int y1,
	uint8_t color)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	int a = (x1 - x0) / 2, b = (y1 - y0) / 2;
	if (a <= 0 || b <= 0)
		return;
	int cx = x0 + a, cy = y0 + b;
	long a2 = (long)a * a, b2 = (long)b * b;

	long x = 0, y = b;
	long sigma = 2 * b2 + a2 * (1 - 2 * b);
	while (b2 * x <= a2 * y) {
		px_set(c, cx + (int)x, cy + (int)y, color);
		px_set(c, cx - (int)x, cy + (int)y, color);
		px_set(c, cx + (int)x, cy - (int)y, color);
		px_set(c, cx - (int)x, cy - (int)y, color);
		if (sigma >= 0) {
			sigma += 4 * a2 * (1 - y);
			y--;
		}
		sigma += b2 * (4 * x + 6);
		x++;
	}
	x = a;
	y = 0;
	sigma = 2 * a2 + b2 * (1 - 2 * a);
	while (a2 * y <= b2 * x) {
		px_set(c, cx + (int)x, cy + (int)y, color);
		px_set(c, cx - (int)x, cy + (int)y, color);
		px_set(c, cx + (int)x, cy - (int)y, color);
		px_set(c, cx - (int)x, cy - (int)y, color);
		if (sigma >= 0) {
			sigma += 4 * b2 * (1 - x);
			x--;
		}
		sigma += a2 * (4 * y + 6);
		y++;
	}
}

void px_fill_ellipse(const PxCanvas *c, int x0, int y0, int x1, int y1,
	uint8_t color)
{
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	int a = (x1 - x0) / 2, b = (y1 - y0) / 2;
	if (a <= 0 || b <= 0)
		return;
	int cx = x0 + a, cy = y0 + b;
	for (int dy = -b; dy <= b; dy++) {
		/* Solve the ellipse equation for x on this row. */
		float t = 1.0f - ((float)dy * (float)dy) / ((float)b * (float)b);
		if (t < 0.0f)
			continue;
		int dx = (int)lrintf((float)a * sqrtf(t));
		px_hline(c, cx - dx, cx + dx, cy + dy, color);
	}
}

void px_triangle(const PxCanvas *c, int x0, int y0, int x1, int y1, int x2,
	int y2, uint8_t color)
{
	px_line(c, x0, y0, x1, y1, color);
	px_line(c, x1, y1, x2, y2, color);
	px_line(c, x2, y2, x0, y0, color);
}

/* Scanline fill by sorting vertices on y and walking the two active edges.
 * Integer-only: no risk of an off-by-one row from float rounding. */
void px_fill_triangle(const PxCanvas *c, int x0, int y0, int x1, int y1,
	int x2, int y2, uint8_t color)
{
	int xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
	for (int i = 0; i < 2; i++)
		for (int j = i + 1; j < 3; j++)
			if (ys[j] < ys[i]) {
				int t = ys[i]; ys[i] = ys[j]; ys[j] = t;
				t = xs[i]; xs[i] = xs[j]; xs[j] = t;
			}
	if (ys[0] == ys[2]) {
		int lo = xs[0] < xs[1] ? (xs[0] < xs[2] ? xs[0] : xs[2])
				       : (xs[1] < xs[2] ? xs[1] : xs[2]);
		int hi = xs[0] > xs[1] ? (xs[0] > xs[2] ? xs[0] : xs[2])
				       : (xs[1] > xs[2] ? xs[1] : xs[2]);
		px_hline(c, lo, hi, ys[0], color);
		return;
	}
	for (int y = ys[0]; y <= ys[2]; y++) {
		/* Long edge 0-2 always spans the whole height. */
		int xa = xs[0] + (xs[2] - xs[0]) * (y - ys[0]) / (ys[2] - ys[0]);
		int xb;
		if (y < ys[1])
			xb = (ys[1] == ys[0]) ? xs[1]
				: xs[0] + (xs[1] - xs[0]) * (y - ys[0]) / (ys[1] - ys[0]);
		else
			xb = (ys[2] == ys[1]) ? xs[1]
				: xs[1] + (xs[2] - xs[1]) * (y - ys[1]) / (ys[2] - ys[1]);
		px_hline(c, xa, xb, y, color);
	}
}
