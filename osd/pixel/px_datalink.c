/* px_datalink.c - see px_datalink.h. */
#include "px_datalink.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* First signed number at or after `p`, or 0 if there is none before the line
 * ends. `ok` reports whether a digit was actually found, so a caller can tell
 * "the field said zero" from "the field was not there". */
static float number_at(const char *p, int *ok)
{
	*ok = 0;
	if (!p)
		return 0.0f;
	while (*p && !isdigit((unsigned char)*p) && *p != '-')
		p++;
	if (!*p)
		return 0.0f;
	char *end = NULL;
	float v = strtof(p, &end);
	if (end == p)
		return 0.0f;
	*ok = 1;
	return v;
}

/* Number that appears immediately BEFORE a unit, as in "52000kbps" or "3.3Mb":
 * walk back over the digits and the decimal point, then parse forward. */
static float number_before(const char *line, const char *unit, int *ok)
{
	*ok = 0;
	const char *u = strstr(line, unit);
	if (!u || u == line)
		return 0.0f;
	const char *s = u;
	while (s > line && (isdigit((unsigned char)s[-1]) || s[-1] == '.'))
		s--;
	if (s == u)
		return 0.0f; /* the unit was there but no number in front of it */
	return number_at(s, ok);
}

static const char *find_key(const char *line, const char *key)
{
	const char *p = strstr(line, key);
	return p ? p + strlen(key) : NULL;
}

void px_datalink_parse(PxDatalink *dl, const char *line)
{
	if (!dl || !line || !*line)
		return;

	int ok;
	float v;

	v = number_at(find_key(line, "LQ:"), &ok);
	if (ok) { dl->lq_pct = (int)v; dl->have = 1; }

	v = number_at(find_key(line, "MCS:"), &ok);
	if (ok) { dl->mcs = (int)v; dl->have = 1; }

	v = number_before(line, "kbps", &ok);
	if (ok) { dl->bitrate_kbps = (int)v; dl->have = 1; }

	v = number_before(line, "Mb", &ok);
	if (ok) { dl->throughput_mb = v; dl->have = 1; }

	v = number_at(find_key(line, "FPS:"), &ok);
	if (ok) { dl->fps = (int)v; dl->have = 1; }

	/* "CPU:24%,73c" carries two values; take the load, then the temperature
	 * after the comma - looking for the next number after "CPU:" alone would
	 * find only the load and never the temperature. */
	const char *cpu = find_key(line, "CPU:");
	if (cpu) {
		v = number_at(cpu, &ok);
		if (ok) { dl->cpu_pct = (int)v; dl->have = 1; }
		const char *comma = strchr(cpu, ',');
		if (comma) {
			v = number_at(comma, &ok);
			if (ok) { dl->soc_temp_c = (int)v; dl->have = 1; }
		}
	}

	v = number_at(find_key(line, "TX:"), &ok);
	if (ok) { dl->tx_temp_c = (int)v; dl->have = 1; }

	/* "[#########......] [66%] uplink": take the number in the SECOND bracket
	 * pair, since the first contains the bar itself and no digits. */
	const char *up = strstr(line, "uplink");
	if (up) {
		const char *pct = NULL;
		for (const char *p = line; p < up; p++)
			if (*p == '[')
				pct = p;          /* last '[' before the word */
		if (pct) {
			v = number_at(pct, &ok);
			if (ok) { dl->uplink_pct = (int)v; dl->have = 1; }
		}
	}

	v = number_at(find_key(line, "pw:"), &ok);
	if (ok) { dl->tx_power = (int)v; dl->have = 1; }

	/* "Ch:165-20mhz" is two numbers in one token. */
	const char *ch = find_key(line, "Ch:");
	if (ch) {
		v = number_at(ch, &ok);
		if (ok) { dl->channel = (int)v; dl->have = 1; }
		const char *dash = strchr(ch, '-');
		if (dash) {
			v = number_at(dash, &ok);
			if (ok) { dl->bandwidth_mhz = (int)v; dl->have = 1; }
		}
	}

	v = number_at(find_key(line, "q:"), &ok);
	if (ok) { dl->pubq = (int)v; dl->have = 1; }
}
