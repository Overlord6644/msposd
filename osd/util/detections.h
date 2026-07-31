/* detections.h - draw AI detection boxes (from an external worker) into the
 * msposd overlay canvas, so ONE region carries both OSD and boxes.
 *
 * The IPU worker writes /tmp/yolo.det atomically (tmp + rename):
 *   line 1:  <epoch_ms> <count>
 *   lines:   <class> <score_pct> <x1> <y1> <x2> <y2>   (normalized 0..1 floats)
 *
 * Boxes older than DET_STALE_MS are not drawn (worker gone / paused). */
#ifndef DETECTIONS_H
#define DETECTIONS_H

#include <stdint.h>

/* Overridable so the host-side preview harness can point at a fixture. */
#ifndef DET_FILE
#define DET_FILE "/tmp/yolo.det"
#endif
#ifndef DET_TEST_FILE
#define DET_TEST_FILE "/tmp/yolo.test"
#endif
#define DET_STALE_MS 1500
#define DET_MAX 32

/* Draw the current detections into an I4 canvas of size w x h.
 * rowStride = bytes per row. Returns number of boxes drawn. */
int draw_detections_i4(uint8_t *bmpData, uint32_t w, uint32_t h, uint32_t rowStride);

#endif
