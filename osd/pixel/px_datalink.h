/*
 * px_datalink.h - pull the radio link figures out of the air-unit status line.
 *
 * On this camera the adaptive-link daemon does not expose an API: it writes a
 * single formatted line for the OSD to print, e.g.
 *
 *   LQ:79% | MCS:5 | 52000kbps | 3.3Mb FPS:6 | CPU:24%,73c | TX:62c
 *
 * The character OSD showed that string verbatim. A pixel OSD wants the numbers,
 * so it can colour a value by threshold, drive a bar from it, or place link
 * quality somewhere other than wherever the string happens to fall.
 *
 * Parsing someone else's display string is inherently brittle, so the scanner
 * looks for known keys anywhere in the line and takes the first number after
 * each, rather than assuming field order or separators. A field that is absent
 * keeps its previous value instead of snapping to zero - a missing token in one
 * update should not make the OSD flash "0 Mbps".
 */
#ifndef PX_DATALINK_H
#define PX_DATALINK_H

typedef struct {
	int   lq_pct;        /* LQ:79%          link quality            */
	int   mcs;           /* MCS:5           modulation index        */
	int   bitrate_kbps;  /* 52000kbps       encoder target          */
	float throughput_mb; /* 3.3Mb           measured air throughput */
	int   fps;           /* FPS:6           frames actually sent    */
	int   cpu_pct;       /* CPU:24%,73c     load                    */
	int   soc_temp_c;    /* CPU:24%,73c     SoC temperature         */
	int   tx_temp_c;     /* TX:62c          radio temperature       */
	/* Second and third lines of the block:
	 *   [#########......] [66%] uplink
	 *   pw:1450 | Ch:165-20mhz | q:1745,shift:0.0                            */
	int   uplink_pct;    /* [66%] uplink    uplink quality          */
	int   tx_power;      /* pw:1450         radio tx power          */
	int   channel;       /* Ch:165-20mhz    channel number          */
	int   bandwidth_mhz; /* Ch:165-20mhz    channel width           */
	int   pubq;          /* q:1745          driver queue metric     */
	int   have;          /* 1 once anything has been parsed         */
} PxDatalink;

/* Scan `line` and update `dl` in place. Safe to call with NULL or an empty
 * string, which leaves the previous values alone. */
void px_datalink_parse(PxDatalink *dl, const char *line);

#endif /* PX_DATALINK_H */
