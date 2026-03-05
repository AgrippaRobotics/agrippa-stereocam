/*
 * burst.c — burst-mode capture for PDH016S
 *
 * Uses FrameBurstStart triggering: a single software trigger causes
 * the camera to deliver N frames at maximum frame rate.
 */

#include "burst.h"
#include "apriltag_detect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
burst_configure_trigger (ArvDevice *device, int burst_count)
{
    printf ("Configuring burst mode (N=%d)...\n", burst_count);

    /* Enable FrameBurstStart trigger. */
    try_set_string_feature  (device, "TriggerSelector", "FrameBurstStart");
    try_set_string_feature  (device, "TriggerMode",     "On");
    try_set_string_feature  (device, "TriggerSource",   "Software");
    try_set_integer_feature (device, "AcquisitionBurstFrameCount",
                             (gint64) burst_count);

    /* Disable FrameStart trigger — free-run within the burst. */
    try_set_string_feature  (device, "TriggerSelector", "FrameStart");
    try_set_string_feature  (device, "TriggerMode",     "Off");

    /* Disable frame rate capping for maximum throughput. */
    try_set_string_feature  (device, "AcquisitionFrameRateEnable", "False");

    return EXIT_SUCCESS;
}

void
burst_ensure_buffers (ArvStream *stream, size_t payload,
                      int burst_count, int already_pushed)
{
    int needed = burst_count + 4;
    if (needed < 16)
        needed = 16;

    int extra = needed - already_pushed;
    if (extra > 0) {
        printf ("  Pushing %d additional stream buffers for burst\n", extra);
        for (int i = 0; i < extra; i++)
            arv_stream_push_buffer (stream,
                                    arv_buffer_new_allocate (payload));
    }
}

char *
burst_frame_basename (int index, int total_count)
{
    int digits = 3;
    if (total_count > 999)
        digits = 4;
    return g_strdup_printf ("frame_%0*d", digits, index);
}

int
burst_capture (ArvCamera *camera, const AgCameraConfig *cfg,
               int burst_count, const char *output_dir,
               AgEncFormat enc,
               const AgRemapTable *remap_left,
               const AgRemapTable *remap_right,
               double tag_size_m)
{
    ArvDevice *device = arv_camera_get_device (camera);

    /* Wait for TriggerArmed. */
    gboolean armed = FALSE;
    for (int p = 0; p < 100 && !armed; p++) {
        GError *e = NULL;
        armed = arv_device_get_boolean_feature_value (device,
                                                       "TriggerArmed", &e);
        g_clear_error (&e);
        if (!armed)
            g_usleep (10000);
    }
    if (!armed)
        fprintf (stderr, "warn: TriggerArmed not set after 100 polls, "
                 "triggering anyway\n");
    else
        printf ("  TriggerArmed OK\n");

    /* Fire single software trigger for the entire burst. */
    {
        GError *e = NULL;
        arv_device_execute_command (device, "TriggerSoftware", &e);
        if (e) {
            fprintf (stderr, "error: TriggerSoftware failed: %s\n",
                     e->message);
            g_clear_error (&e);
            return EXIT_FAILURE;
        }
        printf ("  TriggerSoftware executed (burst of %d)\n", burst_count);
    }

    const char *pixel_format = arv_device_get_string_feature_value (
                                   device, "PixelFormat", NULL);
    gboolean is_dual_bayer = pixel_format &&
                              strcmp (pixel_format, "DualBayerRG8") == 0;

#ifdef HAVE_APRILTAG
    apriltag_detector_t *at_detector = NULL;
    apriltag_family_t   *at_family   = NULL;
    double at_fx = 0, at_fy = 0, at_cx = 0, at_cy = 0;

    if (tag_size_m > 0.0 && is_dual_bayer) {
        at_detector = ag_apriltag_detector_create (&at_family);

        guint sub_w = (cfg->frame_w / 2) / (guint) cfg->software_binning;
        guint sub_h = cfg->frame_h / (guint) cfg->software_binning;
        ag_apriltag_estimate_intrinsics (sub_w, sub_h,
                                         &at_fx, &at_fy, &at_cx, &at_cy);
    }
#else
    (void) tag_size_m;
#endif

    int frames_saved  = 0;
    int frames_failed = 0;

    for (int i = 0; i < burst_count; i++) {
        ArvBuffer *buffer = arv_stream_timeout_pop_buffer (cfg->stream,
                                                            5000000);
        if (!buffer) {
            fprintf (stderr, "  frame %d/%d: timeout (no buffer)\n",
                     i + 1, burst_count);
            frames_failed++;
            continue;
        }

        ArvBufferStatus st = arv_buffer_get_status (buffer);
        if (st != ARV_BUFFER_STATUS_SUCCESS) {
            fprintf (stderr, "  frame %d/%d: buffer status %d\n",
                     i + 1, burst_count, (int) st);
            arv_stream_push_buffer (cfg->stream, buffer);
            frames_failed++;
            continue;
        }

        size_t data_size = 0;
        const guint8 *data = arv_buffer_get_data (buffer, &data_size);
        guint width  = arv_buffer_get_image_width (buffer);
        guint height = arv_buffer_get_image_height (buffer);
        size_t needed = (size_t) width * (size_t) height;

        if (!data || data_size < needed) {
            fprintf (stderr, "  frame %d/%d: short buffer (%zu < %zu)\n",
                     i + 1, burst_count, data_size, needed);
            arv_stream_push_buffer (cfg->stream, buffer);
            frames_failed++;
            continue;
        }

        char *base = burst_frame_basename (i, burst_count);
        int rc;

        if (is_dual_bayer) {
#ifdef HAVE_APRILTAG
            if (at_detector) {
                guint sub_w = (width / 2) / (guint) cfg->software_binning;
                guint sub_h = height / (guint) cfg->software_binning;
                size_t eye_n = (size_t) sub_w * (size_t) sub_h;
                guint8 *tag_left  = g_malloc (eye_n);
                guint8 *tag_right = g_malloc (eye_n);
                extract_dual_bayer_eyes (data, width, height,
                                          cfg->software_binning,
                                          tag_left, tag_right);

                AgTagOverlay overlays[AG_MAX_TAG_OVERLAYS];
                ag_detect_tags_and_pose (at_detector, tag_left,
                                         sub_w, sub_h, tag_size_m,
                                         at_fx, at_fy, at_cx, at_cy,
                                         (guint64) i, "left",
                                         overlays, AG_MAX_TAG_OVERLAYS);
                ag_detect_tags_and_pose (at_detector, tag_right,
                                         sub_w, sub_h, tag_size_m,
                                         at_fx, at_fy, at_cx, at_cy,
                                         (guint64) i, "right",
                                         overlays, AG_MAX_TAG_OVERLAYS);

                g_free (tag_left);
                g_free (tag_right);
            }
#endif
            rc = write_dual_bayer_pair (output_dir, base, data,
                                         width, height, enc,
                                         cfg->software_binning,
                                         cfg->data_is_bayer,
                                         remap_left, remap_right);
        } else {
            const char *ext = (enc == AG_ENC_PNG) ? "png"
                            : (enc == AG_ENC_JPG) ? "jpg" : "pgm";
            char *name = g_strdup_printf ("%s.%s", base, ext);
            char *path = g_build_filename (output_dir, name, NULL);
            if (enc == AG_ENC_PGM)
                rc = write_pgm (path, data, width, height);
            else
                rc = write_color_image (enc, path, data, width, height);
            g_free (name);
            g_free (path);
        }

        g_free (base);
        arv_stream_push_buffer (cfg->stream, buffer);

        if (rc == EXIT_SUCCESS) {
            frames_saved++;
            printf ("  frame %d/%d saved\n", i + 1, burst_count);
        } else {
            frames_failed++;
        }
    }

#ifdef HAVE_APRILTAG
    ag_apriltag_detector_destroy (at_detector, at_family);
#endif

    printf ("Burst complete: %d/%d frames saved", frames_saved, burst_count);
    if (frames_failed > 0)
        printf (" (%d failed)", frames_failed);
    printf ("\n");

    return (frames_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
