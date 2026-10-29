/*
 * dp_text.c - Betaflight's state in words, for the pixel OSD's mode widget
 * and its fc_msg bar.
 *
 * Mostly from MSP data, with a plain MSP port: the mode from MSP_STATUS's
 * flag bits mapped through MSP_BOXIDS, every arming-disable reason from
 * MSP_STATUS_EX's mask, LOW BATTERY / LAND NOW from MSP_BATTERY_STATE's
 * verdict, FAIL SAFE and CRASH FLIP from the boxes.
 *
 * Optionally also from Betaflight's own OSD, for the few warnings the data
 * has no field for (RESCUE N/A, BEACON ON, ARM IN 1.2, ESC alarms...). With
 * the port set to MSP + "VTX (MSP + DisplayPort)" and osd_displayport_device
 * = MSP, Betaflight sends its OSD as DisplayPort text; enable only its
 * Warnings and Flight mode elements, anywhere on screen. Their text lands in
 * character_map like it does for the character OSD (which stays off), and
 * here it is read back at every draw_complete:
 *   - a run that is one of the Flight mode element's words -> the mode
 *   - an arming-disabled flag name (shown one at a time, 0.5 s each, while
 *     the arm switch is on) -> collected into "CAN'T ARM: A - B"
 *   - anything else -> the warning, as Betaflight wrote it
 * Warnings blink, so each is held a little past its last sighting.
 *
 * Included into msposd.c after osd/mav_osd.c: shares its arming-reason words.
 */

#define DP_MODE_HOLD_MS 2000
#define DP_WARN_HOLD_MS 1500
#define DP_ARM_HOLD_MS  1600 /* a bit more than one turn of BF's rotation */
#define DP_MAX_RUNS     4    /* more is a stats page, the CMS menu or the logo */

static const struct { const char *bf, *name; } dp_modes[] = {
	{"!FS!", "FAILSAFE"},  {"N-RESC", "GPS RESCUE"}, {"RESC", "GPS RESCUE"},
	{"HEAD", "HEADFREE"},  {"PASS", "PASSTHRU"},     {"POSH", "POS HOLD"},
	{"ALTH", "ALT HOLD"},  {"ANGL", "ANGLE"},        {"HOR", "HORIZON"},
	{"ATRN", "ACRO TRAINER"}, {"CHIR", "CHIRP"},
	/* Betaflight says AIR for acro with airmode on, which is nearly always */
	{"AIR", "ACRO"},       {"ACRO", "ACRO"},
};

static char dp_mode[16];
static uint64_t dp_mode_ms;
static char dp_warn[64];
static uint64_t dp_warn_ms;
static uint64_t dp_crashflip_ms;
static struct { const char *text; uint64_t ms; } dp_arm[sizeof(mav_arm_reasons) / sizeof(mav_arm_reasons[0])];

static void dp_text_run(const char *run, uint64_t now)
{
	for (size_t i = 0; i < sizeof(dp_modes) / sizeof(dp_modes[0]); i++)
		if (!strcmp(run, dp_modes[i].bf)) {
			snprintf(dp_mode, sizeof(dp_mode), "%s", dp_modes[i].name);
			dp_mode_ms = now;
			return;
		}
	for (size_t i = 0; i < sizeof(mav_arm_reasons) / sizeof(mav_arm_reasons[0]); i++)
		if (!strcmp(run, mav_arm_reasons[i].flag)) {
			dp_arm[i].text = mav_arm_reasons[i].text;
			dp_arm[i].ms = now;
			return;
		}
	if (!strcmp(run, ">CRASH FLIP<")) {
		dp_crashflip_ms = now;
		run = "CRASH FLIP";
	}
	snprintf(dp_warn, sizeof(dp_warn), "%s", run);
	dp_warn_ms = now;
}

/* draw_complete, pixel mode: read the finished frame's text back. */
static void dp_text_scan(void)
{
	char runs[DP_MAX_RUNS][MAX_OSD_WIDTH + 1];
	int nruns = 0;
	for (int y = 0; y < current_display_info.char_height && y < MAX_OSD_HEIGHT; y++) {
		char line[MAX_OSD_WIDTH + 1];
		int w = current_display_info.char_width < MAX_OSD_WIDTH
			? current_display_info.char_width : MAX_OSD_WIDTH;
		for (int x = 0; x < w; x++) {
			uint16_t c = character_map_complete[x][y];
			/* page 0 printable ASCII is text; symbols and empty cells
			 * are not */
			line[x] = (c >= 0x20 && c < 0x7f) ? (char)c : ' ';
		}
		line[w] = '\0';
		/* runs of text, split where two blanks in a row separate elements */
		for (int x = 0; x < w;) {
			while (x < w && line[x] == ' ')
				x++;
			int s = x, e = x;
			while (x < w && !(line[x] == ' ' && (x + 1 >= w || line[x + 1] == ' '))) {
				x++;
				e = x;
			}
			if (e > s) {
				if (nruns == DP_MAX_RUNS)
					return; /* a full page: not the elements we read */
				memcpy(runs[nruns], line + s, (size_t)(e - s));
				runs[nruns][e - s] = '\0';
				nruns++;
			}
		}
	}
	uint64_t now = get_time_ms();
	for (int i = 0; i < nruns; i++)
		dp_text_run(runs[i], now);
}

/* Betaflight's arming-disable flags in bit order (fc/runtime_config.c,
 * 2026.6), the names its OSD and STATUSTEXT use. */
static const char *const bf_arm_flag_names[] = {
	"NOGYRO", "FAILSAFE", "RXLOSS", "NOT_DISARMED", "BOXFAILSAFE", "RUNAWAY",
	"CRASH", "THROTTLE", "ANGLE", "BOOTGRACE", "NOPREARM", "LOAD", "CALIB",
	"CLI", "CMS", "BST", "MSP", "PARALYZE", "GPS", "RESCUE_SW", "DSHOT_TELEM",
	"REBOOT_REQD", "DSHOT_BBANG", "NO_ACC_CAL", "MOTOR_PROTO", "FLIP_SWITCH",
	"ALT_HOLD_SW", "POS_HOLD_SW", "AUTOPILOT_SW", "ARM_SWITCH",
};
#define BF_ARM_SWITCH_BIT 29

static const char *bf_arm_word(const char *flag)
{
	for (size_t i = 0; i < sizeof(mav_arm_reasons) / sizeof(mav_arm_reasons[0]); i++)
		if (!strcmp(flag, mav_arm_reasons[i].flag))
			return mav_arm_reasons[i].text;
	return flag;
}

/* Latched in the MSP_STATUS handler (osd.c): see bf_crash_flipping. */
static int bf_crash_flip(void)
{
	return bf_crash_flipping;
}

/* The MSP data's mode, in Betaflight's own precedence (osdElementFlymode);
 * NULL until MSP_BOXIDS says which bit is which. */
static const char *bf_data_mode(void)
{
	if (bf_box_count <= 0 || !last_fm_seen)
		return NULL;
	if (bf_crash_flip())
		return "CRASH FLIP";
	static const struct { uint8_t id; const char *name; } order[] = {
		{27, "FAILSAFE"}, {46, "GPS RESCUE"}, {6, "HEADFREE"}, {12, "PASSTHRU"},
		{11, "POS HOLD"}, {3, "ALT HOLD"}, {1, "ANGLE"}, {2, "HORIZON"},
		{47, "ACRO TRAINER"}, {56, "AUTOPILOT"},
	};
	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++)
		if (bf_box_on(order[i].id))
			return order[i].name;
	return "ACRO";
}

/* The MSP data's warning, most urgent first; severity in *sev. */
static int bf_data_warning(char *out, size_t n, int *sev)
{
	if (bf_box_count > 0 && bf_box_on(27)) {
		snprintf(out, n, "FAIL SAFE");
		*sev = MAV_SEVERITY_ERROR;
		return 1;
	}
	if (bf_box_count > 0 && bf_crash_flip()) {
		snprintf(out, n, "CRASH FLIP");
		*sev = MAV_SEVERITY_WARNING;
		return 1;
	}
	/* Every reason at once, whenever it is disarmed - not one at a time with
	 * the switch on like Betaflight's OSD - and ahead of the battery: on the
	 * ground, why it will not arm is the question. ARM_SWITCH only says the
	 * switch is up while blocked: dropped unless alone, as Betaflight does. */
	uint32_t f = bf_arm_flags;
	if (!armed && bf_status_ex_seen && f) {
		if (f != (1u << BF_ARM_SWITCH_BIT))
			f &= ~(1u << BF_ARM_SWITCH_BIT);
		snprintf(out, n, "%s", MAV_ARM_PREFIX);
		int first = 1;
		for (unsigned b = 0; b < 32; b++) {
			if (!(f & (1u << b)))
				continue;
			char unknown[12];
			const char *flag = b < sizeof(bf_arm_flag_names) / sizeof(bf_arm_flag_names[0])
				? bf_arm_flag_names[b] : (snprintf(unknown, sizeof(unknown), "FLAG%u", b), unknown);
			size_t used = strlen(out);
			snprintf(out + used, n - used, "%s%s", first ? "" : " - ", bf_arm_word(flag));
			first = 0;
		}
		*sev = MAV_SEVERITY_WARNING;
		return 1;
	}
	if (last_batt_state == 2 || last_batt_state == 1) {
		snprintf(out, n, last_batt_state == 2 ? "LAND NOW" : "LOW BATTERY");
		*sev = last_batt_state == 2 ? MAV_SEVERITY_ERROR : MAV_SEVERITY_WARNING;
		return 1;
	}
	return 0;
}

/* px_osd_fill: Betaflight's state in words. The MSP data first; text read
 * off its DisplayPort OSD, when the port carries it, fills what the data
 * has no field for (RESCUE N/A, BEACON ON, ARM IN 1.2, ESC alarms...). */
static void dp_text_fill(PxTelemetry *t)
{
	uint64_t now = get_time_ms();
	const char *mode = bf_data_mode();
	if (mode)
		snprintf(t->mode, sizeof(t->mode), "%s", mode);
	else if (dp_mode_ms && now - dp_mode_ms < DP_MODE_HOLD_MS)
		snprintf(t->mode, sizeof(t->mode), "%s", dp_mode);
	if (dp_crashflip_ms && now - dp_crashflip_ms < DP_WARN_HOLD_MS)
		snprintf(t->mode, sizeof(t->mode), "CRASH FLIP");

	/* a DisplayPort warning is Betaflight's own pick, by its own precedence */
	if (dp_warn_ms && now - dp_warn_ms < DP_WARN_HOLD_MS) {
		snprintf(t->fc_msg, sizeof(t->fc_msg), "%s", dp_warn);
		/* the ones that mean "act now" in red, the rest yellow */
		int crit = strstr(dp_warn, "LAND NOW") || strstr(dp_warn, "FAIL") ||
			   strstr(dp_warn, "OVERLOAD") || strstr(dp_warn, "FLYAWAY");
		t->fc_msg_sev = crit ? MAV_SEVERITY_ERROR : MAV_SEVERITY_WARNING;
		return;
	}
	int sev;
	if (bf_data_warning(t->fc_msg, sizeof(t->fc_msg), &sev)) {
		t->fc_msg_sev = sev;
		return;
	}
	char arm[sizeof(t->fc_msg)] = "";
	for (size_t i = 0; i < sizeof(dp_arm) / sizeof(dp_arm[0]); i++) {
		if (!dp_arm[i].text || now - dp_arm[i].ms >= DP_ARM_HOLD_MS)
			continue;
		size_t used = strlen(arm);
		snprintf(arm + used, sizeof(arm) - used, "%s%s",
			 used ? " - " : MAV_ARM_PREFIX, dp_arm[i].text);
	}
	if (arm[0]) {
		snprintf(t->fc_msg, sizeof(t->fc_msg), "%s", arm);
		t->fc_msg_sev = MAV_SEVERITY_WARNING;
	}
}
