/*
 * cmd_focus.c — "ag-cam-tools focus" subcommand
 *
 * Continuously captures software-triggered DualBayerRG8 frames, computes
 * a Variance-of-Laplacian focus score for each eye, and displays the
 * live stream with ROI overlay and score readout via SDL2.
 */

#include "common.h"
#include "focus.h"
#include "focus_audio.h"
#include "font.h"
#include "../vendor/argtable3.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

static volatile sig_atomic_t g_quit = 0;

#define AG_FOCUS_LOCK_THRESHOLD      0.05f
#define AG_FOCUS_LOCK_HOLD_SECONDS   1.0
#define AG_FOCUS_SCORE_AVG_FRAMES    5

static float
focus_normalized_delta (double score_left, double score_right)
{
    double scale = fmax (fmax (score_left, score_right), 1.0);
    double normalized = (score_right - score_left) / scale;

    if (normalized < -1.0)
        normalized = -1.0;
    if (normalized > 1.0)
        normalized = 1.0;
    return (float) normalized;
}

static void
sigint_handler (int sig)
{
    (void) sig;
    g_quit = 1;
}

static int
focus_loop (const char *device_id, const char *iface_ip,
            double fps, double exposure_us, double gain_db,
            gboolean auto_expose, int packet_size, int binning,
            int user_roi_x, int user_roi_y,
            int user_roi_w, int user_roi_h,
            int roi_specified, gboolean enable_audio)
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

    /* ROI: default to center 50%, or use user-specified values. */
    int roi_x, roi_y, roi_w, roi_h;
    if (roi_specified) {
        roi_x = user_roi_x;
        roi_y = user_roi_y;
        roi_w = user_roi_w;
        roi_h = user_roi_h;
    } else {
        roi_x = (int) proc_sub_w / 4;
        roi_y = (int) proc_h / 4;
        roi_w = (int) proc_sub_w / 2;
        roi_h = (int) proc_h / 2;
    }

    printf ("Focus ROI: x=%d y=%d w=%d h=%d (image %ux%u per eye)\n",
            roi_x, roi_y, roi_w, roi_h, proc_sub_w, proc_h);

    /* SDL2 setup. */
    if (SDL_Init (enable_audio ? (SDL_INIT_VIDEO | SDL_INIT_AUDIO)
                               : SDL_INIT_VIDEO) != 0) {
        fprintf (stderr, "error: SDL_Init: %s\n", SDL_GetError ());
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    SDL_Window *window = SDL_CreateWindow (
        "Focus Tool",
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

    if (enable_audio)
        focus_audio_init ();

    /* Scratch buffers. */
    guint8 *rgb_left        = g_malloc ((size_t) proc_sub_w * proc_h * 3);
    guint8 *rgb_right       = g_malloc ((size_t) proc_sub_w * proc_h * 3);
    guint8 *bayer_left_src  = g_malloc ((size_t) src_sub_w  * src_h);
    guint8 *bayer_right_src = g_malloc ((size_t) src_sub_w  * src_h);
    guint8 *bayer_left      = g_malloc ((size_t) proc_sub_w * proc_h);
    guint8 *bayer_right     = g_malloc ((size_t) proc_sub_w * proc_h);

    /* Start acquisition. */
    printf ("Starting focus at %.1f Hz...\n", fps);
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
    GTimer *stats_timer  = g_timer_new ();
    GTimer *stdout_timer = g_timer_new ();

    double score_left  = 0.0;
    double score_right = 0.0;
    double raw_score_left = 0.0;
    double raw_score_right = 0.0;
    double score_history_left[AG_FOCUS_SCORE_AVG_FRAMES] = { 0 };
    double score_history_right[AG_FOCUS_SCORE_AVG_FRAMES] = { 0 };
    double score_sum_left = 0.0;
    double score_sum_right = 0.0;
    int score_history_count = 0;
    int score_history_index = 0;
    float normalized_delta = 0.0f;
    double lock_stable_seconds = 0.0;
    gboolean focus_locked = FALSE;
    gint64 last_frame_time_us = g_get_monotonic_time ();

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

        /* Compute focus scores on raw bayer (before gamma). */
        raw_score_left = compute_focus_score (bayer_left,
                         (int) proc_sub_w, (int) proc_h,
                         roi_x, roi_y, roi_w, roi_h);
        raw_score_right = compute_focus_score (bayer_right,
                          (int) proc_sub_w, (int) proc_h,
                          roi_x, roi_y, roi_w, roi_h);

        if (score_history_count < AG_FOCUS_SCORE_AVG_FRAMES) {
            score_history_count++;
        } else {
            score_sum_left -= score_history_left[score_history_index];
            score_sum_right -= score_history_right[score_history_index];
        }

        score_history_left[score_history_index] = raw_score_left;
        score_history_right[score_history_index] = raw_score_right;
        score_sum_left += raw_score_left;
        score_sum_right += raw_score_right;
        score_history_index = (score_history_index + 1) % AG_FOCUS_SCORE_AVG_FRAMES;

        score_left = score_sum_left / (double) score_history_count;
        score_right = score_sum_right / (double) score_history_count;
        normalized_delta = focus_normalized_delta (score_left, score_right);
        if (enable_audio)
            focus_audio_update_delta (normalized_delta);

        {
            gint64 now_us = g_get_monotonic_time ();
            double frame_dt = (double) (now_us - last_frame_time_us) / 1000000.0;
            last_frame_time_us = now_us;

            if (fabsf (normalized_delta) < AG_FOCUS_LOCK_THRESHOLD) {
                lock_stable_seconds += frame_dt;
                if (lock_stable_seconds >= AG_FOCUS_LOCK_HOLD_SECONDS)
                    focus_locked = TRUE;
            } else {
                lock_stable_seconds = 0.0;
                focus_locked = FALSE;
            }
        }

        /* Gamma + debayer for display. */
        size_t eye_n = (size_t) proc_sub_w * (size_t) proc_h;
        apply_lut_inplace (bayer_left,  eye_n, gamma_lut);
        apply_lut_inplace (bayer_right, eye_n, gamma_lut);

        debayer_rg8_to_rgb (bayer_left,  rgb_left,  proc_sub_w, proc_h);
        debayer_rg8_to_rgb (bayer_right, rgb_right, proc_sub_w, proc_h);

        /* Upload to SDL texture. */
        void *tex_pixels;
        int tex_pitch;
        if (SDL_LockTexture (texture, NULL, &tex_pixels, &tex_pitch) == 0) {
            for (guint y = 0; y < proc_h; y++) {
                guint8 *dst = (guint8 *) tex_pixels + (size_t) y * (size_t) tex_pitch;
                memcpy (dst, rgb_left + (size_t) y * proc_sub_w * 3, proc_sub_w * 3);
                memcpy (dst + proc_sub_w * 3,
                        rgb_right + (size_t) y * proc_sub_w * 3, proc_sub_w * 3);
            }
            SDL_UnlockTexture (texture);
        }

        arv_stream_push_buffer (cfg.stream, buffer);

        SDL_RenderClear (renderer);
        SDL_RenderCopy (renderer, texture, NULL, NULL);

        /* Draw ROI rectangles and focus scores as overlay. */
        {
            int out_w, out_h;
            SDL_GetRendererOutputSize (renderer, &out_w, &out_h);
            double sx = (double) out_w / (double) display_w;
            double sy = (double) out_h / (double) display_h;

            /* ROI rectangle — left eye. */
            SDL_SetRenderDrawColor (renderer, 0, 255, 0, 255);
            SDL_Rect roi_left = {
                (int) (roi_x * sx),
                (int) (roi_y * sy),
                (int) (roi_w * sx),
                (int) (roi_h * sy)
            };
            SDL_RenderDrawRect (renderer, &roi_left);

            /* ROI rectangle — right eye (offset by proc_sub_w). */
            SDL_Rect roi_right = {
                (int) ((roi_x + (int) proc_sub_w) * sx),
                (int) (roi_y * sy),
                (int) (roi_w * sx),
                (int) (roi_h * sy)
            };
            SDL_RenderDrawRect (renderer, &roi_right);

            /* Focus score text overlay. */
            int font_scale = out_w > 1200 ? 3 : 2;
            int line_h = 7 * font_scale + 4;
            char buf[128];

            snprintf (buf, sizeof buf, "left: %.2f", score_left);
            ag_font_render (renderer, buf, 8, 8, font_scale, 0, 255, 0);

            snprintf (buf, sizeof buf, "right: %.2f", score_right);
            ag_font_render (renderer, buf, 8, 8 + line_h, font_scale, 0, 255, 0);

            double delta_pct = fabs ((double) normalized_delta) * 100.0;
            snprintf (buf, sizeof buf, "delta: %.1f%%", delta_pct);
            ag_font_render (renderer, buf, 8, 8 + line_h * 2, font_scale,
                            delta_pct > (AG_FOCUS_LOCK_THRESHOLD * 100.0) ? 255 : 0,
                            delta_pct > (AG_FOCUS_LOCK_THRESHOLD * 100.0) ? 100 : 255,
                            0);

            snprintf (buf, sizeof buf, "lock: %s", focus_locked ? "LOCKED" : "ALIGNING");
            ag_font_render (renderer, buf, 8, 8 + line_h * 3, font_scale,
                            focus_locked ? 0 : 255,
                            focus_locked ? 255 : 200,
                            0);
        }

        SDL_RenderPresent (renderer);

        frames_displayed++;

        /* Print to stdout periodically (~1/second). */
        double stdout_elapsed = g_timer_elapsed (stdout_timer, NULL);
        if (stdout_elapsed >= 1.0) {
            double delta = fabs (score_left - score_right);
            printf ("left: %.2f  right: %.2f  delta: %.2f\n",
                    score_left, score_right, delta);
            g_timer_start (stdout_timer);
        }

        /* FPS stats every 5 seconds. */
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
    g_timer_destroy (stdout_timer);
    printf ("\nStopping...\n");
    arv_camera_stop_acquisition (camera, NULL);

cleanup:
    g_free (bayer_left_src);
    g_free (bayer_right_src);
    g_free (bayer_left);
    g_free (bayer_right);
    g_free (rgb_left);
    g_free (rgb_right);
    if (enable_audio)
        focus_audio_shutdown ();
    SDL_DestroyTexture (texture);
    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit ();
    g_object_unref (cfg.stream);
    g_object_unref (camera);
    arv_shutdown ();
    return EXIT_SUCCESS;
}

int
cmd_focus (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) ctx;

    struct arg_str *cmd       = arg_str1 (NULL, NULL, "focus", NULL);
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
    struct arg_lit *quiet_audio = arg_lit0 ("q", "quiet-audio",
                                            "disable focus audio feedback");
    struct arg_int *roi_a     = arg_intn (NULL, "roi", "<x y w h>", 0, 4,
                                          "region of interest (default: center 50%%)");
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (10);

    void *argtable[] = { cmd, serial, address, interface, fps_a, exposure,
                         gain, auto_exp, binning_a, pkt_size, quiet_audio, roi_a,
                         help, end };

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
    if (arg_make_syntax_err_help_msg (res, "focus", help->count, nerrors,
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

    int roi_specified = 0;
    int uroi_x = 0, uroi_y = 0, uroi_w = 0, uroi_h = 0;
    if (roi_a->count == 4) {
        uroi_x = roi_a->ival[0];
        uroi_y = roi_a->ival[1];
        uroi_w = roi_a->ival[2];
        uroi_h = roi_a->ival[3];
        if (uroi_w <= 0 || uroi_h <= 0) {
            arg_dstr_catf (res, "error: --roi width and height must be positive\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
        roi_specified = 1;
    } else if (roi_a->count > 0 && roi_a->count != 4) {
        arg_dstr_catf (res, "error: --roi requires exactly 4 values: x y w h\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

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

    exitcode = focus_loop (device_id, iface_ip, fps, exposure_us, gain_db,
                           do_auto_expose, pkt_sz, binning,
                           uroi_x, uroi_y, uroi_w, uroi_h, roi_specified,
                           quiet_audio->count == 0);
    g_free (device_id);

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
