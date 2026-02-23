/*
 * stream.c — real-time stereo preview using SDL2
 *
 * Continuously captures DualBayerRG8 frames from the PDH016S stereo camera,
 * debayers each eye, and displays them side-by-side in an SDL2 window.
 *
 * Default trigger rate is 10 Hz (adjustable with --fps).
 */

#include <arv.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include <errno.h>
#include <glib/gstdio.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

static const double k_raw_gamma = 2.5;

static const guint8 *
gamma_lut_2p5 (void)
{
    static gboolean initialized = FALSE;
    static guint8 lut[256];
    if (!initialized) {
        double inv_gamma = 1.0 / k_raw_gamma;
        for (int i = 0; i < 256; i++) {
            double x = (double) i / 255.0;
            double y = pow (x, inv_gamma) * 255.0;
            if (y < 0.0)
                y = 0.0;
            if (y > 255.0)
                y = 255.0;
            lut[i] = (guint8) y;
        }
        initialized = TRUE;
    }
    return lut;
}

static void
apply_lut_inplace (guint8 *data, size_t n, const guint8 lut[256])
{
    for (size_t i = 0; i < n; i++)
        data[i] = lut[data[i]];
}

/* ------------------------------------------------------------------ */
/*  Shared helpers (same as capture.c)                                */
/* ------------------------------------------------------------------ */

static const char *
interface_ipv4_address (const char *iface_name)
{
    static char buf[INET_ADDRSTRLEN];
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs (&ifaddr) != 0)
        return NULL;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (strcmp (ifa->ifa_name, iface_name) != 0)
            continue;

        struct sockaddr_in *sin = (struct sockaddr_in *) ifa->ifa_addr;
        inet_ntop (AF_INET, &sin->sin_addr, buf, sizeof (buf));
        freeifaddrs (ifaddr);
        return buf;
    }

    freeifaddrs (ifaddr);
    return NULL;
}

static gboolean
device_on_interface (const char *device_addr_str, const char *iface_name)
{
    if (!device_addr_str || !iface_name)
        return FALSE;

    struct in_addr device_addr;
    if (inet_pton (AF_INET, device_addr_str, &device_addr) != 1)
        return FALSE;

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs (&ifaddr) != 0)
        return FALSE;

    gboolean found = FALSE;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (!ifa->ifa_netmask)
            continue;
        if (strcmp (ifa->ifa_name, iface_name) != 0)
            continue;

        struct in_addr iface_addr =
            ((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;
        struct in_addr netmask =
            ((struct sockaddr_in *) ifa->ifa_netmask)->sin_addr;

        if ((device_addr.s_addr & netmask.s_addr) ==
            (iface_addr.s_addr  & netmask.s_addr)) {
            found = TRUE;
            break;
        }
    }

    freeifaddrs (ifaddr);
    return found;
}

static char *
resolve_device_id_by_address (const char *address, const char *opt_interface)
{
    arv_update_device_list ();
    guint n = arv_get_n_devices ();

    for (guint i = 0; i < n; i++) {
        const char *dev_id = arv_get_device_id (i);
        const char *dev_addr = arv_get_device_address (i);

        if (!dev_addr || strcmp (dev_addr, address) != 0)
            continue;
        if (opt_interface && !device_on_interface (dev_addr, opt_interface))
            continue;

        return g_strdup (dev_id);
    }

    return NULL;
}

static void
try_set_string_feature (ArvDevice *device, const char *name, const char *value)
{
    GError *error = NULL;
    arv_device_set_string_feature_value (device, name, value, &error);
    if (error) {
        fprintf (stderr, "warn: failed to set %s=%s: %s\n", name, value, error->message);
        g_clear_error (&error);
    } else {
        printf ("  %s = %s\n", name, value);
    }
}

static void
try_set_integer_feature (ArvDevice *device, const char *name, gint64 value)
{
    GError *error = NULL;
    arv_device_set_integer_feature_value (device, name, value, &error);
    if (error) {
        fprintf (stderr, "warn: failed to set %s=%" G_GINT64_FORMAT ": %s\n",
                 name, value, error->message);
        g_clear_error (&error);
    } else {
        printf ("  %s = %" G_GINT64_FORMAT "\n", name, value);
    }
}

static void
try_set_float_feature (ArvDevice *device, const char *name, double value)
{
    GError *error = NULL;
    arv_device_set_float_feature_value (device, name, value, &error);
    if (error) {
        fprintf (stderr, "warn: failed to set %s=%g: %s\n", name, value, error->message);
        g_clear_error (&error);
    } else {
        printf ("  %s = %g\n", name, value);
    }
}

static gint64
read_integer_feature_or_default (ArvDevice *device, const char *name, gint64 fallback)
{
    GError *error = NULL;
    gint64 value = arv_device_get_integer_feature_value (device, name, &error);
    if (error) {
        fprintf (stderr, "warn: failed to read %s: %s (using %" G_GINT64_FORMAT ")\n",
                 name, error->message, fallback);
        g_clear_error (&error);
        return fallback;
    }
    return value;
}

static gboolean
try_get_integer_feature (ArvDevice *device, const char *name, gint64 *out_value)
{
    GError *error = NULL;
    gint64 value = arv_device_get_integer_feature_value (device, name, &error);
    if (error) {
        g_clear_error (&error);
        return FALSE;
    }
    *out_value = value;
    return TRUE;
}

static void
try_execute_optional_command (ArvDevice *device, const char *name)
{
    GError *error = NULL;
    gboolean available = arv_device_is_feature_available (device, name, &error);
    if (error) {
        g_clear_error (&error);
        return;
    }
    if (!available)
        return;

    arv_device_execute_command (device, name, &error);
    if (error) {
        fprintf (stderr, "warn: command %s failed: %s\n", name, error->message);
        g_clear_error (&error);
    } else {
        printf ("  %s executed\n", name);
    }
}

/* ------------------------------------------------------------------ */
/*  Debayer (same as capture.c)                                       */
/* ------------------------------------------------------------------ */

static void
debayer_rg8_to_rgb (const guint8 *bayer, guint8 *rgb, guint width, guint height)
{
    for (guint y = 0; y < height; y++) {
        for (guint x = 0; x < width; x++) {
#define B(dx, dy) ((int) bayer[ \
    (guint) CLAMP ((int)(y) + (dy), 0, (int)(height) - 1) * (width) + \
    (guint) CLAMP ((int)(x) + (dx), 0, (int)(width)  - 1)])

            int r, g, b;
            int ye = ((y & 1) == 0);
            int xe = ((x & 1) == 0);

            if (ye && xe) {
                r = B( 0,  0);
                g = (B(-1, 0) + B(1, 0) + B( 0,-1) + B(0, 1)) / 4;
                b = (B(-1,-1) + B(1,-1) + B(-1, 1) + B(1, 1)) / 4;
            } else if (ye && !xe) {
                r = (B(-1, 0) + B(1, 0)) / 2;
                g = B( 0,  0);
                b = (B( 0,-1) + B(0, 1)) / 2;
            } else if (!ye && xe) {
                r = (B( 0,-1) + B(0, 1)) / 2;
                g = B( 0,  0);
                b = (B(-1, 0) + B(1, 0)) / 2;
            } else {
                r = (B(-1,-1) + B(1,-1) + B(-1, 1) + B(1, 1)) / 4;
                g = (B(-1, 0) + B(1, 0) + B( 0,-1) + B(0, 1)) / 4;
                b = B( 0,  0);
            }

#undef B
            size_t idx = ((size_t) y * width + x) * 3;
            rgb[idx + 0] = (guint8) r;
            rgb[idx + 1] = (guint8) g;
            rgb[idx + 2] = (guint8) b;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Global quit flag for signal handler                               */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_quit = 0;

static void
sigint_handler (int sig)
{
    (void) sig;
    g_quit = 1;
}

/* ------------------------------------------------------------------ */
/*  Usage                                                             */
/* ------------------------------------------------------------------ */

static void
print_usage (const char *prog)
{
    fprintf (stderr,
             "Usage:\n"
             "  %s -s <serial>  [-i <interface>] [options]\n"
             "  %s -a <address> [-i <interface>] [options]\n"
             "\n"
             "Options:\n"
             "  -s, --serial     <serial>    match by serial number\n"
             "  -a, --address    <address>   connect directly by camera IP\n"
             "  -i, --interface  <iface>     force NIC selection (ARV_INTERFACE)\n"
             "  -f, --fps        <rate>      trigger rate in Hz (default: 10)\n"
             "  -x, --exposure   <us>        exposure time in microseconds\n"
             "  -b, --binning    <1|2>       sensor binning factor (default: 1)\n",
             prog, prog);
}

/* ------------------------------------------------------------------ */
/*  Stream + display                                                  */
/* ------------------------------------------------------------------ */

static int
stream_loop (const char *device_id, const char *iface_ip,
             double fps, double exposure_us, int binning)
{
    GError *error = NULL;
    ArvCamera *camera = arv_camera_new (device_id, &error);
    if (!camera) {
        fprintf (stderr, "error: %s\n", error ? error->message : "failed to open device");
        g_clear_error (&error);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);
    printf ("Connected.\n");

    /* Stop any stale acquisition. */
    printf ("Stopping any stale acquisition...\n");
    arv_camera_stop_acquisition (camera, NULL);
    try_execute_optional_command (device, "TransferStop");
    g_usleep (100000);

    printf ("Configuring for continuous streaming...\n");

    /*
     * Continuous acquisition with periodic software trigger.
     * This mirrors the proven trigger path from capture.c but fires
     * repeatedly at the requested frame rate.
     */
    try_set_string_feature  (device, "AcquisitionMode", "Continuous");
    try_set_string_feature  (device, "AcquisitionStartMode", "Normal");
    try_set_string_feature  (device, "TriggerSelector", "FrameStart");
    try_set_string_feature  (device, "TriggerMode", "On");
    try_set_string_feature  (device, "TriggerSource", "Software");
    try_set_string_feature  (device, "ImagerOutputSelector", "All");

    /* Always program binning nodes so --binning=1 truly disables binning. */
    try_set_string_feature  (device, "BinningSelector",       "Sensor");
    try_set_integer_feature (device, "BinningHorizontal",     (gint64) binning);
    try_set_integer_feature (device, "BinningVertical",       (gint64) binning);
    try_set_string_feature  (device, "BinningHorizontalMode", "Average");
    try_set_string_feature  (device, "BinningVerticalMode",   "Average");

    gint64 eff_bin_h = 1, eff_bin_v = 1;
    gboolean has_bin_h = try_get_integer_feature (device, "BinningHorizontal", &eff_bin_h);
    gboolean has_bin_v = try_get_integer_feature (device, "BinningVertical", &eff_bin_v);
    int software_binning = 1;
    if (binning > 1 && (!has_bin_h || !has_bin_v || eff_bin_h != binning || eff_bin_v != binning)) {
        software_binning = binning;
        eff_bin_h = 1;
        eff_bin_v = 1;
        fprintf (stderr,
                 "warn: hardware binning unavailable/ineffective; using %dx software binning\n",
                 software_binning);
    }

    /* Reset ROI offsets, then apply geometry from effective binning factors. */
    try_set_integer_feature (device, "OffsetX", 0);
    try_set_integer_feature (device, "OffsetY", 0);
    gint64 target_w = (eff_bin_h > 0) ? (2880 / eff_bin_h) : 2880;
    gint64 target_h = (eff_bin_v > 0) ? (1080 / eff_bin_v) : 1080;
    try_set_integer_feature (device, "Width",  target_w);
    try_set_integer_feature (device, "Height", target_h);
    try_set_string_feature  (device, "PixelFormat", "DualBayerRG8");

    guint frame_w = (guint) read_integer_feature_or_default (device, "Width", target_w);
    guint frame_h = (guint) read_integer_feature_or_default (device, "Height", target_h);
    if ((gint64) frame_w != target_w || (gint64) frame_h != target_h) {
        fprintf (stderr,
                 "warn: geometry readback is %ux%u (requested %" G_GINT64_FORMAT "x%" G_GINT64_FORMAT ")\n",
                 frame_w, frame_h, target_w, target_h);
    }

    if (exposure_us > 0.0)
        try_set_float_feature (device, "ExposureTime", exposure_us);

    try_set_string_feature  (device, "TransferSelector", "Stream0");
    try_set_integer_feature (device, "TransferSelector", 0);
    try_set_string_feature  (device, "TransferControlMode", "Automatic");
    try_set_string_feature  (device, "TransferQueueMode", "FirstInFirstOut");

    try_set_integer_feature (device, "GevSCPSPacketSize", 1400);

    arv_camera_gv_set_stream_options (camera,
                                       ARV_GV_STREAM_OPTION_PACKET_SOCKET_DISABLED);

    ArvStream *stream = arv_camera_create_stream (camera, NULL, NULL, &error);
    if (!stream) {
        fprintf (stderr, "error: failed to create stream: %s\n",
                 error ? error->message : "(unknown)");
        g_clear_error (&error);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    if (ARV_IS_GV_STREAM (stream)) {
        g_object_set (stream,
                      "packet-resend",   ARV_GV_STREAM_PACKET_RESEND_ALWAYS,
                      "packet-timeout",  (guint) 200000,
                      "frame-retention", (guint) 10000000,
                      NULL);
    }

    /* Force unicast GVSP. */
    {
        const char *host_ip = iface_ip;
        if (!host_ip) {
            GError *e = NULL;
            gint64 scda = arv_device_get_integer_feature_value (device, "GevSCDA", &e);
            if (!e && scda != 0) {
                struct in_addr a;
                a.s_addr = htonl ((uint32_t) scda);
                static char detected_ip[INET_ADDRSTRLEN];
                inet_ntop (AF_INET, &a, detected_ip, sizeof detected_ip);
                host_ip = detected_ip;
            }
            g_clear_error (&e);
        }
        if (host_ip) {
            struct in_addr addr;
            if (inet_pton (AF_INET, host_ip, &addr) == 1) {
                gint64 scda_val = (gint64) ntohl (addr.s_addr);
                try_set_integer_feature (device, "GevSCDA", scda_val);
                printf ("  Forced GevSCDA -> %s (unicast)\n", host_ip);
            }
        }
        arv_camera_gv_set_packet_size (camera, 1400, &error);
        if (error) {
            fprintf (stderr, "warn: set_packet_size failed: %s\n", error->message);
            g_clear_error (&error);
        }
    }

    size_t payload = (size_t) arv_camera_get_payload (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to read payload size: %s\n", error->message);
        g_clear_error (&error);
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }
    printf ("  payload = %zu bytes\n", payload);

    /* Push more buffers for continuous streaming to avoid underruns. */
    for (int i = 0; i < 16; i++)
        arv_stream_push_buffer (stream, arv_buffer_new_allocate (payload));

    /* ---- SDL2 setup ---- */

    guint src_sub_w = frame_w / 2;  /* each eye after deinterleaving */
    guint src_h = frame_h;
    guint proc_sub_w = src_sub_w / (guint) software_binning;
    guint proc_h = src_h / (guint) software_binning;
    guint display_w = proc_sub_w * 2;
    guint display_h = proc_h;

    if (SDL_Init (SDL_INIT_VIDEO) != 0) {
        fprintf (stderr, "error: SDL_Init: %s\n", SDL_GetError ());
        g_object_unref (stream);
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
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer (
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        /* Fall back to software renderer. */
        renderer = SDL_CreateRenderer (window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf (stderr, "error: SDL_CreateRenderer: %s\n", SDL_GetError ());
        SDL_DestroyWindow (window);
        SDL_Quit ();
        g_object_unref (stream);
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
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    /* Debayer scratch buffers for left and right eyes. */
    guint8 *rgb_left  = g_malloc ((size_t) proc_sub_w * proc_h * 3);
    guint8 *rgb_right = g_malloc ((size_t) proc_sub_w * proc_h * 3);
    guint8 *bayer_left_src  = g_malloc ((size_t) src_sub_w * src_h);
    guint8 *bayer_right_src = g_malloc ((size_t) src_sub_w * src_h);
    guint8 *bayer_left  = g_malloc ((size_t) proc_sub_w * proc_h);
    guint8 *bayer_right = g_malloc ((size_t) proc_sub_w * proc_h);

    /* ---- Start acquisition ---- */

    printf ("Starting acquisition at %.1f Hz...\n", fps);
    arv_camera_start_acquisition (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to start acquisition: %s\n", error->message);
        g_clear_error (&error);
        goto cleanup;
    }

    signal (SIGINT, sigint_handler);

    guint64 trigger_interval_us = (guint64) (1000000.0 / fps);
    guint64 frames_displayed = 0;
    guint64 frames_dropped = 0;
    const guint8 *gamma_lut = gamma_lut_2p5 ();
    GTimer *stats_timer = g_timer_new ();

    while (!g_quit) {
        /* Handle SDL events. */
        SDL_Event ev;
        while (SDL_PollEvent (&ev)) {
            if (ev.type == SDL_QUIT)
                g_quit = 1;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                g_quit = 1;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_q)
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
                    g_usleep (2000);  /* 2 ms */
                    polls++;
                }
            }
            if (!armed) {
                /* Camera not ready — skip this cycle. */
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

        /* Pop the frame. */
        ArvBuffer *buffer = arv_stream_timeout_pop_buffer (stream, 2000000); /* 2 s */
        if (!buffer) {
            frames_dropped++;
            continue;
        }

        ArvBufferStatus st = arv_buffer_get_status (buffer);
        if (st != ARV_BUFFER_STATUS_SUCCESS) {
            frames_dropped++;
            arv_stream_push_buffer (stream, buffer);
            continue;
        }

        size_t data_size = 0;
        const guint8 *data = arv_buffer_get_data (buffer, &data_size);
        guint w = arv_buffer_get_image_width (buffer);
        guint h = arv_buffer_get_image_height (buffer);
        size_t needed = (size_t) w * (size_t) h;

        if (!data || data_size < needed || w % 2 != 0 || w != frame_w || h != frame_h) {
            frames_dropped++;
            arv_stream_push_buffer (stream, buffer);
            continue;
        }

        /* Deinterleave DualBayer columns into left/right Bayer planes. */
        guint sw = w / 2;
        for (guint y = 0; y < h; y++) {
            const guint8 *row = data + ((size_t) y * (size_t) w);
            guint8 *lrow = bayer_left_src  + ((size_t) y * (size_t) sw);
            guint8 *rrow = bayer_right_src + ((size_t) y * (size_t) sw);
            for (guint x = 0; x < sw; x++) {
                lrow[x] = row[2 * x];
                rrow[x] = row[2 * x + 1];
            }
        }

        if (software_binning > 1) {
            for (guint y = 0; y < proc_h; y++) {
                guint sy = 2 * y;
                for (guint x = 0; x < proc_sub_w; x++) {
                    guint sx = 2 * x;
                    size_t i00 = (size_t) sy * sw + sx;
                    size_t i01 = i00 + 1;
                    size_t i10 = i00 + sw;
                    size_t i11 = i10 + 1;
                    bayer_left[(size_t) y * proc_sub_w + x] = (guint8)
                        ((bayer_left_src[i00] + bayer_left_src[i01] + bayer_left_src[i10] + bayer_left_src[i11]) / 4);
                    bayer_right[(size_t) y * proc_sub_w + x] = (guint8)
                        ((bayer_right_src[i00] + bayer_right_src[i01] + bayer_right_src[i10] + bayer_right_src[i11]) / 4);
                }
            }
        } else {
            memcpy (bayer_left, bayer_left_src, (size_t) sw * h);
            memcpy (bayer_right, bayer_right_src, (size_t) sw * h);
        }

        size_t eye_n = (size_t) proc_sub_w * (size_t) proc_h;
        apply_lut_inplace (bayer_left, eye_n, gamma_lut);
        apply_lut_inplace (bayer_right, eye_n, gamma_lut);

        /* Debayer each eye to RGB. */
        debayer_rg8_to_rgb (bayer_left,  rgb_left,  proc_sub_w, proc_h);
        debayer_rg8_to_rgb (bayer_right, rgb_right, proc_sub_w, proc_h);

        /* Upload to SDL texture: left eye on the left half, right on the right. */
        void *tex_pixels;
        int tex_pitch;
        if (SDL_LockTexture (texture, NULL, &tex_pixels, &tex_pitch) == 0) {
            for (guint y = 0; y < proc_h; y++) {
                guint8 *dst = (guint8 *) tex_pixels + (size_t) y * (size_t) tex_pitch;
                /* Left eye */
                memcpy (dst, rgb_left + (size_t) y * proc_sub_w * 3, proc_sub_w * 3);
                /* Right eye */
                memcpy (dst + proc_sub_w * 3, rgb_right + (size_t) y * proc_sub_w * 3, proc_sub_w * 3);
            }
            SDL_UnlockTexture (texture);
        }

        arv_stream_push_buffer (stream, buffer);

        SDL_RenderClear (renderer);
        SDL_RenderCopy (renderer, texture, NULL, NULL);
        SDL_RenderPresent (renderer);

        frames_displayed++;

        /* Print stats every 5 seconds. */
        double elapsed = g_timer_elapsed (stats_timer, NULL);
        if (elapsed >= 5.0) {
            printf ("  %.1f fps (displayed=%" G_GUINT64_FORMAT
                    " dropped=%" G_GUINT64_FORMAT ")\n",
                    frames_displayed / elapsed, frames_displayed, frames_dropped);
            frames_displayed = 0;
            frames_dropped = 0;
            g_timer_start (stats_timer);
        }

        /* Pace to target frame rate. */
        g_usleep ((gulong) trigger_interval_us);
    }

    g_timer_destroy (stats_timer);
    printf ("\nStopping...\n");
    arv_camera_stop_acquisition (camera, NULL);

cleanup:
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
    g_object_unref (stream);
    g_object_unref (camera);
    arv_shutdown ();
    return EXIT_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int
main (int argc, char **argv)
{
    const char *opt_serial    = NULL;
    const char *opt_address   = NULL;
    const char *opt_interface = NULL;
    const char *opt_fps       = NULL;
    const char *opt_exposure  = NULL;
    const char *opt_binning   = NULL;

    static const struct option long_opts[] = {
        { "serial",    required_argument, NULL, 's' },
        { "address",   required_argument, NULL, 'a' },
        { "interface", required_argument, NULL, 'i' },
        { "fps",       required_argument, NULL, 'f' },
        { "exposure",  required_argument, NULL, 'x' },
        { "binning",   required_argument, NULL, 'b' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long (argc, argv, "s:a:i:f:x:b:", long_opts, NULL)) != -1) {
        switch (c) {
            case 's': opt_serial    = optarg; break;
            case 'a': opt_address   = optarg; break;
            case 'i': opt_interface = optarg; break;
            case 'f': opt_fps       = optarg; break;
            case 'x': opt_exposure  = optarg; break;
            case 'b': opt_binning   = optarg; break;
            default:
                print_usage (argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (!opt_serial && !opt_address) {
        fprintf (stderr, "error: one of --serial or --address is required\n\n");
        print_usage (argv[0]);
        return EXIT_FAILURE;
    }
    if (opt_serial && opt_address) {
        fprintf (stderr, "error: --serial and --address are mutually exclusive\n\n");
        print_usage (argv[0]);
        return EXIT_FAILURE;
    }

    double fps = 10.0;
    if (opt_fps) {
        fps = atof (opt_fps);
        if (fps <= 0.0 || fps > 120.0) {
            fprintf (stderr, "error: --fps must be between 0 and 120\n\n");
            print_usage (argv[0]);
            return EXIT_FAILURE;
        }
    }

    double exposure_us = 0.0;
    if (opt_exposure) {
        exposure_us = atof (opt_exposure);
        if (exposure_us <= 0.0) {
            fprintf (stderr, "error: --exposure must be a positive number\n\n");
            print_usage (argv[0]);
            return EXIT_FAILURE;
        }
    }

    int binning = 1;
    if (opt_binning) {
        binning = atoi (opt_binning);
        if (binning != 1 && binning != 2) {
            fprintf (stderr, "error: --binning must be 1 or 2\n\n");
            print_usage (argv[0]);
            return EXIT_FAILURE;
        }
    }

    const char *iface_ip = NULL;
    if (opt_interface) {
        iface_ip = interface_ipv4_address (opt_interface);
        if (!iface_ip) {
            fprintf (stderr, "error: interface '%s' not found or has no IPv4 address\n",
                     opt_interface);
            return EXIT_FAILURE;
        }
        if (setenv ("ARV_INTERFACE", opt_interface, 1) != 0) {
            fprintf (stderr, "error: failed to set ARV_INTERFACE=%s: %s\n",
                     opt_interface, strerror (errno));
            return EXIT_FAILURE;
        }
        printf ("ARV_INTERFACE forced to %s (%s)\n", opt_interface, iface_ip);
    }

    /* Resolve device ID. */
    char *device_id = NULL;

    if (opt_address) {
        device_id = resolve_device_id_by_address (opt_address, opt_interface);
        if (!device_id) {
            printf ("Device not found in discovery; using address directly.\n");
            device_id = g_strdup (opt_address);
        } else {
            printf ("Using discovered device id: %s\n", device_id);
        }
    } else {
        arv_update_device_list ();
        guint n = arv_get_n_devices ();
        for (guint i = 0; i < n; i++) {
            const char *dev_id      = arv_get_device_id (i);
            const char *dev_address = arv_get_device_address (i);
            const char *dev_serial  = arv_get_device_serial_nbr (i);

            if (opt_interface && !device_on_interface (dev_address, opt_interface))
                continue;
            if (dev_serial && strcmp (dev_serial, opt_serial) == 0) {
                device_id = g_strdup (dev_id);
                break;
            }
        }
        if (!device_id) {
            fprintf (stderr, "error: serial '%s' not found%s%s\n",
                     opt_serial,
                     opt_interface ? " on interface " : "",
                     opt_interface ? opt_interface : "");
            arv_shutdown ();
            return EXIT_FAILURE;
        }
    }

    int rc = stream_loop (device_id, iface_ip, fps, exposure_us, binning);
    g_free (device_id);
    return rc;
}
