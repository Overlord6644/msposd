/*
 * dp_text.c - Betaflight's own OSD texts for the pixel OSD.
 *
 * The pixel OSD polls the numbers (MSP) and draws everything itself, but the
 * words a pilot needs - LOW BATTERY, >CRASH FLIP<, LAND NOW, RESCUE N/A, why
 * arming is refused - exist only in Betaflight's OSD. With the camera's port
 * set to MSP + "VTX (MSP + DisplayPort)" and osd_displayport_device = MSP,
 * Betaflight sends that OSD as DisplayPort text; enable only its Warnings and
 * Flight mode elements, anywhere on screen. Their text lands in
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

/* px_osd_fill: Betaflight's own words take over while they are fresh. */
static void dp_text_fill(PxTelemetry *t)
{
	uint64_t now = get_time_ms();
	if (dp_mode_ms && now - dp_mode_ms < DP_MODE_HOLD_MS)
		snprintf(t->mode, sizeof(t->mode), "%s", dp_mode);
	if (dp_crashflip_ms && now - dp_crashflip_ms < DP_WARN_HOLD_MS)
		snprintf(t->mode, sizeof(t->mode), "CRASH FLIP");

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
	} else if (dp_warn_ms && now - dp_warn_ms < DP_WARN_HOLD_MS) {
		snprintf(t->fc_msg, sizeof(t->fc_msg), "%s", dp_warn);
		/* the ones that mean "act now" in red, the rest yellow */
		int crit = strstr(dp_warn, "LAND NOW") || strstr(dp_warn, "FAIL") ||
			   strstr(dp_warn, "OVERLOAD") || strstr(dp_warn, "FLYAWAY");
		t->fc_msg_sev = crit ? MAV_SEVERITY_ERROR : MAV_SEVERITY_WARNING;
	}
}
