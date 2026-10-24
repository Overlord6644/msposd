/*
 * mav_osd.c - feed the pixel OSD from MAVLink (-M) instead of MSP.
 *
 * Included into msposd.c right after osd.c, like the rest of this program, so
 * it writes the same last_* telemetry the MSP handlers write and px_osd_fill()
 * does not care which protocol the numbers came from.
 *
 * MAVLink is pushed by the flight controller rather than polled, so instead of
 * MSP requests we ask for the messages we need with MAV_CMD_SET_MESSAGE_INTERVAL.
 * That keeps the FC's SRx_* stream parameters out of it: the OSD gets its rates
 * whatever the port's defaults are. The request is repeated every few seconds,
 * which also covers a flight controller that rebooted under us.
 */

#include <math.h>

/* What we ask for, and how often (microseconds between messages). */
static const struct {
	uint16_t msgid;
	int32_t interval_us;
} mav_osd_streams[] = {
	{MAVLINK_MSG_ID_ATTITUDE, 50000},             /* 20 Hz: the horizon */
	{MAVLINK_MSG_ID_VFR_HUD, 100000},             /* speed, climb, throttle */
	{MAVLINK_MSG_ID_GLOBAL_POSITION_INT, 200000}, /* position, relative alt */
	{MAVLINK_MSG_ID_GPS_RAW_INT, 500000},         /* satellites */
	{MAVLINK_MSG_ID_SYS_STATUS, 500000},          /* voltage, current, % */
	{MAVLINK_MSG_ID_BATTERY_STATUS, 1000000},     /* mAh used */
	{MAVLINK_MSG_ID_RC_CHANNELS, 500000},         /* RSSI */
	{MAVLINK_MSG_ID_HOME_POSITION, 2000000},      /* home, for distance/bearing */
};

#define MAV_OSD_SYSID 191 /* onboard companion range; the FC accepts any */
#define MAV_OSD_REQUEST_PERIOD_MS 5000

static uint8_t mav_fc_sysid = 0;   /* 0 until the FC's first heartbeat */
static uint8_t mav_fc_compid = 0;
static uint8_t mav_fc_type = 0;    /* MAV_TYPE_* */
static uint64_t mav_last_request_ms = 0;

static int32_t mav_home_lat_e7 = 0, mav_home_lon_e7 = 0;
static int mav_home_valid = 0;

static void mav_osd_dump(void);

/* ArduPilot custom_mode -> the short names ArduPilot itself uses on its OSD. */
static const char *mav_plane_mode(uint32_t m)
{
	static const char *const names[] = {
		"MANU", "CIRC", "STAB", "TRAN", "ACRO", "FBWA", "FBWB", "CRUS",
		"ATUN", NULL,   "AUTO", "RTL",  "LOIT", "TKOF", "AVOI", "GUID",
		"INIT", "QSTB", "QHOV", "QLOT", "QLND", "QRTL", "QATN", "QACO",
		"THML", "L2QL", "ALND",
	};
	return m < sizeof(names) / sizeof(names[0]) ? names[m] : NULL;
}

static const char *mav_copter_mode(uint32_t m)
{
	static const char *const names[] = {
		"STAB", "ACRO", "ALTH", "AUTO", "GUID", "LOIT", "RTL",  "CIRC",
		NULL,   "LAND", NULL,   "DRIF", NULL,   "SPRT", "FLIP", "ATUN",
		"PHLD", "BRAK", "THRW", "AVOI", "GNGP", "SRTL", "FHLD", "FOLL",
		"ZIGZ", "SYSI", "AROT", "AUTO", "TRTL",
	};
	return m < sizeof(names) / sizeof(names[0]) ? names[m] : NULL;
}

static int mav_is_plane(uint8_t type)
{
	switch (type) {
	case MAV_TYPE_FIXED_WING:
	case MAV_TYPE_VTOL_TAILSITTER_DUOROTOR:
	case MAV_TYPE_VTOL_TAILSITTER_QUADROTOR:
	case MAV_TYPE_VTOL_TILTROTOR:
	case MAV_TYPE_VTOL_FIXEDROTOR:
	case MAV_TYPE_VTOL_TAILSITTER:
	case MAV_TYPE_VTOL_TILTWING:
		return 1;
	default:
		return 0;
	}
}

static void mav_osd_heartbeat(const mavlink_message_t *msg)
{
	mavlink_heartbeat_t hb;
	mavlink_msg_heartbeat_decode(msg, &hb);
	/* Only the autopilot's heartbeat: a GCS or another component on the
	 * same link must not set our mode. */
	if (hb.autopilot == MAV_AUTOPILOT_INVALID || hb.type == MAV_TYPE_GCS)
		return;
	mav_fc_sysid = msg->sysid;
	mav_fc_compid = msg->compid;
	mav_fc_type = hb.type;
	armed = (hb.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) != 0;

	const char *name = NULL;
	if (hb.autopilot == MAV_AUTOPILOT_ARDUPILOTMEGA) {
		memcpy(current_fc_identifier, "ARDU", 4);
		name = mav_is_plane(hb.type) ? mav_plane_mode(hb.custom_mode)
					     : mav_copter_mode(hb.custom_mode);
	}
	if (name)
		snprintf(last_mav_mode, sizeof(last_mav_mode), "%s", name);
	else
		snprintf(last_mav_mode, sizeof(last_mav_mode), "M%u",
			 (unsigned)hb.custom_mode);
}

/* Equirectangular is plenty at OSD ranges and avoids the haversine trig. */
static void mav_osd_update_home(void)
{
	if (!mav_home_valid || (last_lat_e7 == 0 && last_lon_e7 == 0))
		return;
	const double deg = 1e-7 * M_PI / 180.0;
	double lat1 = last_lat_e7 * deg, lat2 = mav_home_lat_e7 * deg;
	double dlat = lat2 - lat1;
	double dlon = (mav_home_lon_e7 - last_lon_e7) * deg * cos((lat1 + lat2) / 2);
	double dist = sqrt(dlat * dlat + dlon * dlon) * 6371000.0;
	double brg = atan2(dlon, dlat) * 180.0 / M_PI;
	if (brg < 0)
		brg += 360.0;
	last_distanceToHome = dist > 32767 ? 32767 : (int16_t)dist;
	/* Same convention as ArduPilot's MSP_COMP_GPS: absolute bearing, and 0
	 * when close enough that the arrow would only spin. */
	last_directionToHome = dist < 2.0 ? 0 : (int16_t)lround(brg);
}

static void mav_osd_handle(const mavlink_message_t *msg)
{
	switch (msg->msgid) {
	case MAVLINK_MSG_ID_HEARTBEAT:
		mav_osd_heartbeat(msg);
		break;

	case MAVLINK_MSG_ID_ATTITUDE: {
		mavlink_attitude_t a;
		mavlink_msg_attitude_decode(msg, &a);
		/* last_* hold MSP units (decidegrees). px_osd_fill negates the pitch
		 * MSP-style, so store it negated: nose up must come out positive. */
		last_roll = (int16_t)lrintf(a.roll * 1800.0f / (float)M_PI);
		last_pitch = (int16_t)lrintf(-a.pitch * 1800.0f / (float)M_PI);
		float yaw = a.yaw * 180.0f / (float)M_PI;
		last_heading = (int16_t)lrintf(yaw < 0 ? yaw + 360.0f : yaw);
		stat_msp_msg_attitude++;
		break;
	}

	case MAVLINK_MSG_ID_VFR_HUD: {
		mavlink_vfr_hud_t v;
		mavlink_msg_vfr_hud_decode(msg, &v);
		last_speed = (int16_t)lrintf(v.groundspeed * 100.0f);
		last_vario = (int16_t)lrintf(v.climb * 100.0f);
		last_throttle_pct = v.throttle;
		break;
	}

	case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
		mavlink_global_position_int_t p;
		mavlink_msg_global_position_int_decode(msg, &p);
		last_lat_e7 = p.lat;
		last_lon_e7 = p.lon;
		last_altitude = (int16_t)(p.relative_alt / 1000);
		mav_osd_update_home();
		break;
	}

	case MAVLINK_MSG_ID_GPS_RAW_INT: {
		mavlink_gps_raw_int_t g;
		mavlink_msg_gps_raw_int_decode(msg, &g);
		last_numSat = g.satellites_visible == 255 ? 0 : g.satellites_visible;
		break;
	}

	case MAVLINK_MSG_ID_HOME_POSITION: {
		mavlink_home_position_t h;
		mavlink_msg_home_position_decode(msg, &h);
		mav_home_lat_e7 = h.latitude;
		mav_home_lon_e7 = h.longitude;
		mav_home_valid = 1;
		mav_osd_update_home();
		break;
	}

	case MAVLINK_MSG_ID_SYS_STATUS: {
		mavlink_sys_status_t s;
		mavlink_msg_sys_status_decode(msg, &s);
		if (s.voltage_battery != UINT16_MAX)
			last_vbat_cv = s.voltage_battery / 10;      /* mV -> cV */
		if (s.current_battery >= 0)
			last_amperage_ca = s.current_battery;       /* already cA */
		last_batt_pct_fc = s.battery_remaining;          /* -1 unknown */
		break;
	}

	case MAVLINK_MSG_ID_BATTERY_STATUS: {
		mavlink_battery_status_t b;
		mavlink_msg_battery_status_decode(msg, &b);
		/* Only the main pack (id 0). Cell count is not read from voltages[]:
		 * without per-cell sensing ArduPilot puts the whole pack in [0]. */
		if (b.id == 0 && b.current_consumed >= 0)
			last_mah_used = (uint16_t)b.current_consumed;
		break;
	}

	case MAVLINK_MSG_ID_RC_CHANNELS: {
		mavlink_rc_channels_t rc;
		mavlink_msg_rc_channels_decode(msg, &rc);
		if (rc.rssi != 255)
			last_rssi_pct = rc.rssi * 100 / 254;
		break;
	}

	case MAVLINK_MSG_ID_STATUSTEXT: {
		mavlink_statustext_t st;
		mavlink_msg_statustext_decode(msg, &st);
		char text[sizeof(st.text) + 1];
		memcpy(text, st.text, sizeof(st.text));
		text[sizeof(st.text)] = '\0';
		set_fc_msg(text, st.severity);
		break;
	}
	}
}

/* Ask the FC for our streams. Called from the render timer; does nothing until
 * the FC has identified itself, then repeats every few seconds. */
static void mav_osd_request_streams(int fd)
{
	if (fd <= 0 || mav_fc_sysid == 0)
		return;
	uint64_t now = get_time_ms();
	if (mav_last_request_ms && now - mav_last_request_ms < MAV_OSD_REQUEST_PERIOD_MS)
		return;
	mav_last_request_ms = now;

	for (size_t i = 0; i < sizeof(mav_osd_streams) / sizeof(mav_osd_streams[0]); i++) {
		mavlink_message_t m;
		uint8_t buf[MAVLINK_MAX_PACKET_LEN];
		mavlink_msg_command_long_pack_chan(MAV_OSD_SYSID, MAV_COMP_ID_OSD,
			MAVLINK_COMM_2, &m, mav_fc_sysid, mav_fc_compid,
			MAV_CMD_SET_MESSAGE_INTERVAL, 0,
			(float)mav_osd_streams[i].msgid,
			(float)mav_osd_streams[i].interval_us, 0, 0, 0, 0, 0);
		int len = mavlink_msg_to_send_buffer(buf, &m);
		if (write(fd, buf, len) != len && verbose)
			printf("[mav_osd] stream request write failed\n");
	}
}

/* MAVLink mode has no MSP poll timer, which is what drives the pixel OSD's
 * redraw in MSP mode. This one does both jobs for -M. */
static void mav_osd_tick(evutil_socket_t sock, short event, void *arg)
{
	(void)sock;
	(void)event;
	int fd = *(int *)arg;
	if (px_osd_active())
		draw_screenBMP();
	mav_osd_request_streams(fd);
	mav_osd_dump();
}

/* MSPOSD_TELEM_DUMP=1 prints what the OSD would show, once a second - for
 * checking a new link without the video. */
static void mav_osd_dump(void)
{
	static int on = -1;
	static uint64_t last;
	if (on < 0) {
		const char *e = getenv("MSPOSD_TELEM_DUMP");
		on = e && *e == '1';
	}
	if (!on || get_time_ms() - last < 1000)
		return;
	last = get_time_ms();
	PxTelemetry t;
	px_osd_fill(&t);
	printf("[telem] mode=%s armed=%d roll=%.1f pitch=%.1f hdg=%.0f alt=%.0f "
	       "spd=%.1fkph vs=%.1f thr=%d volt=%.2f curr=%.2f mah=%d bat=%d%% "
	       "sats=%d home=%.0fm@%.0f rssi=%d fc_msg[%d]=\"%s\"\n",
	       t.mode, t.armed, t.roll_deg, t.pitch_deg, t.yaw_deg, t.alt_m,
	       t.spd_kph, t.vspd_ms, t.throttle_pct, t.volt_v, t.curr_a, t.mah_used,
	       t.batt_pct, t.sats, t.home_dist_m, t.home_bearing_deg, t.rssi_pct,
	       t.fc_msg_sev, t.fc_msg);
}
