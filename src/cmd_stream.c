/*
 * cmd_stream.c — "ag-cam-tools stream" subcommand
 *
 * Continuously captures DualBayerRG8 frames, debayers each eye,
 * and displays them side-by-side in an SDL2 window.
 * Optionally detects AprilTags and estimates their pose.
 */

#include "common.h"
#include "calib_load.h"
#include "remap.h"
#include "../vendor/argtable3.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#ifdef HAVE_APRILTAG
#include <apriltag.h>
#include <apriltag_pose.h>
#include <tagStandard52h13.h>
#include <common/image_u8.h>
#endif

static volatile sig_atomic_t g_quit = 0;

static void
sigint_handler (int sig)
{
    (void) sig;
    g_quit = 1;
}

#ifdef HAVE_APRILTAG
/* IMX273 sensor: 3.45 µm pixel pitch, 3 mm lens. */
#define AG_PIXEL_PITCH_UM  3.45
#define AG_LENS_FL_UM      3000.0  /* 3 mm */

/* Corners of one detected tag (pixel coords within a single eye). */
typedef struct {
    double p[4][2];   /* corner points, counter-clockwise */
} TagOverlay;

#define MAX_TAG_OVERLAYS  32

static int
detect_tags_and_pose (apriltag_detector_t *detector, const guint8 *gray,
                      guint width, guint height, double tag_size_m,
                      double fx, double fy, double cx, double cy,
                      guint64 frame_num, const char *eye_label,
                      TagOverlay *overlays, int max_overlays)
{
    image_u8_t im = {
        .width  = (int32_t) width,
        .height = (int32_t) height,
        .stride = (int32_t) width,
        .buf    = (uint8_t *) gray
    };

    zarray_t *detections = apriltag_detector_detect (detector, &im);
    int n_overlays = 0;

    for (int i = 0; i < zarray_size (detections); i++) {
        apriltag_detection_t *det;
        zarray_get (detections, i, &det);

        apriltag_detection_info_t info = {
            .det     = det,
            .tagsize = tag_size_m,
            .fx      = fx,
            .fy      = fy,
            .cx      = cx,
            .cy      = cy
        };

        apriltag_pose_t pose;
        double pose_err = estimate_tag_pose (&info, &pose);

        printf ("apriltag frame=%" G_GUINT64_FORMAT
                " eye=%s id=%d hamming=%d margin=%.1f"
                " center=(%.1f,%.1f)"
                " err=%.2e"
                " R=[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]"
                " t=[%.4f,%.4f,%.4f]\n",
                frame_num, eye_label,
                det->id, det->hamming, det->decision_margin,
                det->c[0], det->c[1],
                pose_err,
                matd_get (pose.R, 0, 0), matd_get (pose.R, 0, 1),
                matd_get (pose.R, 0, 2), matd_get (pose.R, 1, 0),
                matd_get (pose.R, 1, 1), matd_get (pose.R, 1, 2),
                matd_get (pose.R, 2, 0), matd_get (pose.R, 2, 1),
                matd_get (pose.R, 2, 2),
                matd_get (pose.t, 0, 0), matd_get (pose.t, 1, 0),
                matd_get (pose.t, 2, 0));

        /* Store corner points for overlay rendering. */
        if (n_overlays < max_overlays) {
            for (int c = 0; c < 4; c++) {
                overlays[n_overlays].p[c][0] = det->p[c][0];
                overlays[n_overlays].p[c][1] = det->p[c][1];
            }
            n_overlays++;
        }

        matd_destroy (pose.R);
        matd_destroy (pose.t);
    }

    apriltag_detections_destroy (detections);
    return n_overlays;
}
#endif /* HAVE_APRILTAG */

static int
stream_loop (const char *device_id, const char *iface_ip,
             double fps, double exposure_us, double gain_db,
             gboolean auto_expose, int packet_size, int binning,
             double tag_size_m, const AgCalibSource *calib_src)
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
    if (camera_configure (camera, AG_MODE_CONTINUOUS,
                          binning, exposure_us, gain_db, auto_expose,
                          packet_size, iface_ip, FALSE, &cfg) != EXIT_SUCCESS) {
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);

    /* Compute display dimensions. */
    guint src_sub_w  = cfg.frame_w / 2;
    guint src_h      = cfg.frame_h;
    guint proc_sub_w = src_sub_w / (guint) cfg.software_binning;
    guint proc_h     = src_h     / (guint) cfg.software_binning;
    guint display_w  = proc_sub_w * 2;
    guint display_h  = proc_h;

#ifdef HAVE_APRILTAG
    /* AprilTag detector setup. */
    apriltag_detector_t *at_detector = NULL;
    apriltag_family_t   *at_family   = NULL;
    double at_fx = 0, at_fy = 0, at_cx = 0, at_cy = 0;

    if (tag_size_m > 0.0) {
        at_family   = tagStandard52h13_create ();
        at_detector = apriltag_detector_create ();
        apriltag_detector_add_family (at_detector, at_family);

        at_detector->quad_decimate   = 1.5f;
        at_detector->quad_sigma      = 0.0f;
        at_detector->nthreads        = 1;
        at_detector->refine_edges    = 1;
        at_detector->decode_sharpening = 0.25;

        double total_bin = (double) (AG_SENSOR_WIDTH / 2) / (double) proc_sub_w;
        at_fx = AG_LENS_FL_UM / (AG_PIXEL_PITCH_UM * total_bin);
        at_fy = at_fx;
        at_cx = (double) proc_sub_w / 2.0;
        at_cy = (double) proc_h / 2.0;

        printf ("AprilTag: tagStandard52h13, tag_size=%.3f m, "
                "fx=%.1f fy=%.1f cx=%.1f cy=%.1f\n",
                tag_size_m, at_fx, at_fy, at_cx, at_cy);
    }
#else
    (void) tag_size_m;
#endif

    /* SDL2 setup. */
    if (SDL_Init (SDL_INIT_VIDEO) != 0) {
        fprintf (stderr, "error: SDL_Init: %s\n", SDL_GetError ());
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    SDL_Window *window = SDL_CreateWindow (
        "Stereo Stream",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int) display_w, (int) display_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf (stderr, "error: SDL_CreateWindow: %s\n", SDL_GetError ());
        SDL_Quit ();
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer (
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer (window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf (stderr, "error: SDL_CreateRenderer: %s\n", SDL_GetError ());
        SDL_DestroyWindow (window);
        SDL_Quit ();
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    SDL_Texture *texture = SDL_CreateTexture (
        renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        (int) display_w, (int) display_h);
    if (!texture) {
        fprintf (stderr, "error: SDL_CreateTexture: %s\n", SDL_GetError ());
        SDL_DestroyRenderer (renderer);
        SDL_DestroyWindow (window);
        SDL_Quit ();
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    /* Scratch buffers. */
    guint8 *rgb_left        = g_malloc ((size_t) proc_sub_w * proc_h * 3);
    guint8 *rgb_right       = g_malloc ((size_t) proc_sub_w * proc_h * 3);
    guint8 *bayer_left_src  = g_malloc ((size_t) src_sub_w  * src_h);
    guint8 *bayer_right_src = g_malloc ((size_t) src_sub_w  * src_h);
    guint8 *bayer_left      = g_malloc ((size_t) proc_sub_w * proc_h);
    guint8 *bayer_right     = g_malloc ((size_t) proc_sub_w * proc_h);

    /* Load rectification remap tables (optional). */
    AgRemapTable *remap_left  = NULL;
    AgRemapTable *remap_right = NULL;
    guint8 *rect_left  = NULL;
    guint8 *rect_right = NULL;

    if (calib_src->local_path || calib_src->slot >= 0) {
        if (ag_calib_load (device, calib_src,
                            &remap_left, &remap_right, NULL) != 0)
            goto cleanup;

        if (remap_left->width != proc_sub_w || remap_left->height != proc_h) {
            fprintf (stderr,
                     "error: remap dimensions %ux%u do not match frame %ux%u\n",
                     remap_left->width, remap_left->height,
                     proc_sub_w, proc_h);
            goto cleanup;
        }

        rect_left  = g_malloc ((size_t) proc_sub_w * proc_h * 3);
        rect_right = g_malloc ((size_t) proc_sub_w * proc_h * 3);
        printf ("Rectification enabled (%ux%u maps loaded).\n",
                proc_sub_w, proc_h);
    }

    /* Pointers used for SDL upload — either raw or rectified. */
    const guint8 *display_left  = remap_left ? rect_left  : rgb_left;
    const guint8 *display_right = remap_left ? rect_right : rgb_right;

    /* Start acquisition. */
    printf ("Starting acquisition at %.1f Hz...\n", fps);
    arv_camera_start_acquisition (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to start acquisition: %s\n",
                 error->message);
        g_clear_error (&error);
        goto cleanup;
    }

    signal (SIGINT, sigint_handler);

    guint64 trigger_interval_us = (guint64) (1000000.0 / fps);

    if (auto_expose)
        auto_expose_settle (camera, &cfg, (double) trigger_interval_us);

    guint64 frames_displayed = 0;
    guint64 frames_dropped   = 0;
    const guint8 *gamma_lut  = gamma_lut_2p5 ();
    GTimer *stats_timer = g_timer_new ();

    while (!g_quit) {
        SDL_Event ev;
        while (SDL_PollEvent (&ev)) {
            if (ev.type == SDL_QUIT)
                g_quit = 1;
            if (ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q))
                g_quit = 1;
        }
        if (g_quit)
            break;

        /* Wait for TriggerArmed. */
        {
            gboolean armed = FALSE;
            int polls = 0;
            while (!armed && polls < 50) {
                GError *e = NULL;
                armed = arv_device_get_boolean_feature_value (device, "TriggerArmed", &e);
                g_clear_error (&e);
                if (!armed) {
                    g_usleep (2000);
                    polls++;
                }
            }
            if (!armed) {
                g_usleep ((gulong) trigger_interval_us);
                continue;
            }
        }

        /* Fire software trigger. */
        {
            GError *e = NULL;
            arv_device_execute_command (device, "TriggerSoftware", &e);
            if (e) {
                fprintf (stderr, "warn: TriggerSoftware failed: %s\n", e->message);
                g_clear_error (&e);
                g_usleep ((gulong) trigger_interval_us);
                continue;
            }
        }

        ArvBuffer *buffer = arv_stream_timeout_pop_buffer (cfg.stream, 500000);
        if (!buffer) {
            frames_dropped++;
            continue;
        }

        ArvBufferStatus st = arv_buffer_get_status (buffer);
        if (st != ARV_BUFFER_STATUS_SUCCESS) {
            frames_dropped++;
            arv_stream_push_buffer (cfg.stream, buffer);
            continue;
        }

        size_t data_size = 0;
        const guint8 *data = arv_buffer_get_data (buffer, &data_size);
        guint w = arv_buffer_get_image_width (buffer);
        guint h = arv_buffer_get_image_height (buffer);
        size_t needed = (size_t) w * (size_t) h;

        if (!data || data_size < needed || w % 2 != 0 ||
            w != cfg.frame_w || h != cfg.frame_h) {
            frames_dropped++;
            arv_stream_push_buffer (cfg.stream, buffer);
            continue;
        }

        /* Deinterleave DualBayer. */
        deinterleave_dual_bayer (data, w, h, bayer_left_src, bayer_right_src);

        if (cfg.software_binning > 1) {
            software_bin_2x2 (bayer_left_src,  src_sub_w, src_h,
                              bayer_left,  proc_sub_w, proc_h);
            software_bin_2x2 (bayer_right_src, src_sub_w, src_h,
                              bayer_right, proc_sub_w, proc_h);
        } else {
            memcpy (bayer_left,  bayer_left_src,  (size_t) src_sub_w * src_h);
            memcpy (bayer_right, bayer_right_src, (size_t) src_sub_w * src_h);
        }

#ifdef HAVE_APRILTAG
        /* Detect tags on raw grayscale (before gamma), both eyes. */
        int n_left_tags = 0, n_right_tags = 0;
        TagOverlay left_tags[MAX_TAG_OVERLAYS], right_tags[MAX_TAG_OVERLAYS];
        if (at_detector) {
            n_left_tags = detect_tags_and_pose (
                at_detector, bayer_left,
                proc_sub_w, proc_h, tag_size_m,
                at_fx, at_fy, at_cx, at_cy,
                frames_displayed, "left",
                left_tags, MAX_TAG_OVERLAYS);
            n_right_tags = detect_tags_and_pose (
                at_detector, bayer_right,
                proc_sub_w, proc_h, tag_size_m,
                at_fx, at_fy, at_cx, at_cy,
                frames_displayed, "right",
                right_tags, MAX_TAG_OVERLAYS);
        }
#endif

        size_t eye_n = (size_t) proc_sub_w * (size_t) proc_h;
        apply_lut_inplace (bayer_left,  eye_n, gamma_lut);
        apply_lut_inplace (bayer_right, eye_n, gamma_lut);

        if (cfg.data_is_bayer) {
            debayer_rg8_to_rgb (bayer_left,  rgb_left,  proc_sub_w, proc_h);
            debayer_rg8_to_rgb (bayer_right, rgb_right, proc_sub_w, proc_h);
        } else {
            gray_to_rgb_replicate (bayer_left,  rgb_left,  (uint32_t) eye_n);
            gray_to_rgb_replicate (bayer_right, rgb_right, (uint32_t) eye_n);
        }

        if (remap_left) {
            ag_remap_rgb (remap_left,  rgb_left,  rect_left);
            ag_remap_rgb (remap_right, rgb_right, rect_right);
        }

        /* Upload to SDL texture. */
        void *tex_pixels;
        int tex_pitch;
        if (SDL_LockTexture (texture, NULL, &tex_pixels, &tex_pitch) == 0) {
            for (guint y = 0; y < proc_h; y++) {
                guint8 *dst = (guint8 *) tex_pixels + (size_t) y * (size_t) tex_pitch;
                memcpy (dst,
                        display_left + (size_t) y * proc_sub_w * 3,
                        proc_sub_w * 3);
                memcpy (dst + proc_sub_w * 3,
                        display_right + (size_t) y * proc_sub_w * 3,
                        proc_sub_w * 3);
            }
            SDL_UnlockTexture (texture);
        }

        arv_stream_push_buffer (cfg.stream, buffer);

        SDL_RenderClear (renderer);
        SDL_RenderCopy (renderer, texture, NULL, NULL);

#ifdef HAVE_APRILTAG
        /* Draw detected tag outlines as quadrilaterals.
         * Tag corner coords are in per-eye pixel space; we must map them
         * to the renderer's logical output which may be scaled by the
         * window size.  SDL_RenderCopy stretches the texture to fill the
         * output, so we apply the same scale. */
        if (n_left_tags || n_right_tags) {
            int out_w, out_h;
            SDL_GetRendererOutputSize (renderer, &out_w, &out_h);
            double sx = (double) out_w / (double) display_w;
            double sy = (double) out_h / (double) display_h;

            SDL_SetRenderDrawColor (renderer, 0, 255, 0, 255);

            for (int t = 0; t < n_left_tags; t++) {
                for (int c = 0; c < 4; c++) {
                    int nc = (c + 1) % 4;
                    SDL_RenderDrawLine (renderer,
                        (int) (left_tags[t].p[c][0]  * sx),
                        (int) (left_tags[t].p[c][1]  * sy),
                        (int) (left_tags[t].p[nc][0] * sx),
                        (int) (left_tags[t].p[nc][1] * sy));
                }
            }

            for (int t = 0; t < n_right_tags; t++) {
                double x_off = (double) proc_sub_w;
                for (int c = 0; c < 4; c++) {
                    int nc = (c + 1) % 4;
                    SDL_RenderDrawLine (renderer,
                        (int) ((right_tags[t].p[c][0]  + x_off) * sx),
                        (int) ( right_tags[t].p[c][1]           * sy),
                        (int) ((right_tags[t].p[nc][0] + x_off) * sx),
                        (int) ( right_tags[t].p[nc][1]          * sy));
                }
            }
        }
#endif

        SDL_RenderPresent (renderer);

        frames_displayed++;

        double elapsed = g_timer_elapsed (stats_timer, NULL);
        if (elapsed >= 5.0) {
            printf ("  %.1f fps (displayed=%" G_GUINT64_FORMAT
                    " dropped=%" G_GUINT64_FORMAT ")\n",
                    frames_displayed / elapsed, frames_displayed, frames_dropped);
            frames_displayed = 0;
            frames_dropped = 0;
            g_timer_start (stats_timer);
        }

        g_usleep ((gulong) trigger_interval_us);
    }

    g_timer_destroy (stats_timer);
    printf ("\nStopping...\n");
    arv_camera_stop_acquisition (camera, NULL);

cleanup:
    g_free (rect_left);
    g_free (rect_right);
    ag_remap_table_free (remap_left);
    ag_remap_table_free (remap_right);
    g_free (bayer_left_src);
    g_free (bayer_right_src);
    g_free (bayer_left);
    g_free (bayer_right);
    g_free (rgb_left);
    g_free (rgb_right);
    SDL_DestroyTexture (texture);
    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit ();
#ifdef HAVE_APRILTAG
    if (at_detector) {
        apriltag_detector_destroy (at_detector);
        tagStandard52h13_destroy (at_family);
    }
#endif
    g_object_unref (cfg.stream);
    g_object_unref (camera);
    arv_shutdown ();
    return EXIT_SUCCESS;
}

int
cmd_stream (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) ctx;

    struct arg_str *cmd       = arg_str1 (NULL, NULL, "stream", NULL);
    struct arg_str *serial    = arg_str0 ("s", "serial",    "<serial>",
                                          "match by serial number");
    struct arg_str *address   = arg_str0 ("a", "address",   "<address>",
                                          "connect by camera IP");
    struct arg_str *interface = arg_str0 ("i", "interface",  "<iface>",
                                          "force NIC selection");
    struct arg_dbl *fps_a     = arg_dbl0 ("f", "fps",       "<rate>",
                                          "trigger rate in Hz (default: 10)");
    struct arg_dbl *exposure  = arg_dbl0 ("x", "exposure",  "<us>",
                                          "exposure time in microseconds");
    struct arg_dbl *gain      = arg_dbl0 ("g", "gain",      "<dB>",
                                          "sensor gain in dB (0-48)");
    struct arg_lit *auto_exp  = arg_lit0 ("A", "auto-expose",
                                          "auto-expose then lock");
    struct arg_int *binning_a = arg_int0 ("b", "binning",   "<1|2>",
                                          "sensor binning factor (default: 1)");
    struct arg_int *pkt_size  = arg_int0 ("p", "packet-size", "<bytes>",
                                          "GigE packet size (default: auto-negotiate)");
    struct arg_str *calib_local = arg_str0 (NULL, "calibration-local", "<path>",
                                            "rectify using local calibration session");
    struct arg_int *calib_slot  = arg_int0 (NULL, "calibration-slot", "<0-2>",
                                            "rectify using on-camera calibration slot");
#ifdef HAVE_APRILTAG
    struct arg_dbl *tag_size  = arg_dbl0 ("t", "tag-size",  "<meters>",
                                          "AprilTag size in meters (enables detection)");
#endif
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (10);

#ifdef HAVE_APRILTAG
    void *argtable[] = { cmd, serial, address, interface, fps_a, exposure,
                         gain, auto_exp, binning_a, pkt_size,
                         calib_local, calib_slot,
                         tag_size, help, end };
#else
    void *argtable[] = { cmd, serial, address, interface, fps_a, exposure,
                         gain, auto_exp, binning_a, pkt_size,
                         calib_local, calib_slot,
                         help, end };
#endif

    int exitcode = EXIT_SUCCESS;
    if (arg_nullcheck (argtable) != 0) {
        arg_dstr_catf (res, "error: insufficient memory\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Defaults. */
    fps_a->dval[0]     = 10.0;
    binning_a->ival[0] = 1;

    int nerrors = arg_parse (argc, argv, argtable);
    if (arg_make_syntax_err_help_msg (res, "stream", help->count, nerrors,
                                       argtable, end, &exitcode))
        goto done;

    if (serial->count && address->count) {
        arg_dstr_catf (res, "error: --serial and --address are mutually exclusive\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    double fps = fps_a->dval[0];
    if (fps <= 0.0 || fps > 120.0) {
        arg_dstr_catf (res, "error: --fps must be between 0 and 120\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

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
#endif

    const char *opt_serial    = serial->count    ? serial->sval[0]    : NULL;
    const char *opt_address   = address->count   ? address->sval[0]   : NULL;
    const char *opt_interface = interface->count  ? interface->sval[0] : NULL;

    const char *iface_ip = NULL;
    if (opt_interface) {
        iface_ip = setup_interface (opt_interface);
        if (!iface_ip) { exitcode = EXIT_FAILURE; goto done; }
    }

    char *device_id = resolve_device (opt_serial, opt_address,
                                       opt_interface, TRUE);
    if (!device_id) { exitcode = EXIT_FAILURE; goto done; }

    int pkt_sz = pkt_size->count ? pkt_size->ival[0] : 0;

    exitcode = stream_loop (device_id, iface_ip, fps, exposure_us, gain_db,
                            do_auto_expose, pkt_sz, binning, tag_size_m,
                            &calib_src);
    g_free (device_id);

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
