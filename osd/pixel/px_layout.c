/* px_layout.c - see px_layout.h. */
#include "px_layout.h"
#include "px_text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- telemetry name resolution ---- */

int px_telemetry_value(const PxTelemetry *t, const char *source, float *out)
{
	if (!t || !source || !out)
		return 0;
	struct { const char *n; float v; } map[] = {
		{"roll",        t->roll_deg},
		{"pitch",       t->pitch_deg},
		{"yaw",         t->yaw_deg},
		{"volt",        t->volt_v},
		{"curr",        t->curr_a},
		{"mah",         (float)t->mah_used},
		{"batt",        (float)t->batt_pct},
		{"alt",         t->alt_m},
		{"spd",         t->spd_kph},
		{"vspd",        t->vspd_ms},
		{"throttle",    (float)t->throttle_pct},
		{"sats",        (float)t->sats},
		{"home_dist",   t->home_dist_m},
		{"home_bearing",t->home_bearing_deg},
		{"rssi",        (float)t->rssi_pct},
		{"lq",          (float)t->lq_pct},
		{"armed",       (float)t->armed},
	};
	for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (strcmp(map[i].n, source) == 0) {
			*out = map[i].v;
			return 1;
		}
	return 0;
}

const char *px_telemetry_text(const PxTelemetry *t, const char *source)
{
	if (!t || !source)
		return NULL;
	if (strcmp(source, "mode") == 0)
		return t->mode;
	if (strcmp(source, "msg") == 0)
		return t->msg;
	return NULL;
}

/* ---- parsing helpers ---- */

static uint8_t parse_color(const char *s, uint8_t fallback)
{
	struct { const char *n; uint8_t v; } map[] = {
		{"red", PX_RED}, {"green", PX_GREEN}, {"blue", PX_BLUE},
		{"yellow", PX_YELLOW}, {"magenta", PX_MAGENTA}, {"cyan", PX_CYAN},
		{"white", PX_WHITE}, {"black", PX_BLACK}, {"shade", PX_SHADE},
		{"gray", PX_GRAY_LIGHT}, {"graylight", PX_GRAY_LIGHT},
		{"graydark", PX_GRAY_DARK}, {"none", PX_TRANSPARENT},
		{"transparent", PX_TRANSPARENT},
	};
	if (!s || !*s)
		return fallback;
	for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (strcmp(map[i].n, s) == 0)
			return map[i].v;
	/* A bare number is a raw palette index, for colours with no name. */
	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (end && end != s && v >= 0 && v <= 15)
		return (uint8_t)v;
	fprintf(stderr, "[px_layout] unknown colour '%s'\n", s);
	return fallback;
}

static PxWidgetType parse_type(const char *s)
{
	if (!strcmp(s, "text"))    return PX_W_TEXT;
	if (!strcmp(s, "bar"))     return PX_W_BAR;
	if (!strcmp(s, "gauge"))   return PX_W_GAUGE;
	if (!strcmp(s, "horizon")) return PX_W_HORIZON;
	if (!strcmp(s, "arrow"))   return PX_W_ARROW;
	if (!strcmp(s, "rect"))    return PX_W_RECT;
	return PX_W_NONE;
}

static void trim(char *s)
{
	char *p = s;
	while (*p == ' ' || *p == '\t')
		p++;
	if (p != s)
		memmove(s, p, strlen(p) + 1);
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
		s[n - 1] == '\n'))
		s[--n] = '\0';
}

static void widget_defaults(PxWidget *w)
{
	memset(w, 0, sizeof(*w));
	w->size = 28;
	w->thickness = 2;
	w->color = PX_WHITE;
	w->edge = PX_BLACK;
	w->fill = PX_TRANSPARENT;
	w->min = 0.0f;
	w->max = 100.0f;
	w->pitch_scale = 8.0f;
}

int px_layout_load(PxLayout *l, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "[px_layout] cannot open %s\n", path);
		return -1;
	}
	memset(l, 0, sizeof(*l));
	l->scale = 1.0f;
	snprintf(l->font, sizeof(l->font),
		"/usr/share/fonts/truetype/UbuntuMono-Regular.ttf");

	PxWidget *w = NULL;
	int in_osd = 0;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		trim(line);
		if (!*line || *line == '#' || *line == ';')
			continue;

		if (*line == '[') {
			char *close = strchr(line, ']');
			if (!close)
				continue;
			*close = '\0';
			const char *name = line + 1;
			in_osd = (strcmp(name, "osd") == 0);
			w = NULL;
			if (!in_osd) {
				if (l->count >= PX_LAYOUT_MAX_WIDGETS) {
					fprintf(stderr, "[px_layout] too many widgets, "
						"'%s' and later ignored\n", name);
					continue;
				}
				w = &l->widgets[l->count];
				widget_defaults(w);
				snprintf(w->name, sizeof(w->name), "%s", name);
				/* Counted only once its type is known, so a
				 * section with a bad type does not occupy a slot. */
			}
			continue;
		}

		char *eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *key = line, *val = eq + 1;
		trim(key);
		trim(val);

		if (in_osd) {
			if (!strcmp(key, "font")) {
				snprintf(l->font, sizeof(l->font), "%s", val);
			} else if (!strcmp(key, "scale")) {
				if (!strcmp(val, "auto")) {
					l->scale_auto = 1;
				} else {
					float sc = strtof(val, NULL);
					/* Refuse nonsense instead of rendering an
					 * invisible or unbounded OSD. */
					if (sc >= 0.1f && sc <= 8.0f) {
						l->scale = sc;
						l->scale_auto = 0;
					} else {
						fprintf(stderr, "[px_layout] scale %s out of"
							" range (0.1..8), keeping %.2f\n",
							val, l->scale);
					}
				}
			} else {
				fprintf(stderr, "[px_layout] [osd]: unknown key '%s'\n",
					key);
			}
			continue;
		}
		if (!w)
			continue;

		if (!strcmp(key, "type")) {
			PxWidgetType t = parse_type(val);
			if (t == PX_W_NONE) {
				fprintf(stderr, "[px_layout] '%s': unknown type '%s'\n",
					w->name, val);
				w = NULL;
				continue;
			}
			w->type = t;
			l->count++; /* claim the slot now that it is valid */
		} else if (!strcmp(key, "x"))          w->x = atoi(val);
		else if (!strcmp(key, "y"))            w->y = atoi(val);
		else if (!strcmp(key, "w"))            w->w = atoi(val);
		else if (!strcmp(key, "h"))            w->h = atoi(val);
		else if (!strcmp(key, "size"))         w->size = atoi(val);
		else if (!strcmp(key, "thickness"))    w->thickness = atoi(val);
		else if (!strcmp(key, "color"))        w->color = parse_color(val, PX_WHITE);
		else if (!strcmp(key, "edge"))         w->edge = parse_color(val, PX_BLACK);
		else if (!strcmp(key, "fill"))         w->fill = parse_color(val, PX_TRANSPARENT);
		else if (!strcmp(key, "min"))          w->min = strtof(val, NULL);
		else if (!strcmp(key, "max"))          w->max = strtof(val, NULL);
		else if (!strcmp(key, "pitch_scale"))  w->pitch_scale = strtof(val, NULL);
		else if (!strcmp(key, "source"))       snprintf(w->source, sizeof(w->source), "%s", val);
		else if (!strcmp(key, "format"))       snprintf(w->format, sizeof(w->format), "%s", val);
		else if (!strcmp(key, "label"))        snprintf(w->label, sizeof(w->label), "%s", val);
		else if (!strcmp(key, "align"))
			w->align = !strcmp(val, "center") ? 1 : (!strcmp(val, "right") ? 2 : 0);
		else
			fprintf(stderr, "[px_layout] '%s': unknown key '%s'\n",
				w->name, key);
	}
	fclose(f);
	if (l->scale_auto)
		printf("[px_layout] %s: %d widget(s), scale auto (ref %d px tall),"
			" font %s\n", path, l->count, PX_LAYOUT_REF_HEIGHT, l->font);
	else
		printf("[px_layout] %s: %d widget(s), scale %.2f, font %s\n",
			path, l->count, l->scale, l->font);
	return 0;
}

/* ---- drawing ---- */

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void draw_text_widget(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	char buf[128];
	const char *str = px_telemetry_text(t, w->source);
	if (str) {
		snprintf(buf, sizeof(buf), *w->format ? w->format : "%s", str);
	} else {
		float v = 0.0f;
		if (!px_telemetry_value(t, w->source, &v)) {
			/* Show the offending name instead of a plausible 0, so a
			 * layout typo is obvious on screen. */
			snprintf(buf, sizeof(buf), "?%s", w->source);
		} else {
			snprintf(buf, sizeof(buf), *w->format ? w->format : "%.0f", v);
		}
	}
	int x = w->x;
	if (w->align) {
		int tw = px_text_width(buf, w->size);
		x -= (w->align == 1) ? tw / 2 : tw;
	}
	px_text(c, x, w->y, buf, w->size, w->color, w->edge);
}

static void draw_bar(const PxWidget *w, const PxCanvas *c, const PxTelemetry *t)
{
	float v = 0.0f;
	px_telemetry_value(t, w->source, &v);
	float frac = (w->max > w->min) ? clamp01((v - w->min) / (w->max - w->min)) : 0.0f;
	int width = w->w > 0 ? w->w : 200;
	int height = w->h > 0 ? w->h : 18;
	if (w->fill != PX_TRANSPARENT)
		px_fill_rect(c, w->x, w->y, w->x + width, w->y + height, w->fill);
	int filled = (int)((float)(width - 2) * frac);
	if (filled > 0)
		px_fill_rect(c, w->x + 1, w->y + 1, w->x + 1 + filled,
			w->y + height - 1, w->color);
	px_rect(c, w->x, w->y, w->x + width, w->y + height, 1, w->edge);
	if (*w->label)
		px_text(c, w->x, w->y - 4, w->label, w->size, w->color, w->edge);
}

static void draw_gauge(const PxWidget *w, const PxCanvas *c, const PxTelemetry *t)
{
	float v = 0.0f;
	px_telemetry_value(t, w->source, &v);
	float frac = (w->max > w->min) ? clamp01((v - w->min) / (w->max - w->min)) : 0.0f;
	int r = w->size > 0 ? w->size : 60;
	/* A 240-degree sweep opening downwards reads like a instrument dial. */
	px_arc(c, w->x, w->y, r, 210.0f, -30.0f, w->edge);
	px_arc(c, w->x, w->y, r - 1, 210.0f, -30.0f, w->edge);
	float a = (210.0f - 240.0f * frac) * (float)M_PI / 180.0f;
	px_line_thick(c, w->x, w->y,
		w->x + (int)lrintf(cosf(a) * (float)(r - 8)),
		w->y - (int)lrintf(sinf(a) * (float)(r - 8)),
		w->thickness > 0 ? w->thickness : 3, w->color);
	px_disc(c, w->x, w->y, 3, w->color);
	if (*w->label) {
		char buf[64];
		snprintf(buf, sizeof(buf), *w->format ? w->format : "%s", w->label);
		int tw = px_text_width(buf, w->size / 3 + 8);
		px_text(c, w->x - tw / 2, w->y + r - 2, buf, w->size / 3 + 8,
			w->color, w->edge);
	}
}

static void draw_horizon(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	int half = (w->w > 0 ? w->w : 800) / 2;
	float a = t->roll_deg * (float)M_PI / 180.0f;
	int dy = (int)lrintf(t->pitch_deg * w->pitch_scale);
	int x0 = w->x - (int)lrintf(cosf(a) * (float)half);
	int y0 = w->y + dy + (int)lrintf(sinf(a) * (float)half);
	int x1 = w->x + (int)lrintf(cosf(a) * (float)half);
	int y1 = w->y + dy - (int)lrintf(sinf(a) * (float)half);
	px_line_thick(c, x0, y0, x1, y1, w->thickness > 0 ? w->thickness : 3,
		w->color);
	/* Fixed aircraft reference, so roll is read against something. */
	px_hline(c, w->x - 60, w->x - 20, w->y, w->edge == PX_TRANSPARENT ? w->color : PX_YELLOW);
	px_hline(c, w->x + 20, w->x + 60, w->y, w->edge == PX_TRANSPARENT ? w->color : PX_YELLOW);
	px_rect(c, w->x - 3, w->y - 3, w->x + 3, w->y + 3, 1, PX_YELLOW);
}

static void draw_arrow(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	float deg = 0.0f;
	px_telemetry_value(t, *w->source ? w->source : "home_bearing", &deg);
	float a = deg * (float)M_PI / 180.0f;
	int r = w->size > 0 ? w->size : 40;
	/* Rotated triangle: a glyph OSD would quantise this to a handful of
	 * fixed arrow sprites. */
	int ax = w->x + (int)lrintf(sinf(a) * (float)r);
	int ay = w->y - (int)lrintf(cosf(a) * (float)r);
	int bx = w->x + (int)lrintf(sinf(a + 2.5f) * (float)r * 0.6f);
	int by = w->y - (int)lrintf(cosf(a + 2.5f) * (float)r * 0.6f);
	int cx = w->x + (int)lrintf(sinf(a - 2.5f) * (float)r * 0.6f);
	int cy = w->y - (int)lrintf(cosf(a - 2.5f) * (float)r * 0.6f);
	px_fill_triangle(c, ax, ay, bx, by, cx, cy, w->color);
	if (w->edge != PX_TRANSPARENT)
		px_triangle(c, ax, ay, bx, by, cx, cy, w->edge);
}

float px_layout_scale_for(const PxLayout *l, const PxCanvas *c)
{
	if (!l->scale_auto)
		return l->scale;
	if (!c || c->h <= 0)
		return 1.0f;
	return (float)c->h / (float)PX_LAYOUT_REF_HEIGHT;
}

/* Geometry only: min/max are data ranges, not lengths. pitch_scale is scaled
 * too, so a given pitch angle keeps displacing the horizon by the same
 * proportion of the frame rather than the same number of pixels. */
static void scale_widget(PxWidget *w, float s)
{
	w->x = (int)lrintf((float)w->x * s);
	w->y = (int)lrintf((float)w->y * s);
	w->w = (int)lrintf((float)w->w * s);
	w->h = (int)lrintf((float)w->h * s);
	w->size = (int)lrintf((float)w->size * s);
	w->thickness = (int)lrintf((float)w->thickness * s);
	if (w->thickness < 1)
		w->thickness = 1;
	w->pitch_scale *= s;
}

void px_layout_draw(const PxLayout *l, const PxCanvas *c, const PxTelemetry *t)
{
	const float s = px_layout_scale_for(l, c);
	for (int i = 0; i < l->count; i++) {
		PxWidget scaled;
		const PxWidget *w = &l->widgets[i];
		/* Scale a copy so the layout keeps its authored numbers and the same
		 * loaded layout can be drawn onto canvases of different sizes. */
		if (s != 1.0f) {
			scaled = *w;
			scale_widget(&scaled, s);
			w = &scaled;
		}
		switch (w->type) {
		case PX_W_TEXT:    draw_text_widget(w, c, t); break;
		case PX_W_BAR:     draw_bar(w, c, t);         break;
		case PX_W_GAUGE:   draw_gauge(w, c, t);       break;
		case PX_W_HORIZON: draw_horizon(w, c, t);     break;
		case PX_W_ARROW:   draw_arrow(w, c, t);       break;
		case PX_W_RECT:
			if (w->fill != PX_TRANSPARENT)
				px_fill_rect(c, w->x, w->y, w->x + w->w,
					w->y + w->h, w->fill);
			if (w->edge != PX_TRANSPARENT)
				px_rect(c, w->x, w->y, w->x + w->w, w->y + w->h,
					w->thickness, w->edge);
			break;
		default:
			break;
		}
	}
}
