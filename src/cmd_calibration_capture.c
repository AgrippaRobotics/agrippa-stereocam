/*
 * cmd_calibration_capture.c — "ag-cam-tools calibration-capture" subcommand
 *
 * Interactive stereo pair capture for OpenCV calibration.  Streams a live
 * side-by-side preview via SDL2 and saves left/right PNG images on keypress
 * into stereoLeft/ and stereoRight/ directories, matching the layout expected
 * by 2.Calibration.ipynb.
 *
 * Binning defaults to 1 (1440×1080 per eye, full resolution) but can be
 * overridden with -b 2 (720×540).  Output is always colour PNG so that
 * the calibration notebook can consume the images without changes.
 */

#include "common.h"
#include "image.h"
#include "../vendor/argtable3.h"

#include <glib/gstdio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "beep_wav.h"

static volatile sig_atomic_t g_quit = 0;

/* Audio state for the capture confirmation beep. */
static SDL_AudioDeviceID g_audio_dev = 0;
static Uint8            *g_beep_buf  = NULL;
static Uint32            g_beep_len  = 0;

static void
audio_init (void)
{
    SDL_RWops *rw = SDL_RWFromConstMem (ag_beep_wav, (int) ag_beep_wav_len);
    if (!rw)
        return;

    SDL_AudioSpec spec;
    if (!SDL_LoadWAV_RW (rw, 1 /* free rw */, &spec, &g_beep_buf, &g_beep_len)) {
        fprintf (stderr, "warn: SDL_LoadWAV: %s\n", SDL_GetError ());
        return;
    }

    g_audio_dev = SDL_OpenAudioDevice (NULL, 0, &spec, NULL, 0);
    if (!g_audio_dev) {
        fprintf (stderr, "warn: SDL_OpenAudioDevice: %s\n", SDL_GetError ());
        SDL_FreeWAV (g_beep_buf);
        g_beep_buf = NULL;
        return;
    }

    /* Unpause so queued audio plays immediately. */
    SDL_PauseAudioDevice (g_audio_dev, 0);
}

static void
audio_play_beep (void)
{
    if (g_audio_dev && g_beep_buf) {
        SDL_ClearQueuedAudio (g_audio_dev);
        SDL_QueueAudio (g_audio_dev, g_beep_buf, g_beep_len);
    }
}

static void
audio_cleanup (void)
{
    if (g_audio_dev) {
        SDL_CloseAudioDevice (g_audio_dev);
        g_audio_dev = 0;
    }
    if (g_beep_buf) {
        SDL_FreeWAV (g_beep_buf);
        g_beep_buf = NULL;
    }
}

static void
sigint_handler (int sig)
{
    (void) sig;
    g_quit = 1;
}

static int
calibration_capture_loop (const char *device_id, const char *iface_ip,
                          const char *output_dir, int target_count,
                          double fps, double exposure_us, double gain_db,
                          gboolean auto_expose, int packet_size, int binning)
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

    /* Compute dimensions. */
    guint src_sub_w  = cfg.frame_w / 2;
    guint src_h      = cfg.frame_h;
    guint proc_sub_w = src_sub_w / (guint) cfg.software_binning;
    guint proc_h     = src_h     / (guint) cfg.software_binning;
    guint display_w  = proc_sub_w * 2;
    guint display_h  = proc_h;

    /* Create output directories. */
    char *left_dir  = g_build_filename (output_dir, "stereoLeft",  NULL);
    char *right_dir = g_build_filename (output_dir, "stereoRight", NULL);
    if (g_mkdir_with_parents (left_dir,  0755) != 0 ||
        g_mkdir_with_parents (right_dir, 0755) != 0) {
        fprintf (stderr, "error: cannot create output directories\n");
        g_free (left_dir);
        g_free (right_dir);
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    /* SDL2 setup (video + audio for capture beep). */
    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf (stderr, "error: SDL_Init: %s\n", SDL_GetError ());
        g_free (left_dir);
        g_free (right_dir);
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    char title[128];
    snprintf (title, sizeof title,
              "Calibration Capture [0/%d] — 's' save, 'q' quit", target_count);

    SDL_Window *window = SDL_CreateWindow (
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int) display_w, (int) display_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf (stderr, "error: SDL_CreateWindow: %s\n", SDL_GetError ());
        SDL_Quit ();
        g_free (left_dir);
        g_free (right_dir);
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
        g_free (left_dir);
        g_free (right_dir);
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
        g_free (left_dir);
        g_free (right_dir);
        g_object_unref (cfg.stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    /* Audio feedback for successful captures. */
    audio_init ();

    /* Scratch buffers. */
    guint8 *rgb_left        = g_malloc ((size_t) proc_sub_w * proc_h * 3);
    guint8 *rgb_right       = g_malloc ((size_t) proc_sub_w * proc_h * 3);
    guint8 *bayer_left_src  = g_malloc ((size_t) src_sub_w  * src_h);
    guint8 *bayer_right_src = g_malloc ((size_t) src_sub_w  * src_h);
    guint8 *bayer_left      = g_malloc ((size_t) proc_sub_w * proc_h);
    guint8 *bayer_right     = g_malloc ((size_t) proc_sub_w * proc_h);

    /* Start acquisition. */
    printf ("Starting acquisition at %.1f Hz...\n", fps);
    printf ("Resolution: %u×%u per eye (binning=%d)\n", proc_sub_w, proc_h,
            binning);
    printf ("Output: %s/ and %s/\n", left_dir, right_dir);
    printf ("Press 's' to save a pair, 'q' to quit.\n");
    printf ("Target: %d image pairs\n\n", target_count);

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

    const guint8 *gamma_lut = gamma_lut_2p5 ();
    int saved_count = 0;
    gboolean want_save = FALSE;

    while (!g_quit) {
        SDL_Event ev;
        while (SDL_PollEvent (&ev)) {
            if (ev.type == SDL_QUIT)
                g_quit = 1;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE ||
                    ev.key.keysym.sym == SDLK_q)
                    g_quit = 1;
                if (ev.key.keysym.sym == SDLK_s)
                    want_save = TRUE;
            }
        }
        if (g_quit)
            break;

        /* Wait for TriggerArmed. */
        {
            gboolean armed = FALSE;
            int polls = 0;
            while (!armed && polls < 50) {
                GError *e = NULL;
                armed = arv_device_get_boolean_feature_value (
                    device, "TriggerArmed", &e);
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
                g_clear_error (&e);
                g_usleep ((gulong) trigger_interval_us);
                continue;
            }
        }

        ArvBuffer *buffer = arv_stream_timeout_pop_buffer (cfg.stream, 500000);
        if (!buffer)
            continue;

        ArvBufferStatus st = arv_buffer_get_status (buffer);
        if (st != ARV_BUFFER_STATUS_SUCCESS) {
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

        /* Save pair if requested. */
        if (want_save) {
            want_save = FALSE;

            /* TODO: integrate a Vision Language Model to validate checkerboard
             * visibility, coverage, and pose diversity before accepting the
             * pair. */

            char *left_name  = g_strdup_printf ("imageL%d.png", saved_count);
            char *right_name = g_strdup_printf ("imageR%d.png", saved_count);
            char *left_path  = g_build_filename (left_dir,  left_name,  NULL);
            char *right_path = g_build_filename (right_dir, right_name, NULL);

            int rc_left  = write_color_image (AG_ENC_PNG, left_path,
                                              bayer_left,  proc_sub_w, proc_h);
            int rc_right = write_color_image (AG_ENC_PNG, right_path,
                                              bayer_right, proc_sub_w, proc_h);

            if (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS) {
                saved_count++;
                audio_play_beep ();
                printf ("  Saved pair %d / %d\n", saved_count, target_count);

                snprintf (title, sizeof title,
                          "Calibration Capture [%d/%d] — 's' save, 'q' quit",
                          saved_count, target_count);
                SDL_SetWindowTitle (window, title);

                if (saved_count >= target_count)
                    printf ("\n  Target reached! Press 'q' to finish "
                            "or 's' to capture more.\n\n");
            } else {
                fprintf (stderr, "  error: failed to save pair %d\n",
                         saved_count);
            }

            g_free (left_name);
            g_free (right_name);
            g_free (left_path);
            g_free (right_path);
        }

        /* Gamma-correct and debayer for display. */
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
                guint8 *dst = (guint8 *) tex_pixels
                              + (size_t) y * (size_t) tex_pitch;
                memcpy (dst, rgb_left + (size_t) y * proc_sub_w * 3,
                        proc_sub_w * 3);
                memcpy (dst + proc_sub_w * 3,
                        rgb_right + (size_t) y * proc_sub_w * 3,
                        proc_sub_w * 3);
            }
            SDL_UnlockTexture (texture);
        }

        arv_stream_push_buffer (cfg.stream, buffer);

        SDL_RenderClear (renderer);
        SDL_RenderCopy (renderer, texture, NULL, NULL);
        SDL_RenderPresent (renderer);

        g_usleep ((gulong) trigger_interval_us);
    }

    printf ("\nStopping...\n");
    arv_camera_stop_acquisition (camera, NULL);
    printf ("Captured %d image pairs.\n", saved_count);

    if (saved_count > 0)
        printf ("Open 2.Calibration.ipynb to continue.\n");

cleanup:
    g_free (bayer_left_src);
    g_free (bayer_right_src);
    g_free (bayer_left);
    g_free (bayer_right);
    g_free (rgb_left);
    g_free (rgb_right);
    g_free (left_dir);
    g_free (right_dir);
    audio_cleanup ();
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
cmd_calibration_capture (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) ctx;

    struct arg_str *cmd       = arg_str1 (NULL, NULL, "calibration-capture", NULL);
    struct arg_str *serial    = arg_str0 ("s", "serial",    "<serial>",
                                          "match by serial number");
    struct arg_str *address   = arg_str0 ("a", "address",   "<address>",
                                          "connect by camera IP");
    struct arg_str *interface = arg_str0 ("i", "interface",  "<iface>",
                                          "force NIC selection");
    struct arg_str *output    = arg_str0 ("o", "output",     "<dir>",
                                          "base output directory (default: .)");
    struct arg_int *count     = arg_int0 ("n", "count",      "<N>",
                                          "target number of pairs (default: 30)");
    struct arg_dbl *fps_a     = arg_dbl0 ("f", "fps",       "<rate>",
                                          "preview rate in Hz (default: 10)");
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
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (10);
    void *argtable[] = { cmd, serial, address, interface, output, count,
                         fps_a, exposure, gain, auto_exp, binning_a,
                         pkt_size, help, end };

    int exitcode = EXIT_SUCCESS;
    if (arg_nullcheck (argtable) != 0) {
        arg_dstr_catf (res, "error: insufficient memory\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Defaults. */
    output->sval[0]    = ".";
    count->ival[0]     = 30;
    fps_a->dval[0]     = 10.0;
    binning_a->ival[0] = 1;

    int nerrors = arg_parse (argc, argv, argtable);
    if (arg_make_syntax_err_help_msg (res, "calibration-capture", help->count,
                                       nerrors, argtable, end, &exitcode))
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
        arg_dstr_catf (res, "error: --auto-expose and --exposure/--gain "
                       "are mutually exclusive\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    int binning = binning_a->ival[0];
    if (binning != 1 && binning != 2) {
        arg_dstr_catf (res, "error: --binning must be 1 or 2\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    int target = count->ival[0];
    if (target <= 0) {
        arg_dstr_catf (res, "error: --count must be positive\n");
        exitcode = EXIT_FAILURE;
        goto done;
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

    char *device_id = resolve_device (opt_serial, opt_address,
                                       opt_interface, TRUE);
    if (!device_id) { exitcode = EXIT_FAILURE; goto done; }

    int pkt_sz = pkt_size->count ? pkt_size->ival[0] : 0;

    exitcode = calibration_capture_loop (device_id, iface_ip, opt_output,
                                          target, fps, exposure_us, gain_db,
                                          do_auto_expose, pkt_sz, binning);
    g_free (device_id);

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
