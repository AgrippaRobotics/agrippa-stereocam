/*
 * cmd_capture.c — "ag-cam-tools capture" subcommand
 *
 * SingleFrame acquisition with software trigger.  Writes a stereo
 * DualBayerRG8 pair (PDH016S) or a single Bayer/Mono frame (mono
 * Lucid cameras like the Triton TRT016S).  With --burst N, captures N
 * frames in rapid succession via FrameBurstStart triggering — burst is
 * currently stereo-only.
 *
 * The single-frame path is implemented on top of libagrippa
 * (src/agrippa.[ch]) so the library is dogfooded by the CLI; the
 * burst path retains its own pipeline because libagrippa does not yet
 * expose FrameBurstStart triggering.
 */

#include "agrippa.h"
#include "common.h"
#include "apriltag_detect.h"
#include "burst.h"
#include "calib_load.h"
#include "image.h"
#include "../vendor/argtable3.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int
capture_one_frame (const char *serial, const char *address,
                   const char *interface_name, const char *output_dir,
                   AgEncFormat enc,
                   double exposure_us, double gain_db,
                   gboolean auto_expose, int packet_size, int binning,
                   gboolean verbose,
                   const AgCalibSource *calib_src,
                   double tag_size_m)
{
    AgOpenParams params = {
        .address                = address,
        .serial                 = serial,
        .interface_name         = interface_name,
        .exposure_us            = exposure_us,
        .gain_db                = gain_db,
        .auto_expose            = auto_expose ? 1 : 0,
        .binning                = binning,
        .packet_size            = packet_size,
        .calibration_local_path = calib_src->local_path,
        .calibration_slot       = calib_src->slot,
        .continuous             = 0,
        .verbose                = verbose ? 1 : 0,
    };

    AgCamera *cam = ag_camera_open (&params);
    if (!cam)
        return EXIT_FAILURE;

    AgFrame frame;
    if (ag_camera_capture (cam, &frame) != 0) {
        fprintf (stderr, "error: %s\n", ag_camera_last_error (cam));
        ag_camera_close (cam);
        return EXIT_FAILURE;
    }

    time_t now = time (NULL);
    struct tm tm_now;
    localtime_r (&now, &tm_now);
    char base[64];
    strftime (base, sizeof base, "capture_%Y%m%d_%H%M%S", &tm_now);

    int rc;
    if (ag_camera_is_stereo (cam)) {
        const void *ov_left = NULL, *ov_right = NULL;
        int n_ltags = 0, n_rtags = 0;

#ifdef HAVE_APRILTAG
        AgTagOverlay left_ov[AG_MAX_TAG_OVERLAYS];
        AgTagOverlay right_ov[AG_MAX_TAG_OVERLAYS];
        if (tag_size_m > 0.0) {
            AgApriltagContext *at_ctx = ag_apriltag_create ();
            double fx, fy, cx, cy;
            ag_apriltag_estimate_intrinsics (frame.width, frame.height,
                                             &fx, &fy, &cx, &cy);

            n_ltags = ag_detect_tags_and_pose (at_ctx->detector,
                         frame.left, frame.width, frame.height, tag_size_m,
                         fx, fy, cx, cy, 0, "left",
                         left_ov, AG_MAX_TAG_OVERLAYS, FALSE);
            n_rtags = ag_detect_tags_and_pose (at_ctx->detector,
                         frame.right, frame.width, frame.height, tag_size_m,
                         fx, fy, cx, cy, 0, "right",
                         right_ov, AG_MAX_TAG_OVERLAYS, FALSE);

            ov_left  = left_ov;
            ov_right = right_ov;

            ag_apriltag_destroy (at_ctx);
        }
#else
        (void) tag_size_m;
#endif

        rc = write_split_bayer_pair (output_dir, base,
                                     frame.left, frame.right,
                                     frame.width, frame.height, enc,
                                     ag_camera_data_is_bayer (cam) ? TRUE : FALSE,
                                     ag_camera_get_remap_left  (cam),
                                     ag_camera_get_remap_right (cam),
                                     ov_left, n_ltags,
                                     ov_right, n_rtags);
    } else {
#ifdef HAVE_APRILTAG
        if (tag_size_m > 0.0) {
            AgApriltagContext *at_ctx = ag_apriltag_create ();
            double fx, fy, cx, cy;
            ag_apriltag_estimate_intrinsics (frame.width, frame.height,
                                             &fx, &fy, &cx, &cy);
            AgTagOverlay tags[AG_MAX_TAG_OVERLAYS];
            ag_detect_tags_and_pose (at_ctx->detector, frame.left,
                                     frame.width, frame.height, tag_size_m,
                                     fx, fy, cx, cy, 0, "mono",
                                     tags, AG_MAX_TAG_OVERLAYS, FALSE);
            ag_apriltag_destroy (at_ctx);
        }
#endif
        const char *ext = (enc == AG_ENC_PNG) ? "png"
                        : (enc == AG_ENC_JPG) ? "jpg" : "pgm";
        char *name = g_strdup_printf ("%s.%s", base, ext);
        char *path = g_build_filename (output_dir, name, NULL);
        if (enc == AG_ENC_PGM)
            rc = write_pgm (path, frame.left, frame.width, frame.height);
        else
            rc = write_color_image (enc, path, frame.left,
                                    frame.width, frame.height);
        printf ("Saved: %s  (%ux%u)\n", path, frame.width, frame.height);
        g_free (name);
        g_free (path);
    }

    ag_camera_release_frame (cam, &frame);
    ag_camera_close (cam);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Burst capture path                                                 */
/* ------------------------------------------------------------------ */

static int
capture_burst_frames (const char *device_id, const char *output_dir,
                      const char *iface_ip, AgEncFormat enc,
                      double exposure_us, double gain_db,
                      gboolean auto_expose, int packet_size, int binning,
                      gboolean verbose,
                      const AgCalibSource *calib_src,
                      int burst_count, double tag_size_m)
{
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

    /* Use Continuous mode so FrameBurstStart triggering works. */
    AgCameraConfig cfg;
    if (camera_configure (camera, AG_MODE_CONTINUOUS,
                          binning, exposure_us, gain_db, auto_expose,
                          packet_size, iface_ip, verbose, &cfg) != EXIT_SUCCESS) {
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    /* Burst capture currently writes via write_dual_bayer_pair, which
     * is stereo-only.  Mono burst would need a separate pipeline. */
    if (cfg.sensor_mode == AG_SENSOR_MONO) {
        fprintf (stderr,
                 "error: --burst is currently stereo-only; not yet "
                 "implemented for mono cameras\n");
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);

    /* Load rectification remap tables if calibration was requested. */
    AgRemapTable *remap_left  = NULL;
    AgRemapTable *remap_right = NULL;

    if (calib_src->local_path || calib_src->slot >= 0) {
        if (ag_calib_load (device, calib_src,
                            &remap_left, &remap_right, NULL) != 0) {
            g_object_unref (cfg.stream);
            g_object_unref (camera);
            arv_shutdown ();
            return EXIT_FAILURE;
        }

        guint proc_sub_w = (cfg.frame_w / 2) / (guint) cfg.software_binning;
        guint proc_h     = cfg.frame_h / (guint) cfg.software_binning;
        if (remap_left->width != proc_sub_w ||
            remap_left->height != proc_h) {
            fprintf (stderr,
                     "error: remap dimensions %ux%u do not match frame %ux%u\n",
                     remap_left->width, remap_left->height,
                     proc_sub_w, proc_h);
            ag_remap_table_free (remap_left);
            ag_remap_table_free (remap_right);
            g_object_unref (cfg.stream);
            g_object_unref (camera);
            arv_shutdown ();
            return EXIT_FAILURE;
        }

        printf ("Rectification maps loaded (%ux%u).\n",
                remap_left->width, remap_left->height);
    }

    /* Auto-expose: settle under FrameStart trigger, then reconfigure. */
    if (auto_expose) {
        printf ("Starting acquisition for auto-exposure settle...\n");
        arv_camera_start_acquisition (camera, &error);
        if (error) {
            fprintf (stderr, "error: failed to start acquisition: %s\n",
                     error->message);
            g_clear_error (&error);
            ag_remap_table_free (remap_left);
            ag_remap_table_free (remap_right);
            g_object_unref (cfg.stream);
            g_object_unref (camera);
            arv_shutdown ();
            return EXIT_FAILURE;
        }

        auto_expose_settle (camera, &cfg, 100000.0);

        arv_camera_stop_acquisition (camera, NULL);
        printf ("Auto-exposure locked.  Reconfiguring for burst...\n");
    }

    /* Reconfigure trigger registers for FrameBurstStart. */
    burst_configure_trigger (device, burst_count);

    /* Ensure enough stream buffers.  camera_configure pushed 16. */
    burst_ensure_buffers (cfg.stream, cfg.payload, burst_count, 16);

    /* Start acquisition in burst mode. */
    printf ("Starting burst acquisition...\n");
    arv_camera_start_acquisition (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to start acquisition: %s\n",
                 error->message);
        g_clear_error (&error);
        ag_remap_table_free (remap_left);
        ag_remap_table_free (remap_right);
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    /* Create timestamped burst output subdirectory. */
    time_t now = time (NULL);
    struct tm tm_now;
    localtime_r (&now, &tm_now);
    char subdir_name[64];
    strftime (subdir_name, sizeof subdir_name,
              "burst_%Y%m%d_%H%M%S", &tm_now);
    char *burst_dir = g_build_filename (output_dir, subdir_name, NULL);

    if (g_mkdir_with_parents (burst_dir, 0755) != 0) {
        fprintf (stderr, "error: cannot create burst directory '%s'\n",
                 burst_dir);
        g_free (burst_dir);
        arv_camera_stop_acquisition (camera, NULL);
        ag_remap_table_free (remap_left);
        ag_remap_table_free (remap_right);
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    printf ("Burst output -> %s\n", burst_dir);

    int rc = burst_capture (camera, &cfg, burst_count, burst_dir, enc,
                             remap_left, remap_right, tag_size_m);

    g_free (burst_dir);
    arv_camera_stop_acquisition (camera, NULL);
    ag_remap_table_free (remap_left);
    ag_remap_table_free (remap_right);
    g_object_unref (cfg.stream);
    g_object_unref (camera);
    arv_shutdown ();
    return rc;
}

/* ------------------------------------------------------------------ */
/*  CLI entry point                                                    */
/* ------------------------------------------------------------------ */

int
cmd_capture (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) ctx;

    struct arg_str *cmd       = arg_str1 (NULL, NULL, "capture", NULL);
    struct arg_str *serial    = arg_str0 ("s", "serial",    "<serial>",
                                          "match by serial number");
    struct arg_str *address   = arg_str0 ("a", "address",   "<address>",
                                          "connect by camera IP");
    struct arg_str *interface = arg_str0 ("i", "interface",  "<iface>",
                                          "force NIC selection");
    struct arg_str *output    = arg_str0 ("o", "output",     "<dir>",
                                          "output directory (default: .)");
    struct arg_str *encode    = arg_str0 ("e", "encode",     "<format>",
                                          "output format: pgm, png, jpg (default: pgm)");
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
    struct arg_int *burst     = arg_int0 (NULL, "burst", "<N>",
                                          "burst capture N frames (2-100)");
    struct arg_str *calib_local = arg_str0 (NULL, "calibration-local", "<path>",
                                            "rectify using local calibration session");
    struct arg_int *calib_slot  = arg_int0 (NULL, "calibration-slot", "<0-2>",
                                            "rectify using on-camera calibration slot");
    struct arg_dbl *tag_size  = arg_dbl0 ("t", "tag-size",  "<meters>",
                                          "AprilTag size in meters (enables detection)");
    struct arg_lit *verbose   = arg_lit0 ("v", "verbose",
                                          "print diagnostic readback");
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (10);
    void *argtable[] = { cmd, serial, address, interface, output, encode,
                         exposure, gain, auto_exp, binning_a, pkt_size,
                         burst, calib_local, calib_slot,
                         tag_size, verbose, help, end };

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
    if (arg_make_syntax_err_help_msg (res, "capture", help->count, nerrors,
                                       argtable, end, &exitcode))
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
        arg_dstr_catf (res, "error: --auto-expose and --exposure/--gain are mutually exclusive\n");
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

    /* Validate burst count. */
    int burst_count = 0;
    if (burst->count) {
        burst_count = burst->ival[0];
        if (burst_count < AG_BURST_MIN || burst_count > AG_BURST_MAX) {
            arg_dstr_catf (res, "error: --burst must be between %d and %d\n",
                           AG_BURST_MIN, AG_BURST_MAX);
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }

    /* Validate calibration args (mutually exclusive). */
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

    if (calib_src.local_path)
        printf ("Rectification enabled (calibration from %s).\n",
                calib_src.local_path);
    else if (calib_src.slot >= 0)
        printf ("Rectification enabled (calibration from camera slot %d).\n",
                calib_src.slot);

    double tag_size_m = 0.0;
#ifdef HAVE_APRILTAG
    if (tag_size->count) {
        tag_size_m = tag_size->dval[0];
        if (tag_size_m <= 0.0) {
            arg_dstr_catf (res, "error: --tag-size must be positive\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }
#else
    if (tag_size->count)
        fprintf (stderr,
                 "warning: --tag-size ignored (compiled without AprilTag support)\n");
#endif

    /* Validate encode format. */
    AgEncFormat enc = AG_ENC_PGM;
    if (encode->count) {
        if (parse_enc_format (encode->sval[0], &enc) != 0) {
            arg_dstr_catf (res, "error: --encode must be 'pgm', 'png', or 'jpg'\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }

    const char *opt_serial    = serial->count    ? serial->sval[0]    : NULL;
    const char *opt_address   = address->count   ? address->sval[0]   : NULL;
    const char *opt_interface = interface->count  ? interface->sval[0] : NULL;
    const char *opt_output    = output->sval[0];

    if (g_mkdir_with_parents (opt_output, 0755) != 0) {
        arg_dstr_catf (res, "error: cannot create output directory '%s'\n",
                       opt_output);
        exitcode = EXIT_FAILURE;
        goto done;
    }

    int pkt_sz = pkt_size->count ? pkt_size->ival[0] : 0;

    if (burst_count > 0) {
        /* Burst path still resolves the device itself and uses raw
         * Aravis APIs — libagrippa does not yet support FrameBurstStart. */
        const char *iface_ip = NULL;
        if (opt_interface) {
            iface_ip = setup_interface (opt_interface);
            if (!iface_ip) { exitcode = EXIT_FAILURE; goto done; }
        }
        char *device_id = resolve_device (opt_serial, opt_address,
                                           opt_interface, TRUE);
        if (!device_id) { exitcode = EXIT_FAILURE; goto done; }
        exitcode = capture_burst_frames (device_id, opt_output, iface_ip,
                                          enc, exposure_us, gain_db,
                                          do_auto_expose, pkt_sz, binning,
                                          verbose->count > 0,
                                          &calib_src, burst_count,
                                          tag_size_m);
        g_free (device_id);
    } else {
        exitcode = capture_one_frame (opt_serial, opt_address, opt_interface,
                                       opt_output, enc,
                                       exposure_us, gain_db, do_auto_expose,
                                       pkt_sz, binning, verbose->count > 0,
                                       &calib_src, tag_size_m);
    }

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
