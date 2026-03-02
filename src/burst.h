/*
 * burst.h — burst-mode capture for PDH016S
 *
 * Configures the camera for FrameBurstStart triggering and provides
 * the multi-frame acquisition loop.  Designed for reuse by any command
 * or driver that needs burst capture.
 */

#ifndef AG_BURST_H
#define AG_BURST_H

#include "common.h"
#include "image.h"
#include "remap.h"

/* Burst count limits. */
#define AG_BURST_MIN   2
#define AG_BURST_MAX   100

/*
 * Reconfigure trigger registers for FrameBurstStart mode.
 *
 * Precondition: camera_configure() has already been called with
 * AG_MODE_CONTINUOUS.  This function switches from FrameStart
 * triggering to FrameBurstStart triggering:
 *
 *   TriggerSelector            = FrameBurstStart
 *   TriggerMode                = On
 *   TriggerSource              = Software
 *   AcquisitionBurstFrameCount = burst_count
 *   TriggerSelector            = FrameStart
 *   TriggerMode                = Off
 *   AcquisitionFrameRateEnable = False
 *
 * Returns 0 on success, EXIT_FAILURE on error.
 */
int burst_configure_trigger (ArvDevice *device, int burst_count);

/*
 * Ensure the ArvStream has enough buffers for a burst of the given
 * size.  Pushes additional buffers if the current count is
 * insufficient.
 *
 *   needed = max(16, burst_count + 4)
 *   push   = needed - already_pushed
 */
void burst_ensure_buffers (ArvStream *stream, size_t payload,
                           int burst_count, int already_pushed);

/*
 * Execute a burst capture: wait for TriggerArmed, fire one
 * TriggerSoftware, then pop burst_count frames from the stream.
 *
 * For each successfully received frame, calls write_dual_bayer_pair()
 * to save it to output_dir with the basename "frame_NNN".
 *
 * Parameters:
 *   camera       - open ArvCamera (acquisition must be started)
 *   cfg          - camera config from camera_configure()
 *   burst_count  - number of frames to capture
 *   output_dir   - subdirectory for this burst's output files
 *   enc          - output encoding format
 *   remap_left   - rectification table or NULL
 *   remap_right  - rectification table or NULL
 *
 * Returns 0 if all burst_count frames were saved successfully,
 * EXIT_FAILURE if any frame was lost or writing failed.
 */
int burst_capture (ArvCamera *camera, const AgCameraConfig *cfg,
                   int burst_count, const char *output_dir,
                   AgEncFormat enc,
                   const AgRemapTable *remap_left,
                   const AgRemapTable *remap_right);

/*
 * Generate the output basename for frame index i in a burst of
 * total_count frames.  Returns a g_malloc'd string like "frame_003".
 * Caller must g_free.
 */
char *burst_frame_basename (int index, int total_count);

#endif /* AG_BURST_H */
