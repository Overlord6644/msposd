/* px_layout.c - see px_layout.h. */
#include "px_layout.h"
#include "px_hud.h"
#include "px_icon.h"
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
		/* From the air unit's link daemon - see px_datalink.h. */
		{"dl_lq",       (float)t->dl.lq_pct},
		{"dl_mcs",      (float)t->dl.mcs},
		{"dl_bitrate",  (float)t->dl.bitrate_kbps},
		{"dl_mbps",     t->dl.throughput_mb},
		{"dl_fps",      (float)t->dl.fps},
		{"dl_cpu",      (float)t->dl.cpu_pct},
		{"dl_soc_temp", (float)t->dl.soc_temp_c},
		{"dl_tx_temp",  (float)t->dl.tx_temp_c},
		{"dl_uplink",   (float)t->dl.uplink_pct},
		{"dl_pw",       (float)t->dl.tx_power},
		{"dl_ch",       (float)t->dl.channel},
		{"dl_bw",       (float)t->dl.bandwidth_mhz},
		{"dl_q",        (float)t->dl.pubq},
		{"armed",       (float)t->armed},
		{"cells",       (float)t->cells},
		/* Per-cell voltage is the number a pilot actually flies by: 3.5 means
		 * the same thing on 3S and on 6S, the pack total does not. */
		{"cell_volt",   t->cells > 0 ? t->volt_v / (float)t->cells : t->volt_v},
		{"trip",        t->trip_m},
	};
	for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (strcmp(map[i].n, source) == 0) {
			*out = map[i].v;
			return 1;
		}
	return 0;
}

/* Metres up close, kilometres once metres stop being readable. */
static void fmt_auto_dist(char *buf, size_t n, float m)
{
	if (m >= 1000.0f)
		snprintf(buf, n, "%.2fKM", m / 1000.0f);
	else
		snprintf(buf, n, "%.0fM", m);
}

/* Degrees * 1e7 to "46.1234567". Done on the raw integer: a float dropped the
 * 7th decimal before it ever reached the formatter. */
static void fmt_coord(char *buf, size_t n, int32_t e7)
{
	const char *sign = e7 < 0 ? "-" : "";
	uint32_t v = e7 < 0 ? (uint32_t)(-(int64_t)e7) : (uint32_t)e7;
	snprintf(buf, n, "%s%u.%07u", sign, v / 10000000u, v % 10000000u);
}

const char *px_telemetry_text(const PxTelemetry *t, const char *source)
{
	if (!t || !source)
		return NULL;
	if (strcmp(source, "mode") == 0)
		return t->mode;
	if (strcmp(source, "msg") == 0)
		return t->msg;
	if (strcmp(source, "armed") == 0)
		return t->armed ? "ARMED" : "DISARMED";
	/* Static buffers, one per source: the draw pass and the cache signature
	 * both call this within a frame, and different sources must not share. */
	if (strcmp(source, "home_auto") == 0) {
		static char buf[16];
		fmt_auto_dist(buf, sizeof(buf), t->home_dist_m);
		return buf;
	}
	if (strcmp(source, "trip_auto") == 0) {
		static char buf[16];
		fmt_auto_dist(buf, sizeof(buf), t->trip_m);
		return buf;
	}
	if (strcmp(source, "lat") == 0) {
		static char buf[20];
		fmt_coord(buf, sizeof(buf), t->lat_e7);
		return buf;
	}
	if (strcmp(source, "lon") == 0) {
		static char buf[20];
		fmt_coord(buf, sizeof(buf), t->lon_e7);
		return buf;
	}
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
	if (!strcmp(s, "compass"))   return PX_W_COMPASS;
	if (!strcmp(s, "tape"))      return PX_W_TAPE;
	if (!strcmp(s, "ladder"))    return PX_W_LADDER;
	if (!strcmp(s, "crosshair")) return PX_W_CROSSHAIR;
	if (!strcmp(s, "vario"))     return PX_W_VARIO;
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
	w->span = 0.0f;   /* 0 = each instrument's own sensible default */
	w->step = 0.0f;
	w->accent = PX_YELLOW;
	w->z = 0;
	w->layer = 0;
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
	/* Layer 0 only, so a layout that never mentions layers behaves
	 * exactly as it did before this existed. */
	l->layer_mask = 0x01;
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
			} else if (!strcmp(key, "layers")) {
				/* Which layers are visible at boot, e.g. "0" or "0,2". */
				unsigned m = 0;
				for (const char *p = val; *p; p++)
					if (*p >= '0' && *p <= '7')
						m |= 1u << (*p - '0');
				if (m)
					l->layer_mask = m;
				else
					fprintf(stderr, "[px_layout] layers '%s' names none,"
						" keeping 0x%02x\n", val, l->layer_mask);
			} else if (!strcmp(key, "layer_channel")) {
				int ch = atoi(val);
				if (ch >= 0 && ch <= 16)
					l->layer_channel = ch;
				else
					fprintf(stderr, "[px_layout] layer_channel %s out of"
						" range (0..16)\n", val);
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
	else if (!strcmp(key, "span"))         w->span = strtof(val, NULL);
	else if (!strcmp(key, "step"))         w->step = strtof(val, NULL);
	else if (!strcmp(key, "accent"))       w->accent = parse_color(val, PX_YELLOW);
	else if (!strcmp(key, "z"))            w->z = atoi(val);
	else if (!strcmp(key, "layer")) {
		int ly = atoi(val);
		if (ly >= 0 && ly <= 7)
			w->layer = ly;
		else
			fprintf(stderr, "[px_layout] '%s': layer %s out of range"
				" (0..7)\n", w->name, val);
	}
		else if (!strcmp(key, "icon")) {
			w->icon = (uint8_t)px_icon_parse(val);
			if (w->icon == PX_ICON_NONE && strcmp(val, "none"))
				fprintf(stderr, "[px_layout] '%s': unknown icon '%s'\n",
					w->name, val);
		}
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
	if (!*w->source) {
		/* No source: a static label (units, captions), format is the text. */
		snprintf(buf, sizeof(buf), "%s", *w->format ? w->format : w->name);
	} else if (str) {
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
	/* The icon is part of the widget's width: alignment moves icon and text
	 * as one block, or a centred readout would sit off-centre by half an
	 * icon. */
	int iw = w->icon ? px_icon_width(w->size, (PxIconKind)w->icon) + w->size / 4
			 : 0;
	int x = w->x;
	if (w->align) {
		int tw = px_text_width(buf, w->size) + iw;
		x -= (w->align == 1) ? tw / 2 : tw;
	}
	if (w->icon)
		/* Optically centred on the cap band, not sat on the baseline: caps
		 * are 0.70 of the size (see px_text.c), so a baseline-flush icon
		 * sticks up past the text by nearly a third of its height. */
		px_icon(c, x, w->y - (17 * w->size) / 20, w->size,
			(PxIconKind)w->icon, w->color, w->edge);
	px_text(c, x + iw, w->y, buf, w->size, w->color, w->edge);
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

/* Needle tip in pixels. Shared so the cache signature and the drawing cannot
 * disagree about where the needle is - the whole failure mode this guards. */
static void gauge_needle(const PxWidget *w, const PxTelemetry *t, int *nx, int *ny)
{
	float v = 0.0f;
	px_telemetry_value(t, w->source, &v);
	float frac = (w->max > w->min) ? clamp01((v - w->min) / (w->max - w->min)) : 0.0f;
	int r = w->size > 0 ? w->size : 60;
	float a = (210.0f - 240.0f * frac) * (float)M_PI / 180.0f;
	*nx = w->x + (int)lrintf(cosf(a) * (float)(r - 8));
	*ny = w->y - (int)lrintf(sinf(a) * (float)(r - 8));
}

static void draw_gauge(const PxWidget *w, const PxCanvas *c, const PxTelemetry *t)
{
	int r = w->size > 0 ? w->size : 60;
	/* A 240-degree sweep opening downwards reads like an instrument dial. */
	px_arc(c, w->x, w->y, r, 210.0f, -30.0f, w->edge);
	px_arc(c, w->x, w->y, r - 1, 210.0f, -30.0f, w->edge);
	int nx, ny;
	gauge_needle(w, t, &nx, &ny);
	px_line_thick(c, w->x, w->y, nx, ny,
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

/* Horizon line endpoints in pixels - see gauge_needle for why this is shared. */
static void horizon_ends(const PxWidget *w, const PxTelemetry *t, int *x0,
	int *y0, int *x1, int *y1)
{
	int half = (w->w > 0 ? w->w : 800) / 2;
	float a = t->roll_deg * (float)M_PI / 180.0f;
	int dy = (int)lrintf(t->pitch_deg * w->pitch_scale);
	*x0 = w->x - (int)lrintf(cosf(a) * (float)half);
	*y0 = w->y + dy + (int)lrintf(sinf(a) * (float)half);
	*x1 = w->x + (int)lrintf(cosf(a) * (float)half);
	*y1 = w->y + dy - (int)lrintf(sinf(a) * (float)half);
}

static void draw_horizon(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	int x0, y0, x1, y1;
	horizon_ends(w, t, &x0, &y0, &x1, &y1);
	px_line_thick(c, x0, y0, x1, y1, w->thickness > 0 ? w->thickness : 3,
		w->color);
	/* Fixed aircraft reference, so roll is read against something. */
	px_hline(c, w->x - 60, w->x - 20, w->y, w->edge == PX_TRANSPARENT ? w->color : PX_YELLOW);
	px_hline(c, w->x + 20, w->x + 60, w->y, w->edge == PX_TRANSPARENT ? w->color : PX_YELLOW);
	px_rect(c, w->x - 3, w->y - 3, w->x + 3, w->y + 3, 1, PX_YELLOW);
}

/* Arrow vertices in pixels - see gauge_needle for why this is shared.
 * A proper arrow - shaft plus head - not a bare triangle: rotated a few
 * degrees, a triangle is an ambiguous wedge; a shaft says unambiguously
 * where the tail is. p = tip, head-left, head-right, tail (8 ints). */
static void arrow_pts(const PxWidget *w, const PxTelemetry *t, int *p)
{
	float deg = 0.0f;
	px_telemetry_value(t, *w->source ? w->source : "home_bearing", &deg);
	float a = deg * (float)M_PI / 180.0f;
	float sa = sinf(a), ca = cosf(a);
	int r = w->size > 0 ? w->size : 40;
	/* rot(px,py): widget-local (x right, y up), 0 deg = straight up */
#define ROT_X(px, py) (w->x + (int)lrintf(((float)(px)) * ca + ((float)(py)) * sa))
#define ROT_Y(px, py) (w->y + (int)lrintf(((float)(px)) * sa - ((float)(py)) * ca))
	p[0] = ROT_X(0, r);              p[1] = ROT_Y(0, r);              /* tip  */
	p[2] = ROT_X(-r / 2, r / 4);     p[3] = ROT_Y(-r / 2, r / 4);     /* head */
	p[4] = ROT_X(r / 2, r / 4);      p[5] = ROT_Y(r / 2, r / 4);
	p[6] = ROT_X(0, -r);             p[7] = ROT_Y(0, -r);             /* tail */
#undef ROT_X
#undef ROT_Y
}

static void draw_compass(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	float v = t->yaw_deg;
	if (*w->source)
		px_telemetry_value(t, w->source, &v);
	px_hud_compass(c, w->x, w->y, w->w > 0 ? w->w : 520, v,
		w->span > 0.0f ? w->span : 90.0f,
		w->step > 0.0f ? w->step : 15.0f,
		w->size > 0 ? w->size : 22, w->color, w->accent, w->edge);
}

static void draw_tape(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	float v = 0.0f;
	px_telemetry_value(t, w->source, &v);
	px_hud_tape(c, w->x, w->y, w->h > 0 ? w->h : 300, v,
		w->span > 0.0f ? w->span : 40.0f,
		w->step > 0.0f ? w->step : 5.0f,
		w->align, w->size > 0 ? w->size : 20, w->color, w->accent, w->edge);
}

static void draw_ladder(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	px_hud_ladder(c, w->x, w->y, w->w > 0 ? w->w : 420,
		w->h > 0 ? w->h : 340, t->roll_deg,
		t->pitch_deg, w->pitch_scale > 0.0f ? w->pitch_scale : 8.0f,
		w->step > 0.0f ? w->step : 10.0f,
		w->size > 0 ? w->size : 20, w->color, w->accent, w->edge);
}

static void draw_arrow(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	/* Shaft-and-head arrow, rotated continuously: a glyph OSD would
	 * quantise this to a handful of fixed arrow sprites. */
	int p[8];
	arrow_pts(w, t, p);
	int th = w->thickness > 0 ? w->thickness : 2;
	if (w->edge != PX_TRANSPARENT)
		px_line_thick(c, p[6] + 1, p[7] + 1, p[0] + 1, p[1] + 1, th + 2,
			w->edge);
	px_line_thick(c, p[6], p[7], p[0], p[1], th, w->color);
	px_fill_triangle(c, p[0], p[1], p[2], p[3], p[4], p[5], w->color);
	if (w->edge != PX_TRANSPARENT)
		px_triangle(c, p[0], p[1], p[2], p[3], p[4], p[5], w->edge);
}

/* Which vario arrows show: +1 climb (green up only), -1 descend (red down
 * only), 0 level (both, dimmed by nothing - the pair IS the "level" symbol).
 * Shared by draw and signature. The 0.15 m/s band swallows baro noise, which
 * would otherwise blink the arrows on a parked aircraft. */
static int vario_state(const PxWidget *w, const PxTelemetry *t, float *out_v)
{
	float v = 0.0f;
	px_telemetry_value(t, *w->source ? w->source : "vspd", &v);
	if (out_v)
		*out_v = v;
	return (v > 0.15f) ? 1 : (v < -0.15f ? -1 : 0);
}

static void draw_vario(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	float v = 0.0f;
	int st = vario_state(w, t, &v);
	int s = w->size > 0 ? w->size : 22;
	int aw = (s * 2) / 3;        /* arrow half-width  */
	int ah = (s * 5) / 8;        /* arrow height      */
	int cx = w->x + aw;          /* arrows' centre column */
	int gap = 3;

	/* Stacked like the Betaflight glyph: up above, down below. Climbing
	 * hides the down arrow, descending hides the up one - the symbol reads
	 * before the sign of the number does. */
	if (st >= 0) {
		int base = w->y - gap / 2 - (st > 0 ? -gap / 2 : 0);
		px_fill_triangle(c, cx, base - ah, cx - aw, base, cx + aw, base,
			PX_GREEN);
		if (w->edge != PX_TRANSPARENT)
			px_triangle(c, cx, base - ah, cx - aw, base, cx + aw, base,
				w->edge);
	}
	if (st <= 0) {
		int top = w->y + gap / 2 + (st < 0 ? -gap / 2 : 0);
		px_fill_triangle(c, cx, top + ah, cx - aw, top, cx + aw, top,
			PX_RED);
		if (w->edge != PX_TRANSPARENT)
			px_triangle(c, cx, top + ah, cx - aw, top, cx + aw, top,
				w->edge);
	}

	char buf[24];
	snprintf(buf, sizeof(buf), *w->format ? w->format : "%.1fM/S",
		v < 0 ? -v : v); /* the arrow carries the sign */
	px_text(c, w->x + 2 * aw + 8, w->y + s / 3, buf, s, w->color, w->edge);
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

static void draw_widget(const PxWidget *w, const PxCanvas *c,
	const PxTelemetry *t)
{
	switch (w->type) {
	case PX_W_TEXT:    draw_text_widget(w, c, t); break;
	case PX_W_BAR:     draw_bar(w, c, t);         break;
	case PX_W_GAUGE:   draw_gauge(w, c, t);       break;
	case PX_W_HORIZON: draw_horizon(w, c, t);     break;
	case PX_W_ARROW:   draw_arrow(w, c, t);       break;
	case PX_W_COMPASS: draw_compass(w, c, t);     break;
	case PX_W_TAPE:    draw_tape(w, c, t);        break;
	case PX_W_LADDER:  draw_ladder(w, c, t);      break;
	case PX_W_CROSSHAIR:
		px_hud_crosshair(c, w->x, w->y, w->size > 0 ? w->size : 28,
			w->color, w->edge);
		break;
	case PX_W_VARIO:   draw_vario(w, c, t);       break;
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

/* Union of two dirty boxes, so a frame can clear one region instead of many. */
static void region_add(PxDirty *r, const PxDirty *b)
{
	if (b->x1 < b->x0)
		return; /* empty */
	if (r->x1 < r->x0) {
		*r = *b;
		return;
	}
	if (b->x0 < r->x0) r->x0 = b->x0;
	if (b->y0 < r->y0) r->y0 = b->y0;
	if (b->x1 > r->x1) r->x1 = b->x1;
	if (b->y1 > r->y1) r->y1 = b->y1;
}

static int box_overlaps(const PxDirty *b, int x0, int y0, int x1, int y1)
{
	if (b->x1 < b->x0)
		return 0;
	return !(b->x1 < x0 || b->x0 > x1 || b->y1 < y0 || b->y0 > y1);
}

/* Draw order: by depth, stable, so widgets at the same z keep file order. Both
 * paths use this, because they must agree about what is in front of what. */
static void draw_order(const PxLayout *l, int *idx)
{
	int n = 0;
	for (int i = 0; i < l->count; i++)
		idx[n++] = i;
	/* Insertion sort: n is at most PX_LAYOUT_MAX_WIDGETS and stability is the
	 * property we need, not speed. */
	for (int i = 1; i < n; i++) {
		int cur = idx[i], j = i - 1;
		while (j >= 0 && l->widgets[idx[j]].z > l->widgets[cur].z) {
			idx[j + 1] = idx[j];
			j--;
		}
		idx[j + 1] = cur;
	}
}

/* Scaled copy of widget i, so both draw paths share one definition of geometry. */
static const PxWidget *widget_scaled(const PxLayout *l, int i, float s,
	PxWidget *tmp)
{
	const PxWidget *w = &l->widgets[i];
	if (s == 1.0f)
		return w;
	*tmp = *w;
	scale_widget(tmp, s);
	return tmp;
}

void px_layout_draw(const PxLayout *l, const PxCanvas *c, const PxTelemetry *t)
{
	const float s = px_layout_scale_for(l, c);
	int order[PX_LAYOUT_MAX_WIDGETS];
	draw_order(l, order);
	for (int k = 0; k < l->count; k++) {
		int i = order[k];
		if (!(l->layer_mask & (1u << l->widgets[i].layer)))
			continue;
		PxWidget tmp;
		draw_widget(widget_scaled(l, i, s, &tmp), c, t);
	}
}

/* ---- incremental drawing ---- */

void px_layout_cache_reset(PxLayoutCache *cache)
{
	memset(cache, 0, sizeof(*cache));
	for (int i = 0; i < PX_LAYOUT_MAX_WIDGETS; i++)
		px_dirty_reset(&cache->box[i]);
	cache->primed = 0;
}

static uint32_t fnv1a(uint32_t h, const void *p, size_t n)
{
	const uint8_t *b = p;
	while (n--) {
		h ^= *b++;
		h *= 16777619u;
	}
	return h;
}

/*
 * A signature of what this widget would draw. Two frames with equal signatures
 * are pixel-identical, so the second can be skipped.
 *
 * It hashes the integer PIXEL geometry the draw functions use - endpoints,
 * vertices, filled pixel counts - not the telemetry value. Quantising the value
 * independently was wrong and measurably so: a roll change too small to move a
 * 0.25-degree bucket still moved the horizon line by a pixel, the signature said
 * "unchanged", and the old pixels stayed on screen. Hashing what is drawn makes
 * signature and pixels agree by construction, and still skips the frames where
 * a value moves less than one pixel.
 */
static uint32_t widget_sig(const PxWidget *w, const PxTelemetry *t)
{
	uint32_t h = 2166136261u;
	h = fnv1a(h, &w->type, sizeof(w->type));
	h = fnv1a(h, &w->x, sizeof(w->x));
	h = fnv1a(h, &w->y, sizeof(w->y));

	switch (w->type) {
	case PX_W_TEXT: {
		/* The rendered string is the ground truth: two different values that
		 * format identically genuinely draw the same pixels. */
		char buf[128];
		const char *str = px_telemetry_text(t, w->source);
		if (!*w->source)
			snprintf(buf, sizeof(buf), "%s", *w->format ? w->format : w->name);
		else if (str)
			snprintf(buf, sizeof(buf), *w->format ? w->format : "%s", str);
		else {
			float v = 0.0f;
			if (!px_telemetry_value(t, w->source, &v))
				snprintf(buf, sizeof(buf), "?%s", w->source);
			else
				snprintf(buf, sizeof(buf), *w->format ? w->format : "%.0f", v);
		}
		return fnv1a(h, buf, strlen(buf));
	}
	case PX_W_BAR: {
		float v = 0.0f;
		px_telemetry_value(t, w->source, &v);
		float frac = (w->max > w->min) ? clamp01((v - w->min) / (w->max - w->min)) : 0.0f;
		int width = w->w > 0 ? w->w : 200;
		int filled = (int)((float)(width - 2) * frac); /* whole pixels */
		return fnv1a(h, &filled, sizeof(filled));
	}
	case PX_W_GAUGE: {
		int nx, ny;
		gauge_needle(w, t, &nx, &ny);
		h = fnv1a(h, &nx, sizeof(nx));
		return fnv1a(h, &ny, sizeof(ny));
	}
	case PX_W_HORIZON: {
		int e[4];
		horizon_ends(w, t, &e[0], &e[1], &e[2], &e[3]);
		return fnv1a(h, e, sizeof(e));
	}
	case PX_W_ARROW: {
		int p[8];
		arrow_pts(w, t, p);
		return fnv1a(h, p, sizeof(p));
	}
	case PX_W_COMPASS: {
		float v = t->yaw_deg;
		if (*w->source)
			px_telemetry_value(t, w->source, &v);
		float span = w->span > 0.0f ? w->span : 90.0f;
		int q = (int)lrintf(v * ((float)(w->w > 0 ? w->w : 520) / span));
		return fnv1a(h, &q, sizeof(q)); /* pixels of ribbon travel */
	}
	case PX_W_TAPE: {
		float v = 0.0f;
		px_telemetry_value(t, w->source, &v);
		float span = w->span > 0.0f ? w->span : 40.0f;
		int q = (int)lrintf(v * ((float)(w->h > 0 ? w->h : 300) / span));
		return fnv1a(h, &q, sizeof(q)); /* pixels of tape travel */
	}
	case PX_W_LADDER: {
		float pd = w->pitch_scale > 0.0f ? w->pitch_scale : 8.0f;
		int qr = (int)lrintf(t->roll_deg * 8.0f);      /* eighth of a degree */
		int qp = (int)lrintf(t->pitch_deg * pd);       /* whole pixels */
		h = fnv1a(h, &qr, sizeof(qr));
		return fnv1a(h, &qp, sizeof(qp));
	}
	case PX_W_VARIO: {
		float v = 0.0f;
		int st = vario_state(w, t, &v);
		char buf[24];
		snprintf(buf, sizeof(buf), *w->format ? w->format : "%.1fM/S",
			v < 0 ? -v : v);
		h = fnv1a(h, &st, sizeof(st));
		return fnv1a(h, buf, strlen(buf));
	}
	default:
		return h; /* static: signature never changes, drawn once */
	}
}

int px_layout_draw_cached(const PxLayout *l, const PxCanvas *c,
	const PxTelemetry *t, PxLayoutCache *cache)
{
	return px_layout_draw_cached_ex(l, c, t, cache, NULL);
}

int px_layout_draw_cached_ex(const PxLayout *l, const PxCanvas *c,
	const PxTelemetry *t, PxLayoutCache *cache, const PxDirty *extra)
{
	const float s = px_layout_scale_for(l, c);
	int order[PX_LAYOUT_MAX_WIDGETS];
	draw_order(l, order);

	PxWidget scaled[PX_LAYOUT_MAX_WIDGETS];
	uint32_t sig[PX_LAYOUT_MAX_WIDGETS];
	int visible[PX_LAYOUT_MAX_WIDGETS];
	int changed[PX_LAYOUT_MAX_WIDGETS];

	/* Pass 1: what would each widget draw, and has that changed? */
	PxDirty region;
	px_dirty_reset(&region);
	int any = 0;
	for (int i = 0; i < l->count; i++) {
		visible[i] = (l->layer_mask & (1u << l->widgets[i].layer)) != 0;
		changed[i] = 0;
		if (!visible[i]) {
			/* Just hidden: its pixels have to go. */
			if (cache->sig[i] != 0 || cache->box[i].x1 >= cache->box[i].x0) {
				region_add(&region, &cache->box[i]);
				cache->sig[i] = 0;
				px_dirty_reset(&cache->box[i]);
				any = 1;
			}
			continue;
		}
		scaled[i] = l->widgets[i];
		if (s != 1.0f)
			scale_widget(&scaled[i], s);
		sig[i] = widget_sig(&scaled[i], t);
		if (!cache->primed || sig[i] != cache->sig[i]) {
			changed[i] = 1;
			any = 1;
			/* Where it was: that area must be cleared whether or not the widget
			 * lands in the same place this time. */
			region_add(&region, &cache->box[i]);
		}
	}
	/* Caller-drawn content (detection boxes): its old pixels must be cleared
	 * and its new area repaired exactly like a moved widget's. */
	if (extra && extra->x1 >= extra->x0) {
		region_add(&region, extra);
		any = 1;
	}
	if (!any) {
		cache->primed = 1;
		return 0;
	}

	/* Clear once, over the union. Per-widget clears cannot work when widgets
	 * overlap: erasing the box of something behind would take a bite out of
	 * whatever is drawn on top of it, and that widget - unchanged - would never
	 * be redrawn to repair it. */
	int rx0, ry0, rx1, ry1;
	if (px_dirty_box(&region, &rx0, &ry0, &rx1, &ry1))
		px_clear_rect(c, rx0, ry0, rx1, ry1);

	/* Pass 2: redraw, in depth order, everything that changed or that the clear
	 * touched. Depth order matters: repairing an overlap only comes out right if
	 * the widget in front is drawn after the one behind. */
	PxDirty track;
	PxCanvas tc = *c;
	tc.dirty = &track;
	int redrawn = 0;
	for (int k = 0; k < l->count; k++) {
		int i = order[k];
		if (!visible[i])
			continue;
		int touched = changed[i] ||
			(px_dirty_box(&region, &rx0, &ry0, &rx1, &ry1) &&
			 box_overlaps(&cache->box[i], rx0, ry0, rx1, ry1));
		if (!touched)
			continue;
		px_dirty_reset(&track);
		draw_widget(&scaled[i], &tc, t);
		cache->box[i] = track;
		cache->sig[i] = sig[i];
		redrawn++;
	}
	cache->primed = 1;
	return redrawn;
}

void px_layout_set_layers(PxLayout *l, unsigned mask)
{
	if (!l || !mask)
		return; /* refusing 0: blanking the OSD is never the intent */
	l->layer_mask = mask;
}

int px_layout_apply_rc(PxLayout *l, int channel_us)
{
	if (!l || l->layer_channel <= 0)
		return 0;
	/* Three-position switch. Thresholds sit well inside each band so a jittering
	 * channel cannot flap between layers. */
	unsigned want;
	if (channel_us < 1300)
		want = 0x01;
	else if (channel_us < 1700)
		want = 0x02;
	else
		want = 0x04;
	if (want == l->layer_mask)
		return 0;
	l->layer_mask = want;
	printf("[px_layout] layers -> 0x%02x (channel %d us)\n", want, channel_us);
	return 1;
}
