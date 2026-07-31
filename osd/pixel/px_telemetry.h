/*
 * px_telemetry.h - the values a pixel widget can display.
 *
 * This struct is the seam between "where the numbers come from" and "how they
 * are drawn". Widgets only ever read it, so the source can be MSP telemetry
 * polled from a Betaflight FC, MAVLink from INAV/ArduPilot, or a fixture in the
 * host preview - without touching layout or rendering.
 *
 * Deliberately values, not a character grid. Reading MSP DisplayPort would give
 * us cells of text already laid out by the flight controller, which is exactly
 * the constraint a pixel OSD exists to escape.
 */
#ifndef PX_TELEMETRY_H
#define PX_TELEMETRY_H

#include <stdint.h>

#include "px_datalink.h"

typedef struct {
	/* Attitude, degrees. Roll positive right, pitch positive nose-up. */
	float roll_deg;
	float pitch_deg;
	float yaw_deg;   /* heading, 0..360 */

	/* Battery */
	float volt_v;
	float curr_a;
	int   mah_used;
	int   batt_pct;
	int   cells;      /* 0 = unknown */

	/* Flight */
	float alt_m;      /* relative altitude */
	float spd_kph;    /* ground speed */
	float vspd_ms;    /* vertical speed */
	int   throttle_pct;
	/* Average of the motors' eRPM/pole-pairs, from MSP_MOTOR_TELEMETRY.
	 * Needs bidirectional DShot on the FC; stays 0 without it. */
	float rpm_avg;

	/* Navigation */
	int   sats;
	float trip_m;     /* ground distance travelled since boot */
	float home_dist_m;
	float home_bearing_deg; /* relative to nose, 0 = ahead */
	/* Wire format, degrees * 1e7: a float rounds the 7th decimal away, and
	 * the OSD prints coordinates, it does not compute with them. */
	int32_t lat_e7;
	int32_t lon_e7;

	/* Link. rssi/lq come from the flight controller; `dl' comes from the
	 * air unit's own adaptive-link daemon, which knows things the FC cannot
	 * (modulation index, air throughput, radio temperature). */
	int   rssi_pct;
	int   lq_pct;
	PxDatalink dl;

	/* Status */
	int  armed;
	char mode[16];    /* flight mode name from the FC */
	char msg[64];     /* transient warning/status line */

	/* Freshness: widgets can grey out when telemetry stops. */
	uint64_t updated_ms;
	int      valid;
} PxTelemetry;

/* Resolve a source name from the layout ("alt", "volt", "rssi", ...) to a
 * number. Returns 0 and leaves *out untouched when the name is unknown, so a
 * typo in a layout is visible rather than silently rendering zero. */
int px_telemetry_value(const PxTelemetry *t, const char *source, float *out);

/* Same for text sources ("mode", "msg"). Returns NULL when unknown. */
const char *px_telemetry_text(const PxTelemetry *t, const char *source);

#endif /* PX_TELEMETRY_H */
