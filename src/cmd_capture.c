/*
 * cmd_capture.c — "ag-cam-tools capture" subcommand
 *
 * SingleFrame acquisition with software trigger.  Writes DualBayerRG8
 * stereo pairs to disk.
 */

#include "common.h"
#include "calib_load.h"
#include "image.h"
#include "../vendor/argtable3.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int
capture_one_frame (const char *device_id, const char *output_dir,
                   const char *iface_ip, AgEncFormat enc,
                   double exposure_us, double gain_db,
                   gboolean auto_expose, int packet_size, int binning,
                   gboolean verbose,
                   const AgCalibSource *calib_src)
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

    AgCameraConfig cfg;
    if (camera_configure (camera, AG_MODE_SINGLE_FRAME,
                          binning, exposure_us, gain_db, auto_expose,
                          packet_size, iface_ip, verbose, &cfg) != EXIT_SUCCESS) {
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

        /* Validate remap dimensions against processed frame size. */
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

    printf ("Starting acquisition...\n");
    arv_camera_start_acquisition (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to start acquisition: %s\n",
                 error->message);
        g_clear_error (&error);
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    if (auto_expose)
        auto_expose_settle (camera, &cfg, 100000.0);

    /* Wait for TriggerArmed. */
    {
        gboolean armed = FALSE;
        int polls = 0;
        while (!armed && polls < 100) {
            GError *e = NULL;
            armed = arv_device_get_boolean_feature_value (device, "TriggerArmed", &e);
            g_clear_error (&e);
            if (!armed) {
                g_usleep (10000);
                polls++;
            }
        }
        if (!armed)
            fprintf (stderr, "warn: TriggerArmed not set after %d polls, "
                     "triggering anyway\n", polls);
        else
            printf ("  TriggerArmed after %d poll(s)\n", polls);
    }

    /* Fire software trigger. */
    {
        GError *e = NULL;
        arv_device_execute_command (device, "TriggerSoftware", &e);
        if (e) {
            fprintf (stderr, "error: TriggerSoftware failed: %s\n", e->message);
            g_clear_error (&e);
        } else {
            printf ("  TriggerSoftware executed\n");
        }
    }

    ArvBuffer *buffer = NULL;
    ArvBuffer *partial_buf = NULL;

    for (int i = 0; i < 10; i++) {
        ArvBuffer *b = arv_stream_timeout_pop_buffer (cfg.stream, 5000000);
        if (!b) {
            printf ("  attempt %d: no buffer\n", i);
            continue;
        }
        ArvBufferStatus st = arv_buffer_get_status (b);
        if (st == ARV_BUFFER_STATUS_SUCCESS) {
            if (partial_buf) {
                arv_stream_push_buffer (cfg.stream, partial_buf);
                partial_buf = NULL;
            }
            buffer = b;
            break;
        }

        size_t bdata_sz = 0;
        arv_buffer_get_data (b, &bdata_sz);
        ArvBufferPayloadType bpt = arv_buffer_get_payload_type (b);
        guint bw = 0, bh = 0;
        if (bdata_sz > 0 &&
            (bpt == ARV_BUFFER_PAYLOAD_TYPE_IMAGE ||
             bpt == ARV_BUFFER_PAYLOAD_TYPE_EXTENDED_CHUNK_DATA)) {
            bw = arv_buffer_get_image_width (b);
            bh = arv_buffer_get_image_height (b);
        }
        printf ("  attempt %d: status=%d  payload=0x%x  frame_id=%" G_GUINT64_FORMAT
                "  recv=%zu bytes  %ux%u\n",
                i, (int) st, (unsigned) bpt,
                arv_buffer_get_frame_id (b), bdata_sz, bw, bh);

        if (partial_buf)
            arv_stream_push_buffer (cfg.stream, partial_buf);
        partial_buf = b;
    }

    if (!buffer) {
        fprintf (stderr, "error: timeout waiting for frame\n");

        /* Save partial data for debugging. */
        if (partial_buf) {
            size_t ps = 0;
            const guint8 *pd = (const guint8 *) arv_buffer_get_data (partial_buf, &ps);
            ArvBufferPayloadType ppt = arv_buffer_get_payload_type (partial_buf);
            guint pw = 0, ph = 0;
            if (ps > 0 &&
                (ppt == ARV_BUFFER_PAYLOAD_TYPE_IMAGE ||
                 ppt == ARV_BUFFER_PAYLOAD_TYPE_EXTENDED_CHUNK_DATA)) {
                pw = arv_buffer_get_image_width (partial_buf);
                ph = arv_buffer_get_image_height (partial_buf);
            }
            fprintf (stderr, "  partial frame: %ux%u  %zu bytes received\n", pw, ph, ps);
            if (pd && pw > 0 && ph > 0 && ps >= (size_t) pw * ph) {
                char *ppath = g_build_filename (output_dir, "partial_frame.pgm", NULL);
                if (write_pgm (ppath, pd, pw, ph) == EXIT_SUCCESS)
                    fprintf (stderr, "  partial frame saved -> %s\n", ppath);
                g_free (ppath);
            }
            arv_stream_push_buffer (cfg.stream, partial_buf);
        }

        if (ARV_IS_GV_STREAM (cfg.stream)) {
            guint64 n_completed = 0, n_failures = 0, n_underruns = 0;
            arv_stream_get_statistics (cfg.stream, &n_completed, &n_failures, &n_underruns);
            fprintf (stderr, "  stream stats: completed=%" G_GUINT64_FORMAT
                     " failures=%" G_GUINT64_FORMAT
                     " underruns=%" G_GUINT64_FORMAT "\n",
                     n_completed, n_failures, n_underruns);

            guint64 resent = 0, missing = 0;
            arv_gv_stream_get_statistics (ARV_GV_STREAM (cfg.stream), &resent, &missing);
            fprintf (stderr, "  gv stats:     resent=%" G_GUINT64_FORMAT
                     " missing=%" G_GUINT64_FORMAT "\n", resent, missing);
        }
        arv_camera_stop_acquisition (camera, NULL);
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    size_t data_size = 0;
    const guint8 *data = arv_buffer_get_data (buffer, &data_size);
    guint width  = arv_buffer_get_image_width (buffer);
    guint height = arv_buffer_get_image_height (buffer);
    size_t needed = (size_t) width * (size_t) height;

    int rc = EXIT_FAILURE;
    if (!data || data_size < needed) {
        fprintf (stderr, "error: unsupported frame buffer size (%zu bytes for %ux%u)\n",
                 data_size, width, height);
    } else {
        time_t now = time (NULL);
        struct tm tm_now;
        localtime_r (&now, &tm_now);
        char base[64];
        strftime (base, sizeof base, "capture_%Y%m%d_%H%M%S", &tm_now);

        const char *pixel_format = arv_device_get_string_feature_value (
                                       device, "PixelFormat", NULL);
        if (pixel_format && strcmp (pixel_format, "DualBayerRG8") == 0) {
            rc = write_dual_bayer_pair (output_dir, base, data, width, height,
                                        enc, cfg.software_binning,
                                        cfg.data_is_bayer,
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
    }

    arv_stream_push_buffer (cfg.stream, buffer);
    arv_camera_stop_acquisition (camera, NULL);
    ag_remap_table_free (remap_left);
    ag_remap_table_free (remap_right);
    g_object_unref (cfg.stream);
    g_object_unref (camera);
    arv_shutdown ();
    return rc;
}

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
    struct arg_str *calib_local = arg_str0 (NULL, "calibration-local", "<path>",
                                            "rectify using local calibration session");
    struct arg_int *calib_slot  = arg_int0 (NULL, "calibration-slot", "<0-2>",
                                            "rectify using on-camera calibration slot");
    struct arg_lit *verbose   = arg_lit0 ("v", "verbose",
                                          "print diagnostic readback");
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (10);
    void *argtable[] = { cmd, serial, address, interface, output, encode,
                         exposure, gain, auto_exp, binning_a, pkt_size,
                         calib_local, calib_slot,
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

    exitcode = capture_one_frame (device_id, opt_output, iface_ip, enc,
                                   exposure_us, gain_db, do_auto_expose,
                                   pkt_sz, binning, verbose->count > 0,
                                   &calib_src);
    g_free (device_id);

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
