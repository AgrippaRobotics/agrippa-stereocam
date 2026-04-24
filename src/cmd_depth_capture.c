/*
 * cmd_depth_capture.c — "ag-cam-tools depth-capture" subcommand
 *
 * Captures a single stereo frame, computes disparity via StereoSGBM,
 * converts to depth, and writes an RGBA PNG where RGB = camera colour
 * and A = depth normalised to [0, 255] against a user-supplied maximum.
 * Output is center-cropped to a square sized to a multiple of 32.
 *
 * Implements #19.
 */

#include "common.h"
#include "calib_load.h"
#include "image.h"
#include "stereo.h"
#include "../vendor/argtable3.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int
depth_capture (const char *device_id, const char *output_dir,
               const char *iface_ip,
               double exposure_us, double gain_db,
               gboolean auto_expose, int packet_size, int binning,
               gboolean verbose,
               const AgCalibSource *calib_src,
               double max_depth_cm, int user_size)
{
    int exitcode = EXIT_FAILURE;
    GError *error = NULL;

    ArvCamera *camera = arv_camera_new (device_id, &error);
    if (!camera) {
        fprintf (stderr, "error: %s\n",
                 error ? error->message : "failed to open device");
        g_clear_error (&error);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    printf ("Connected.\n");

    AgCameraConfig cfg;
    if (camera_configure (camera, AG_MODE_SINGLE_FRAME,
                          binning, exposure_us, gain_db, auto_expose,
                          packet_size, iface_ip, verbose, &cfg) != EXIT_SUCCESS) {
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    /* Stereo depth requires two eyes; refuse mono cameras explicitly. */
    if (cfg.sensor_mode == AG_SENSOR_MONO) {
        const char *model  = arv_camera_get_model_name (camera, NULL);
        const char *serial = arv_device_get_string_feature_value (
            arv_camera_get_device (camera), "DeviceSerialNumber", NULL);
        fprintf (stderr,
                 "error: depth-capture requires a stereo Lucid camera; "
                 "detected monocular sensor on %s (%s)\n",
                 model  ? model  : "(unknown model)",
                 serial ? serial : "(unknown serial)");
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);

    /* Processing dimensions (per eye). */
    guint proc_sub_w = (cfg.frame_w / 2) / (guint) cfg.software_binning;
    guint proc_h     = cfg.frame_h      / (guint) cfg.software_binning;

    /* Load calibration (mandatory for depth). */
    AgRemapTable *remap_left  = NULL;
    AgRemapTable *remap_right = NULL;
    AgCalibMeta meta = { .min_disparity = 0, .num_disparities = 128,
                         .focal_length_px = 0.0, .baseline_cm = 0.0 };

    if (ag_calib_load (device, calib_src,
                        &remap_left, &remap_right, &meta) != 0)
        goto cleanup;

    if (remap_left->width != proc_sub_w || remap_left->height != proc_h) {
        fprintf (stderr,
                 "error: remap dimensions %ux%u do not match frame %ux%u\n",
                 remap_left->width, remap_left->height,
                 proc_sub_w, proc_h);
        goto cleanup;
    }

    if (meta.focal_length_px <= 0.0 || meta.baseline_cm <= 0.0) {
        fprintf (stderr,
                 "error: calibration metadata missing focal_length_px (%.2f) "
                 "or baseline_cm (%.2f)\n",
                 meta.focal_length_px, meta.baseline_cm);
        goto cleanup;
    }

    printf ("Calibration loaded: focal=%.1f px  baseline=%.2f cm  "
            "disp_range=[%d, %d+%d)\n",
            meta.focal_length_px, meta.baseline_cm,
            meta.min_disparity, meta.min_disparity, meta.num_disparities);

    /* SGBM defaults, seeded from calibration metadata. */
    AgSgbmParams sgbm;
    ag_sgbm_params_defaults (&sgbm);
    if (meta.min_disparity != 0)
        sgbm.min_disparity = meta.min_disparity;
    if (meta.num_disparities > 0) {
        sgbm.num_disparities = meta.num_disparities;
        sgbm.num_disparities = ((sgbm.num_disparities + 15) / 16) * 16;
    }

    AgDisparityContext *disp_ctx = ag_disparity_create (
        AG_STEREO_SGBM, proc_sub_w, proc_h, &sgbm, NULL);
    if (!disp_ctx) {
        fprintf (stderr, "error: failed to create SGBM backend\n");
        goto cleanup;
    }

    /* Start acquisition. */
    printf ("Starting acquisition...\n");
    arv_camera_start_acquisition (camera, &error);
    if (error) {
        fprintf (stderr, "error: start acquisition: %s\n", error->message);
        g_clear_error (&error);
        ag_disparity_destroy (disp_ctx);
        goto cleanup;
    }

    if (auto_expose)
        auto_expose_settle (camera, &cfg, 100000.0);

    /* Wait for TriggerArmed. */
    {
        gboolean armed = FALSE;
        int polls = 0;
        while (!armed && polls < 100) {
            GError *e = NULL;
            armed = arv_device_get_boolean_feature_value (device,
                                                           "TriggerArmed", &e);
            g_clear_error (&e);
            if (!armed) { g_usleep (10000); polls++; }
        }
        if (!armed)
            fprintf (stderr, "warn: TriggerArmed not set, triggering anyway\n");
    }

    /* Fire software trigger. */
    {
        GError *e = NULL;
        arv_device_execute_command (device, "TriggerSoftware", &e);
        if (e) {
            fprintf (stderr, "error: TriggerSoftware: %s\n", e->message);
            g_clear_error (&e);
            arv_camera_stop_acquisition (camera, NULL);
            ag_disparity_destroy (disp_ctx);
            goto cleanup;
        }
    }

    /* Pop frame. */
    ArvBuffer *buffer = NULL;
    for (int i = 0; i < 10; i++) {
        ArvBuffer *b = arv_stream_timeout_pop_buffer (cfg.stream, 5000000);
        if (!b) continue;
        if (arv_buffer_get_status (b) == ARV_BUFFER_STATUS_SUCCESS) {
            buffer = b;
            break;
        }
        arv_stream_push_buffer (cfg.stream, b);
    }

    if (!buffer) {
        fprintf (stderr, "error: timeout waiting for frame\n");
        arv_camera_stop_acquisition (camera, NULL);
        ag_disparity_destroy (disp_ctx);
        goto cleanup;
    }

    size_t data_size = 0;
    const guint8 *data = arv_buffer_get_data (buffer, &data_size);
    guint w = arv_buffer_get_image_width (buffer);
    guint h = arv_buffer_get_image_height (buffer);

    if (!data || data_size < (size_t) w * h || w != cfg.frame_w || h != cfg.frame_h) {
        fprintf (stderr, "error: bad frame (%ux%u, %zu bytes)\n", w, h, data_size);
        arv_stream_push_buffer (cfg.stream, buffer);
        arv_camera_stop_acquisition (camera, NULL);
        ag_disparity_destroy (disp_ctx);
        goto cleanup;
    }

    /* ---- Process frame ---- */
    size_t eye_pixels = (size_t) proc_sub_w * proc_h;

    guint8 *bayer_left  = g_malloc (eye_pixels);
    guint8 *bayer_right = g_malloc (eye_pixels);

    extract_dual_bayer_eyes (data, w, h, cfg.software_binning,
                              bayer_left, bayer_right);

    /* Depth path: debayer to gray (no gamma) -> rectify -> disparity. */
    guint8 *gray_left   = g_malloc (eye_pixels);
    guint8 *gray_right  = g_malloc (eye_pixels);
    guint8 *rect_gray_l = g_malloc (eye_pixels);
    guint8 *rect_gray_r = g_malloc (eye_pixels);

    if (cfg.data_is_bayer) {
        debayer_rg8_to_gray (bayer_left,  gray_left,  proc_sub_w, proc_h);
        debayer_rg8_to_gray (bayer_right, gray_right, proc_sub_w, proc_h);
    } else {
        memcpy (gray_left,  bayer_left,  eye_pixels);
        memcpy (gray_right, bayer_right, eye_pixels);
    }
    ag_remap_gray (remap_left,  gray_left,  rect_gray_l);
    ag_remap_gray (remap_right, gray_right, rect_gray_r);

    int16_t *disparity_buf = g_malloc (eye_pixels * sizeof (int16_t));
    int disp_ok = ag_disparity_compute (disp_ctx, rect_gray_l, rect_gray_r,
                                         disparity_buf);
    if (disp_ok != 0) {
        fprintf (stderr, "error: disparity computation failed\n");
        goto cleanup_bufs;
    }

    /* Convert disparity -> depth -> normalised alpha. */
    guint8 *alpha_buf = g_malloc (eye_pixels);
    for (size_t i = 0; i < eye_pixels; i++) {
        double depth = ag_disparity_to_depth (disparity_buf[i],
                                               meta.focal_length_px,
                                               meta.baseline_cm);
        if (depth <= 0.0) {
            alpha_buf[i] = 0;
        } else {
            double normalised = depth * 255.0 / max_depth_cm;
            if (normalised > 255.0) normalised = 255.0;
            alpha_buf[i] = (guint8) normalised;
        }
    }

    /* Colour path: gamma -> debayer/expand to RGB -> rectify. */
    const guint8 *gamma_lut = gamma_lut_2p5 ();
    apply_lut_inplace (bayer_left, eye_pixels, gamma_lut);

    guint8 *rgb_left    = g_malloc (eye_pixels * 3);
    guint8 *rect_rgb_l  = g_malloc (eye_pixels * 3);

    if (cfg.data_is_bayer)
        debayer_rg8_to_rgb (bayer_left, rgb_left, proc_sub_w, proc_h);
    else
        gray_to_rgb_replicate (bayer_left, rgb_left, (uint32_t) eye_pixels);

    ag_remap_rgb (remap_left, rgb_left, rect_rgb_l);

    /* ---- Square crop ---- */
    int sq_side = 0;
    guint8 *sq_rgb   = g_malloc (eye_pixels * 3);
    guint8 *sq_alpha = g_malloc (eye_pixels);

    crop_center_square (rect_rgb_l, (int) proc_sub_w, (int) proc_h, 3,
                         sq_rgb, &sq_side);
    crop_center_square (alpha_buf,  (int) proc_sub_w, (int) proc_h, 1,
                         sq_alpha, NULL);

    /* ---- Resize ---- */
    int out_side;
    if (user_size > 0)
        out_side = user_size;
    else
        out_side = round_down_32 (sq_side);

    if (out_side <= 0) {
        fprintf (stderr, "error: output dimension rounds to 0 "
                 "(square side is %d)\n", sq_side);
        goto cleanup_sq;
    }

    guint8 *out_rgb;
    guint8 *out_alpha;

    if (out_side == sq_side) {
        /* No resize needed. */
        out_rgb   = sq_rgb;
        out_alpha = sq_alpha;
    } else {
        out_rgb   = g_malloc ((size_t) out_side * out_side * 3);
        out_alpha = g_malloc ((size_t) out_side * out_side);
        resize_nn (sq_rgb,   sq_side, 3, out_rgb,   out_side);
        resize_nn (sq_alpha, sq_side, 1, out_alpha, out_side);
    }

    /* ---- Write output ---- */
    time_t now = time (NULL);
    struct tm tm_now;
    localtime_r (&now, &tm_now);
    char basename[64];
    strftime (basename, sizeof basename,
              "depth_%Y%m%d_%H%M%S.png", &tm_now);
    char *out_path = g_build_filename (output_dir, basename, NULL);

    int write_rc = write_rgba_png (out_path, out_rgb, out_alpha,
                                    (guint) out_side, (guint) out_side);
    if (write_rc == EXIT_SUCCESS) {
        printf ("Saved: %s  (%dx%d RGBA, max_depth=%.0f cm)\n",
                out_path, out_side, out_side, max_depth_cm);
        exitcode = EXIT_SUCCESS;
    }
    g_free (out_path);

    if (out_side != sq_side) {
        g_free (out_rgb);
        g_free (out_alpha);
    }

cleanup_sq:
    g_free (sq_rgb);
    g_free (sq_alpha);

    g_free (rgb_left);
    g_free (rect_rgb_l);
    g_free (alpha_buf);

cleanup_bufs:
    g_free (bayer_left);
    g_free (bayer_right);
    g_free (gray_left);
    g_free (gray_right);
    g_free (rect_gray_l);
    g_free (rect_gray_r);
    g_free (disparity_buf);

    arv_stream_push_buffer (cfg.stream, buffer);
    arv_camera_stop_acquisition (camera, NULL);
    ag_disparity_destroy (disp_ctx);

cleanup:
    ag_remap_table_free (remap_left);
    ag_remap_table_free (remap_right);
    g_object_unref (cfg.stream);
    g_object_unref (camera);
    arv_shutdown ();
    return exitcode;
}

/* ------------------------------------------------------------------ */
/*  CLI entry point                                                    */
/* ------------------------------------------------------------------ */

int
cmd_depth_capture (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) ctx;

    struct arg_str *cmd       = arg_str1 (NULL, NULL, "depth-capture", NULL);
    struct arg_str *serial    = arg_str0 ("s", "serial",    "<serial>",
                                          "match by serial number");
    struct arg_str *address   = arg_str0 ("a", "address",   "<address>",
                                          "connect by camera IP");
    struct arg_str *interface = arg_str0 ("i", "interface",  "<iface>",
                                          "force NIC selection");
    struct arg_str *output    = arg_str0 ("o", "output",     "<dir>",
                                          "output directory (default: .)");
    struct arg_dbl *exposure  = arg_dbl0 ("x", "exposure",   "<us>",
                                          "exposure time in microseconds");
    struct arg_dbl *gain      = arg_dbl0 ("g", "gain",       "<dB>",
                                          "sensor gain in dB (0-48)");
    struct arg_lit *auto_exp  = arg_lit0 ("A", "auto-expose",
                                          "auto-expose then lock");
    struct arg_int *binning_a = arg_int0 ("b", "binning",    "<1|2>",
                                          "sensor binning factor (default: 1)");
    struct arg_int *pkt_size  = arg_int0 ("p", "packet-size", "<bytes>",
                                          "GigE packet size (default: auto-negotiate)");
    struct arg_str *calib_local = arg_str0 (NULL, "calibration-local", "<path>",
                                            "calibration session directory");
    struct arg_int *calib_slot  = arg_int0 (NULL, "calibration-slot", "<0-2>",
                                            "on-camera calibration slot");
    struct arg_dbl *max_depth   = arg_dbl1 (NULL, "max-depth", "<cm>",
                                            "maximum depth for alpha normalisation (cm)");
    struct arg_int *size_arg    = arg_int0 (NULL, "size", "<px>",
                                            "output side length (multiple of 32)");
    struct arg_lit *verbose   = arg_lit0 ("v", "verbose",
                                          "print diagnostic readback");
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (10);
    void *argtable[] = { cmd, serial, address, interface, output,
                         exposure, gain, auto_exp, binning_a, pkt_size,
                         calib_local, calib_slot, max_depth, size_arg,
                         verbose, help, end };

    int exitcode = EXIT_SUCCESS;
    if (arg_nullcheck (argtable) != 0) {
        arg_dstr_catf (res, "error: insufficient memory\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Defaults. */
    output->sval[0]    = ".";
    binning_a->ival[0] = 1;

    int nerrors = arg_parse (argc, argv, argtable);
    if (arg_make_syntax_err_help_msg (res, "depth-capture", help->count,
                                       nerrors, argtable, end, &exitcode))
        goto done;

    if (serial->count && address->count) {
        arg_dstr_catf (res, "error: --serial and --address are mutually exclusive\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Validate exposure. */
    double exposure_us = 0.0;
    if (exposure->count) {
        exposure_us = exposure->dval[0];
        if (exposure_us <= 0.0) {
            arg_dstr_catf (res, "error: --exposure must be positive\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }

    double gain_db = -1.0;
    if (gain->count) {
        gain_db = gain->dval[0];
        if (gain_db < 0.0 || gain_db > 48.0) {
            arg_dstr_catf (res, "error: --gain must be between 0 and 48\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }

    gboolean do_auto_expose = auto_exp->count > 0;
    if (do_auto_expose && (exposure->count || gain->count)) {
        arg_dstr_catf (res, "error: --auto-expose and --exposure/--gain "
                       "are mutually exclusive\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Validate binning. */
    int binning = binning_a->ival[0];
    if (binning != 1 && binning != 2) {
        arg_dstr_catf (res, "error: --binning must be 1 or 2\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Validate max-depth. */
    double max_depth_cm = max_depth->dval[0];
    if (max_depth_cm <= 0.0) {
        arg_dstr_catf (res, "error: --max-depth must be positive\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Validate size. */
    int user_size = 0;
    if (size_arg->count) {
        user_size = size_arg->ival[0];
        if (user_size <= 0 || (user_size & 31) != 0) {
            arg_dstr_catf (res, "error: --size must be a positive multiple of 32\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }

    /* Validate calibration args — at least one is required. */
    if (!calib_local->count && !calib_slot->count) {
        arg_dstr_catf (res, "error: depth-capture requires "
                       "--calibration-local or --calibration-slot\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }
    if (calib_local->count && calib_slot->count) {
        arg_dstr_catf (res, "error: --calibration-local and --calibration-slot "
                       "are mutually exclusive\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }
    if (calib_slot->count) {
        int s = calib_slot->ival[0];
        if (s < 0 || s > 2) {
            arg_dstr_catf (res, "error: --calibration-slot must be 0, 1, or 2\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }

    AgCalibSource calib_src = { .local_path = NULL, .slot = -1 };
    if (calib_local->count)
        calib_src.local_path = calib_local->sval[0];
    else if (calib_slot->count)
        calib_src.slot = calib_slot->ival[0];

    const char *opt_serial    = serial->count    ? serial->sval[0]    : NULL;
    const char *opt_address   = address->count   ? address->sval[0]   : NULL;
    const char *opt_interface = interface->count  ? interface->sval[0] : NULL;
    const char *opt_output    = output->sval[0];

    const char *iface_ip = NULL;
    if (opt_interface) {
        iface_ip = setup_interface (opt_interface);
        if (!iface_ip) { exitcode = EXIT_FAILURE; goto done; }
    }

    if (g_mkdir_with_parents (opt_output, 0755) != 0) {
        arg_dstr_catf (res, "error: cannot create output directory '%s'\n",
                       opt_output);
        exitcode = EXIT_FAILURE;
        goto done;
    }

    char *device_id = resolve_device (opt_serial, opt_address,
                                       opt_interface, TRUE);
    if (!device_id) { exitcode = EXIT_FAILURE; goto done; }

    int pkt_sz = pkt_size->count ? pkt_size->ival[0] : 0;

    exitcode = depth_capture (device_id, opt_output, iface_ip,
                               exposure_us, gain_db, do_auto_expose,
                               pkt_sz, binning, verbose->count > 0,
                               &calib_src, max_depth_cm, user_size);
    g_free (device_id);

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
