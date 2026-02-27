/*
 * cmd_depth_preview.c — depth preview subcommands
 *
 * Live stereo depth preview: acquires rectified stereo frames, computes
 * disparity via a selectable backend (StereoSGBM, IGEV++, FoundationStereo),
 * and displays the rectified left eye alongside a JET-coloured disparity map.
 */

#include "common.h"
#include "remap.h"
#include "stereo.h"
#include "../vendor/argtable3.h"
#include "../vendor/cJSON.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

static volatile sig_atomic_t g_quit = 0;

static void
sigint_handler (int sig)
{
    (void) sig;
    g_quit = 1;
}

/* ------------------------------------------------------------------ */
/*  Calibration metadata loader                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    int    min_disparity;
    int    num_disparities;
    double focal_length_px;
    double baseline_cm;
} CalibMeta;

static int
load_calibration_meta (const char *session_path, CalibMeta *out)
{
    char *json_path = g_build_filename (session_path, "calib_result",
                                         "calibration_meta.json", NULL);
    gchar *contents = NULL;
    gsize  length   = 0;
    GError *error   = NULL;

    if (!g_file_get_contents (json_path, &contents, &length, &error)) {
        fprintf (stderr, "warn: cannot read %s: %s\n",
                 json_path, error ? error->message : "unknown error");
        g_clear_error (&error);
        g_free (json_path);
        return -1;
    }
    g_free (json_path);

    cJSON *root = cJSON_ParseWithLength (contents, length);
    g_free (contents);

    if (!root) {
        fprintf (stderr, "warn: failed to parse calibration_meta.json\n");
        return -1;
    }

    /* disparity_range object. */
    cJSON *dr = cJSON_GetObjectItemCaseSensitive (root, "disparity_range");
    if (dr) {
        cJSON *md = cJSON_GetObjectItemCaseSensitive (dr, "min_disparity");
        cJSON *nd = cJSON_GetObjectItemCaseSensitive (dr, "num_disparities");
        if (cJSON_IsNumber (md)) out->min_disparity   = md->valueint;
        if (cJSON_IsNumber (nd)) out->num_disparities  = nd->valueint;
    }

    cJSON *fl = cJSON_GetObjectItemCaseSensitive (root, "focal_length_px");
    if (cJSON_IsNumber (fl)) out->focal_length_px = fl->valuedouble;

    cJSON *bl = cJSON_GetObjectItemCaseSensitive (root, "baseline_cm");
    if (cJSON_IsNumber (bl)) out->baseline_cm = bl->valuedouble;

    cJSON_Delete (root);
    return 0;
}

static int
clamp_int (int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int
normalize_num_disparities (int v)
{
    if (v < 16)
        v = 16;
    return ((v + 15) / 16) * 16;
}

static int
normalize_block_size (int v)
{
    if (v < 1)
        v = 1;
    if ((v & 1) == 0)
        v += 1;
    if (v > 255)
        v = 255;
    if ((v & 1) == 0)
        v -= 1;
    return v;
}

static int
sgbm_effective_p1 (const AgSgbmParams *p)
{
    if (p->p1 > 0)
        return p->p1;
    return 8 * p->block_size * p->block_size;
}

static int
sgbm_effective_p2 (const AgSgbmParams *p)
{
    if (p->p2 > 0)
        return p->p2;
    return 32 * p->block_size * p->block_size;
}

static void
print_sgbm_params (const AgSgbmParams *p)
{
    printf ("SGBM params: min=%d num=%d block=%d P1=%d%s P2=%d%s uniq=%d "
            "speckleWin=%d speckleRange=%d preCap=%d disp12=%d mode=%d\n",
            p->min_disparity, p->num_disparities, p->block_size,
            sgbm_effective_p1 (p), p->p1 == 0 ? " (auto)" : "",
            sgbm_effective_p2 (p), p->p2 == 0 ? " (auto)" : "",
            p->uniqueness_ratio, p->speckle_window_size,
            p->speckle_range, p->pre_filter_cap,
            p->disp12_max_diff, p->mode);
}

static void
print_sgbm_controls (void)
{
    printf ("Live SGBM controls:\n"
            "  [ / ] block-size -/+2 (odd)\n"
            "  ; / ' min-disparity -/+1\n"
            "  - / = num-disparities -/+16\n"
            "  z / x P1 -/+100 (explicit)\n"
            "  c / v P2 -/+100 (explicit)\n"
            "  r     reset P1/P2 to auto\n"
            "  u / i uniqueness-ratio -/+1\n"
            "  j / k speckle-window-size -/+10\n"
            "  n / m speckle-range -/+1\n"
            "  h / l pre-filter-cap -/+1\n"
            "  , / . disp12-max-diff -/+1\n"
            "  9 / 0 mode -/+1\n"
            "  p     print current params\n");
}

/* ------------------------------------------------------------------ */
/*  Depth preview loop                                                 */
/* ------------------------------------------------------------------ */

static int
depth_preview_loop (const char *device_id, const char *iface_ip,
                    double fps, double exposure_us, double gain_db,
                    gboolean auto_expose, int packet_size, int binning,
                    const char *rectify_path, AgStereoBackend backend,
                    AgSgbmParams *sgbm_params, const AgOnnxParams *onnx_params,
                    gboolean enable_runtime_tuning)
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

    /* Compute processing dimensions. */
    guint src_sub_w  = cfg.frame_w / 2;
    guint src_h      = cfg.frame_h;
    guint proc_sub_w = src_sub_w / (guint) cfg.software_binning;
    guint proc_h     = src_h     / (guint) cfg.software_binning;
    guint display_w  = proc_sub_w * 2;    /* left eye + disparity side-by-side */
    guint display_h  = proc_h;

    /* Load remap tables (required for depth). */
    char *lpath = g_build_filename (rectify_path, "calib_result",
                                     "remap_left.bin", NULL);
    char *rpath = g_build_filename (rectify_path, "calib_result",
                                     "remap_right.bin", NULL);
    AgRemapTable *remap_left  = ag_remap_table_load (lpath);
    AgRemapTable *remap_right = ag_remap_table_load (rpath);
    g_free (lpath);
    g_free (rpath);

    if (!remap_left || !remap_right) {
        fprintf (stderr, "error: failed to load remap tables from %s\n",
                 rectify_path);
        goto cleanup;
    }
    if (remap_left->width != proc_sub_w || remap_left->height != proc_h) {
        fprintf (stderr, "error: remap dimensions %ux%u do not match frame %ux%u\n",
                 remap_left->width, remap_left->height, proc_sub_w, proc_h);
        goto cleanup;
    }

    printf ("Rectification maps loaded (%ux%u).\n", proc_sub_w, proc_h);

    /* Create disparity backend. */
    AgDisparityContext *disp_ctx = ag_disparity_create (
        backend, proc_sub_w, proc_h, sgbm_params, onnx_params);
    if (!disp_ctx) {
        fprintf (stderr, "error: failed to create %s backend\n",
                 ag_stereo_backend_name (backend));
        goto cleanup;
    }

    printf ("Stereo backend: %s\n", ag_stereo_backend_name (backend));
    if (enable_runtime_tuning && backend == AG_STEREO_SGBM) {
        print_sgbm_controls ();
        print_sgbm_params (sgbm_params);
    }

    /* SDL2 setup. */
    if (SDL_Init (SDL_INIT_VIDEO) != 0) {
        fprintf (stderr, "error: SDL_Init: %s\n", SDL_GetError ());
        ag_disparity_destroy (disp_ctx);
        goto cleanup;
    }

    SDL_Window *window = SDL_CreateWindow (
        "Depth Preview",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int) display_w, (int) display_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf (stderr, "error: SDL_CreateWindow: %s\n", SDL_GetError ());
        SDL_Quit ();
        ag_disparity_destroy (disp_ctx);
        goto cleanup;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer (
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer (window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf (stderr, "error: SDL_CreateRenderer: %s\n", SDL_GetError ());
        SDL_DestroyWindow (window);
        SDL_Quit ();
        ag_disparity_destroy (disp_ctx);
        goto cleanup;
    }

    SDL_Texture *texture = SDL_CreateTexture (
        renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        (int) display_w, (int) display_h);
    if (!texture) {
        fprintf (stderr, "error: SDL_CreateTexture: %s\n", SDL_GetError ());
        SDL_DestroyRenderer (renderer);
        SDL_DestroyWindow (window);
        SDL_Quit ();
        ag_disparity_destroy (disp_ctx);
        goto cleanup;
    }

    /* Scratch buffers. */
    size_t eye_pixels = (size_t) proc_sub_w * proc_h;
    size_t eye_rgb    = eye_pixels * 3;

    guint8 *bayer_left_src  = g_malloc (src_sub_w * src_h);
    guint8 *bayer_right_src = g_malloc (src_sub_w * src_h);
    guint8 *bayer_left      = g_malloc (eye_pixels);
    guint8 *bayer_right     = g_malloc (eye_pixels);

    /* Display path: gamma → debayer → remap RGB. */
    guint8 *rgb_left   = g_malloc (eye_rgb);
    guint8 *rgb_right  = g_malloc (eye_rgb);
    guint8 *rect_rgb_l = g_malloc (eye_rgb);

    /* Disparity path: debayer (no gamma) → rgb_to_gray → remap gray. */
    guint8 *rgb_nogamma_l = g_malloc (eye_rgb);
    guint8 *rgb_nogamma_r = g_malloc (eye_rgb);
    guint8 *gray_left     = g_malloc (eye_pixels);
    guint8 *gray_right    = g_malloc (eye_pixels);
    guint8 *rect_gray_l   = g_malloc (eye_pixels);
    guint8 *rect_gray_r   = g_malloc (eye_pixels);

    /* Disparity output. */
    int16_t *disparity_buf = g_malloc (eye_pixels * sizeof (int16_t));
    guint8  *disparity_rgb = g_malloc (eye_rgb);

    /* Start acquisition. */
    printf ("Starting acquisition at %.1f Hz...\n", fps);
    arv_camera_start_acquisition (camera, &error);
    if (error) {
        fprintf (stderr, "error: start acquisition: %s\n", error->message);
        g_clear_error (&error);
        goto cleanup_sdl;
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
            if (ev.type == SDL_KEYDOWN &&
                enable_runtime_tuning &&
                backend == AG_STEREO_SGBM) {
                SDL_Keycode sym = ev.key.keysym.sym;
                gboolean changed = FALSE;
                gboolean print_only = FALSE;
                AgSgbmParams next = *sgbm_params;

                switch (sym) {
                case SDLK_LEFTBRACKET:
                    next.block_size = normalize_block_size (next.block_size - 2);
                    changed = TRUE;
                    break;
                case SDLK_RIGHTBRACKET:
                    next.block_size = normalize_block_size (next.block_size + 2);
                    changed = TRUE;
                    break;
                case SDLK_SEMICOLON:
                    next.min_disparity -= 1;
                    changed = TRUE;
                    break;
                case SDLK_QUOTE:
                    next.min_disparity += 1;
                    changed = TRUE;
                    break;
                case SDLK_MINUS:
                    next.num_disparities = normalize_num_disparities (
                        next.num_disparities - 16);
                    changed = TRUE;
                    break;
                case SDLK_EQUALS:
                    next.num_disparities = normalize_num_disparities (
                        next.num_disparities + 16);
                    changed = TRUE;
                    break;
                case SDLK_z:
                    next.p1 = clamp_int (sgbm_effective_p1 (sgbm_params) - 100,
                                         0, 2000000);
                    changed = TRUE;
                    break;
                case SDLK_x:
                    next.p1 = clamp_int (sgbm_effective_p1 (sgbm_params) + 100,
                                         0, 2000000);
                    changed = TRUE;
                    break;
                case SDLK_c:
                    next.p2 = clamp_int (sgbm_effective_p2 (sgbm_params) - 100,
                                         0, 2000000);
                    changed = TRUE;
                    break;
                case SDLK_v:
                    next.p2 = clamp_int (sgbm_effective_p2 (sgbm_params) + 100,
                                         0, 2000000);
                    changed = TRUE;
                    break;
                case SDLK_r:
                    next.p1 = 0;
                    next.p2 = 0;
                    changed = TRUE;
                    break;
                case SDLK_u:
                    next.uniqueness_ratio = clamp_int (next.uniqueness_ratio - 1,
                                                       0, 100);
                    changed = TRUE;
                    break;
                case SDLK_i:
                    next.uniqueness_ratio = clamp_int (next.uniqueness_ratio + 1,
                                                       0, 100);
                    changed = TRUE;
                    break;
                case SDLK_j:
                    next.speckle_window_size = clamp_int (
                        next.speckle_window_size - 10, 0, 10000);
                    changed = TRUE;
                    break;
                case SDLK_k:
                    next.speckle_window_size = clamp_int (
                        next.speckle_window_size + 10, 0, 10000);
                    changed = TRUE;
                    break;
                case SDLK_n:
                    next.speckle_range = clamp_int (next.speckle_range - 1,
                                                    0, 1000);
                    changed = TRUE;
                    break;
                case SDLK_m:
                    next.speckle_range = clamp_int (next.speckle_range + 1,
                                                    0, 1000);
                    changed = TRUE;
                    break;
                case SDLK_h:
                    next.pre_filter_cap = clamp_int (next.pre_filter_cap - 1,
                                                     1, 63);
                    changed = TRUE;
                    break;
                case SDLK_l:
                    next.pre_filter_cap = clamp_int (next.pre_filter_cap + 1,
                                                     1, 63);
                    changed = TRUE;
                    break;
                case SDLK_COMMA:
                    next.disp12_max_diff = clamp_int (next.disp12_max_diff - 1,
                                                      -1, 1000);
                    changed = TRUE;
                    break;
                case SDLK_PERIOD:
                    next.disp12_max_diff = clamp_int (next.disp12_max_diff + 1,
                                                      -1, 1000);
                    changed = TRUE;
                    break;
                case SDLK_9:
                    next.mode = clamp_int (next.mode - 1, 0, 3);
                    changed = TRUE;
                    break;
                case SDLK_0:
                    next.mode = clamp_int (next.mode + 1, 0, 3);
                    changed = TRUE;
                    break;
                case SDLK_p:
                    print_only = TRUE;
                    break;
                default:
                    break;
                }

                if (print_only)
                    print_sgbm_params (sgbm_params);
                if (changed) {
                    if (ag_disparity_update_sgbm_params (disp_ctx, &next) == 0) {
                        *sgbm_params = next;
                        print_sgbm_params (sgbm_params);
                    } else {
                        fprintf (stderr, "warn: failed to apply SGBM params\n");
                    }
                }
            }
            /* Mouse click on disparity panel: print depth. */
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int out_w, out_h;
                SDL_GetRendererOutputSize (renderer, &out_w, &out_h);
                double sx = (double) display_w / (double) out_w;
                double sy = (double) display_h / (double) out_h;
                int px = (int) (ev.button.x * sx);
                int py = (int) (ev.button.y * sy);
                /* Right half is disparity panel. */
                if (px >= (int) proc_sub_w && px < (int) display_w &&
                    py >= 0 && py < (int) display_h) {
                    int dx = px - (int) proc_sub_w;
                    int idx = py * (int) proc_sub_w + dx;
                    int16_t d = disparity_buf[idx];
                    double depth = ag_disparity_to_depth (
                        d, sgbm_params->min_disparity > 0 ?
                           sgbm_params->min_disparity : 0,
                        sgbm_params->num_disparities > 0 ?
                           sgbm_params->num_disparities : 128);
                    /* Use metadata for depth if available. */
                    (void) depth;
                    printf ("click (%d,%d) disp_q4=%d disp=%.2f px\n",
                            dx, py, (int) d, (double) d / 16.0);
                }
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
                fprintf (stderr, "warn: TriggerSoftware: %s\n", e->message);
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
            memcpy (bayer_left,  bayer_left_src,  src_sub_w * src_h);
            memcpy (bayer_right, bayer_right_src, src_sub_w * src_h);
        }

        /* ---- Disparity path (pre-gamma for better matching) ---- */
        debayer_rg8_to_rgb (bayer_left,  rgb_nogamma_l, proc_sub_w, proc_h);
        debayer_rg8_to_rgb (bayer_right, rgb_nogamma_r, proc_sub_w, proc_h);
        rgb_to_gray (rgb_nogamma_l, gray_left,  (uint32_t) eye_pixels);
        rgb_to_gray (rgb_nogamma_r, gray_right, (uint32_t) eye_pixels);
        ag_remap_gray (remap_left,  gray_left,  rect_gray_l);
        ag_remap_gray (remap_right, gray_right, rect_gray_r);

        int disp_ok = ag_disparity_compute (disp_ctx,
                                             rect_gray_l, rect_gray_r,
                                             disparity_buf);

        ag_disparity_colorize (disparity_buf, proc_sub_w, proc_h,
                               sgbm_params->min_disparity,
                               sgbm_params->num_disparities,
                               disparity_rgb);

        /* ---- Display path (with gamma for natural look) ---- */
        apply_lut_inplace (bayer_left,  eye_pixels, gamma_lut);
        debayer_rg8_to_rgb (bayer_left, rgb_left, proc_sub_w, proc_h);
        ag_remap_rgb (remap_left, rgb_left, rect_rgb_l);

        /* Upload to SDL texture: [rectified left | disparity colourmap]. */
        void *tex_pixels;
        int tex_pitch;
        if (SDL_LockTexture (texture, NULL, &tex_pixels, &tex_pitch) == 0) {
            for (guint y = 0; y < proc_h; y++) {
                guint8 *dst = (guint8 *) tex_pixels + (size_t) y * (size_t) tex_pitch;
                memcpy (dst,
                        rect_rgb_l + (size_t) y * proc_sub_w * 3,
                        proc_sub_w * 3);
                if (disp_ok == 0) {
                    memcpy (dst + proc_sub_w * 3,
                            disparity_rgb + (size_t) y * proc_sub_w * 3,
                            proc_sub_w * 3);
                } else {
                    memset (dst + proc_sub_w * 3, 0, proc_sub_w * 3);
                }
            }
            SDL_UnlockTexture (texture);
        }

        arv_stream_push_buffer (cfg.stream, buffer);

        SDL_RenderClear (renderer);
        SDL_RenderCopy (renderer, texture, NULL, NULL);
        SDL_RenderPresent (renderer);

        frames_displayed++;

        double elapsed = g_timer_elapsed (stats_timer, NULL);
        if (elapsed >= 5.0) {
            printf ("  %.1f fps (displayed=%" G_GUINT64_FORMAT
                    " dropped=%" G_GUINT64_FORMAT ") [%s]\n",
                    frames_displayed / elapsed, frames_displayed,
                    frames_dropped, ag_stereo_backend_name (backend));
            frames_displayed = 0;
            frames_dropped = 0;
            g_timer_start (stats_timer);
        }

        g_usleep ((gulong) trigger_interval_us);
    }

    g_timer_destroy (stats_timer);
    printf ("\nStopping...\n");
    arv_camera_stop_acquisition (camera, NULL);

cleanup_sdl:
    g_free (disparity_rgb);
    g_free (disparity_buf);
    g_free (rect_gray_r);
    g_free (rect_gray_l);
    g_free (gray_right);
    g_free (gray_left);
    g_free (rgb_nogamma_r);
    g_free (rgb_nogamma_l);
    g_free (rect_rgb_l);
    g_free (rgb_right);
    g_free (rgb_left);
    g_free (bayer_left_src);
    g_free (bayer_right_src);
    g_free (bayer_left);
    g_free (bayer_right);
    SDL_DestroyTexture (texture);
    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit ();
    ag_disparity_destroy (disp_ctx);

cleanup:
    ag_remap_table_free (remap_left);
    ag_remap_table_free (remap_right);
    g_object_unref (cfg.stream);
    g_object_unref (camera);
    arv_shutdown ();
    return EXIT_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Subcommand entry point                                             */
/* ------------------------------------------------------------------ */

static int
cmd_depth_preview_impl (int argc, char *argv[], arg_dstr_t res, void *ctx,
                        const char *cmd_name, gboolean enable_runtime_tuning)
{
    (void) ctx;

    struct arg_str *cmd       = arg_str1 (NULL, NULL, cmd_name, NULL);
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
    struct arg_str *rectify   = arg_str1 ("r", "rectify", "<session>",
                                          "calibration session folder (required)");
    struct arg_str *backend_a = arg_str0 (NULL, "stereo-backend", "<name>",
                                          "sgbm (default), onnx, igev, rt-igev, foundation");
    struct arg_str *model_path_a = arg_str0 (NULL, "model-path", "<path>",
                                              "ONNX model file (auto for named backends)");
    struct arg_int *min_disp_a = arg_int0 (NULL, "min-disparity", "<int>",
                                            "override calibration min_disparity");
    struct arg_int *num_disp_a = arg_int0 (NULL, "num-disparities", "<int>",
                                            "override calibration num_disparities");
    struct arg_int *blk_size_a = arg_int0 (NULL, "block-size", "<int>",
                                            "SGBM block size (default: 5)");
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (15);

    void *argtable[] = { cmd, serial, address, interface, fps_a, exposure,
                         gain, auto_exp, binning_a, pkt_size, rectify,
                         backend_a, model_path_a,
                         min_disp_a, num_disp_a, blk_size_a,
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
    if (arg_make_syntax_err_help_msg (res, cmd_name, help->count,
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

    /* Parse stereo backend. */
    AgStereoBackend backend = AG_STEREO_SGBM;
    if (backend_a->count) {
        if (ag_stereo_parse_backend (backend_a->sval[0], &backend) != 0) {
            arg_dstr_catf (res, "error: unknown --stereo-backend '%s' "
                           "(options: sgbm, onnx, igev, rt-igev, foundation)\n",
                           backend_a->sval[0]);
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }

    /* Load calibration metadata for disparity defaults. */
    CalibMeta meta = { .min_disparity = 0, .num_disparities = 128,
                       .focal_length_px = 0.0, .baseline_cm = 0.0 };
    load_calibration_meta (rectify->sval[0], &meta);

    /* CLI overrides. */
    if (min_disp_a->count)  meta.min_disparity  = min_disp_a->ival[0];
    if (num_disp_a->count)  meta.num_disparities = num_disp_a->ival[0];

    /* Ensure num_disparities is a positive multiple of 16. */
    if (meta.num_disparities <= 0)
        meta.num_disparities = 128;
    meta.num_disparities = ((meta.num_disparities + 15) / 16) * 16;

    /* Build SGBM params. */
    AgSgbmParams sgbm_params;
    ag_sgbm_params_defaults (&sgbm_params);
    sgbm_params.min_disparity   = meta.min_disparity;
    sgbm_params.num_disparities = meta.num_disparities;
    if (blk_size_a->count)
        sgbm_params.block_size = blk_size_a->ival[0];

    printf ("Disparity range: min=%d num=%d\n",
            sgbm_params.min_disparity, sgbm_params.num_disparities);

    /* Build ONNX params (for neural backends). */
    const char *model_path = model_path_a->count ? model_path_a->sval[0] : NULL;

    /* If no explicit --model-path, use the default for named aliases. */
    if (!model_path && backend_a->count)
        model_path = ag_stereo_default_model_path (backend_a->sval[0]);

    AgOnnxParams onnx_params = {
        .model_path = model_path,
    };

    /* Validate ONNX backend requirements. */
    if (backend == AG_STEREO_ONNX && !onnx_params.model_path) {
        arg_dstr_catf (res, "error: --model-path is required for the onnx backend "
                       "(or use a named backend: igev, rt-igev, foundation)\n");
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

    exitcode = depth_preview_loop (device_id, iface_ip, fps,
                                    exposure_us, gain_db,
                                    do_auto_expose, pkt_sz, binning,
                                    rectify->sval[0], backend,
                                    &sgbm_params, &onnx_params,
                                    enable_runtime_tuning);
    g_free (device_id);

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}

int
cmd_depth_preview_classical (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    return cmd_depth_preview_impl (argc, argv, res, ctx,
                                   "depth-preview-classical", TRUE);
}

int
cmd_depth_preview_neural (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    return cmd_depth_preview_impl (argc, argv, res, ctx,
                                   "depth-preview-neural", FALSE);
}
