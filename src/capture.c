/*
 * capture.c — minimal production capture path for Aravis
 *
 * This file intentionally keeps only the known-good control/transport setup.
 * For deeper diagnostics and experimental fallback sequences, use:
 *   src/capture_debug.c
 */

#include <arv.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"
#pragma GCC diagnostic pop

typedef enum { ENC_PGM, ENC_PNG, ENC_JPG } EncFormat;

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
print_usage (const char *prog)
{
    fprintf (stderr,
             "Usage:\n"
             "  %s -s <serial>  [-i <interface>] [-o <output_dir>] [-e <format>]\n"
             "  %s -a <address> [-i <interface>] [-o <output_dir>] [-e <format>]\n"
             "\n"
             "Options:\n"
             "  -s, --serial     <serial>    match by serial number (uses discovery)\n"
             "  -a, --address    <address>   connect directly by camera IP\n"
             "  -i, --interface  <iface>     force Aravis NIC selection (ARV_INTERFACE)\n"
             "  -o, --output     <dir>       output directory (default: .)\n"
             "  -e, --encode     <format>    output format: png or jpg (default: pgm)\n"
             "  -x, --exposure   <us>        exposure time in microseconds (default: camera default)\n"
             "  -b, --binning    <1|2>       sensor binning factor (default: 1)\n",
             prog, prog);
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

static int
write_pgm (const char *path, const guint8 *data, guint width, guint height)
{
    FILE *f = fopen (path, "wb");
    if (!f) {
        fprintf (stderr, "error: cannot open '%s' for write: %s\n", path, strerror (errno));
        return EXIT_FAILURE;
    }

    fprintf (f, "P5\n%u %u\n255\n", width, height);
    size_t n = (size_t) width * (size_t) height;
    size_t written = fwrite (data, 1, n, f);
    fclose (f);

    if (written != n) {
        fprintf (stderr, "error: short write to '%s'\n", path);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * Bilinear debayer for BayerRG8 (RGGB pattern):
 *   even row, even col = R
 *   even row, odd  col = G
 *   odd  row, even col = G
 *   odd  row, odd  col = B
 *
 * Output: interleaved RGB, 3 bytes per pixel, row-major.
 */
static void
debayer_rg8_to_rgb (const guint8 *bayer, guint8 *rgb, guint width, guint height)
{
    for (guint y = 0; y < height; y++) {
        for (guint x = 0; x < width; x++) {
            /* clamp-to-edge sample */
#define B(dx, dy) ((int) bayer[ \
    (guint) CLAMP ((int)(y) + (dy), 0, (int)(height) - 1) * (width) + \
    (guint) CLAMP ((int)(x) + (dx), 0, (int)(width)  - 1)])

            int r, g, b;
            int ye = ((y & 1) == 0);
            int xe = ((x & 1) == 0);

            if (ye && xe) {           /* R pixel */
                r = B( 0,  0);
                g = (B(-1, 0) + B(1, 0) + B( 0,-1) + B(0, 1)) / 4;
                b = (B(-1,-1) + B(1,-1) + B(-1, 1) + B(1, 1)) / 4;
            } else if (ye && !xe) {   /* G on R row */
                r = (B(-1, 0) + B(1, 0)) / 2;
                g = B( 0,  0);
                b = (B( 0,-1) + B(0, 1)) / 2;
            } else if (!ye && xe) {   /* G on B row */
                r = (B( 0,-1) + B(0, 1)) / 2;
                g = B( 0,  0);
                b = (B(-1, 0) + B(1, 0)) / 2;
            } else {                  /* B pixel */
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

/*
 * Debayer a BayerRG8 frame and encode to PNG or JPEG.
 * JPEG quality is fixed at 90.
 */
static int
write_color_image (EncFormat enc, const char *path,
                   const guint8 *bayer, guint width, guint height)
{
    guint8 *rgb = g_malloc ((size_t) width * (size_t) height * 3);
    if (!rgb) {
        fprintf (stderr, "error: out of memory for debayer buffer\n");
        return EXIT_FAILURE;
    }

    debayer_rg8_to_rgb (bayer, rgb, width, height);

    int ok;
    if (enc == ENC_PNG)
        ok = stbi_write_png (path, (int) width, (int) height, 3, rgb, (int) width * 3);
    else
        ok = stbi_write_jpg (path, (int) width, (int) height, 3, rgb, 90);

    g_free (rgb);

    if (!ok) {
        fprintf (stderr, "error: failed to write '%s'\n", path);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int
write_dual_bayer_pair (const char *output_dir,
                       const char *basename_no_ext,
                       const guint8 *interleaved,
                       guint width,
                       guint height,
                       EncFormat enc)
{
    if (width % 2 != 0) {
        fprintf (stderr, "error: DualBayer frame width must be even, got %u\n", width);
        return EXIT_FAILURE;
    }

    guint sub_w = width / 2;
    size_t sub_n = (size_t) sub_w * (size_t) height;
    guint8 *left = g_malloc (sub_n);
    guint8 *right = g_malloc (sub_n);
    if (!left || !right) {
        g_free (left);
        g_free (right);
        fprintf (stderr, "error: out of memory while splitting DualBayer frame\n");
        return EXIT_FAILURE;
    }

    for (guint y = 0; y < height; y++) {
        const guint8 *row = interleaved + ((size_t) y * (size_t) width);
        guint8 *lrow = left  + ((size_t) y * (size_t) sub_w);
        guint8 *rrow = right + ((size_t) y * (size_t) sub_w);
        for (guint x = 0; x < sub_w; x++) {
            lrow[x] = row[2 * x];
            rrow[x] = row[2 * x + 1];
        }
    }

    const char *ext = (enc == ENC_PNG) ? "png" : (enc == ENC_JPG) ? "jpg" : "pgm";
    char *left_name  = g_strdup_printf ("%s_left.%s",  basename_no_ext, ext);
    char *right_name = g_strdup_printf ("%s_right.%s", basename_no_ext, ext);
    char *left_path  = g_build_filename (output_dir, left_name,  NULL);
    char *right_path = g_build_filename (output_dir, right_name, NULL);

    int rc_left, rc_right;
    if (enc == ENC_PGM) {
        rc_left  = write_pgm (left_path,  left,  sub_w, height);
        rc_right = write_pgm (right_path, right, sub_w, height);
    } else {
        rc_left  = write_color_image (enc, left_path,  left,  sub_w, height);
        rc_right = write_color_image (enc, right_path, right, sub_w, height);
    }

    if (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS) {
        printf ("Saved: %s  (%ux%u, BayerRG8 left)\n",  left_path,  sub_w, height);
        printf ("Saved: %s  (%ux%u, BayerRG8 right)\n", right_path, sub_w, height);
    }

    g_free (left_name);
    g_free (right_name);
    g_free (left_path);
    g_free (right_path);
    g_free (left);
    g_free (right);

    return (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int
capture_one_frame (const char *device_id, const char *output_dir,
                   const char *iface_ip, EncFormat enc,
                   double exposure_us, int binning)
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

    /*
     * Force-stop any stale acquisition.  If a previous session crashed or
     * was killed without AcquisitionStop, the camera may still be streaming
     * to a stale port, and a subsequent AcquisitionStart will have no effect.
     * The PDH016S docs note: "During acquisition, all Transport Layer
     * parameters are locked and cannot be modified."  We must stop first.
     */
    printf ("Stopping any stale acquisition...\n");
    arv_camera_stop_acquisition (camera, NULL);
    try_execute_optional_command (device, "TransferStop");
    g_usleep (100000);

    printf ("Configuring...\n");

    /*
     * Use Continuous rather than SingleFrame.  Some cameras (e.g. Lucid PDH016S)
     * have a firmware bug where SingleFrame mode sends the frame before the host
     * stream is ready, resulting in a partial/missing-packet failure every time.
     * In Continuous mode we grab the first good frame and then stop.
     */
    try_set_string_feature  (device, "AcquisitionMode", "Continuous");
    try_set_string_feature  (device, "AcquisitionStartMode", "Normal");
    try_set_string_feature  (device, "TriggerSelector", "FrameStart");
    try_set_string_feature  (device, "TriggerMode", "Off");
    try_set_string_feature  (device, "ImagerOutputSelector", "All");
    if (binning > 1) {
        try_set_integer_feature (device, "BinningHorizontal", (gint64) binning);
        try_set_integer_feature (device, "BinningVertical",   (gint64) binning);
    }
    try_set_integer_feature (device, "Width",  (gint64)(2880 / binning));
    try_set_integer_feature (device, "Height", (gint64)(1080 / binning));
    try_set_string_feature  (device, "PixelFormat", "DualBayerRG8");
    if (exposure_us > 0.0)
        try_set_float_feature (device, "ExposureTime", exposure_us);
    try_set_string_feature  (device, "TransferSelector", "Stream0");
    try_set_integer_feature (device, "TransferSelector", 0);
    try_set_string_feature  (device, "TransferControlMode", "Automatic");
    try_set_string_feature  (device, "TransferQueueMode", "FirstInFirstOut");

    /*
     * Use a fixed packet size instead of arv_camera_gv_auto_packet_size().
     * The auto-negotiation creates and destroys a temporary stream, which
     * can leave the camera's stream channel in a confused state — the
     * camera may not properly re-initialize the channel for the real
     * stream created afterward.
     */
    try_set_integer_feature (device, "GevSCPSPacketSize", 1400);

    /*
     * macOS does not support PF_PACKET (Linux raw L2) sockets.
     * Explicitly disable packet sockets so Aravis uses standard UDP.
     */
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
        /*
         * frame-retention: how long Aravis waits before declaring a frame lost.
         * Default is ~200 ms — far too short when packets need to be resent over
         * a non-ideal path.  Set to 10 s so resend has time to work.
         *
         * packet-timeout: how long to wait for any individual packet before
         * sending a NACK.  200 ms gives the camera reasonable time to respond.
         */
        g_object_set (stream,
                      "packet-resend",   ARV_GV_STREAM_PACKET_RESEND_ALWAYS,
                      "packet-timeout",  (guint) 200000,    /* 200 ms */
                      "frame-retention", (guint) 10000000,  /* 10 s  */
                      NULL);
        guint pt = 0, fr = 0;
        g_object_get (stream, "packet-timeout", &pt, "frame-retention", &fr, NULL);
        printf ("  stream packet-timeout  = %u µs\n", pt);
        printf ("  stream frame-retention = %u µs\n", fr);
    }

    /*
     * Force unicast GVSP: explicitly set GevSCDA to our host IP and
     * re-set packet size via Aravis API after stream creation.
     * This ensures the camera sends unicast UDP to our exact address,
     * not multicast or a stale destination.
     */
    {
        const char *host_ip = iface_ip;  /* from --interface, or detect below */
        if (!host_ip) {
            /* Try to read back what Aravis configured */
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
        /*
         * Keep packet size consistent with the GevSCPSPacketSize feature above (1400).
         * 1500 was wrong here: GevSCPSPacketSize counts only GigE Vision payload, so
         * 1500 + IP/UDP/GigE headers ≈ 1542 bytes total — above standard 1500-byte MTU,
         * causing IP fragmentation and massive packet loss.
         */
        arv_camera_gv_set_packet_size (camera, 1400, &error);
        if (error) {
            fprintf (stderr, "warn: arv_camera_gv_set_packet_size failed: %s\n", error->message);
            g_clear_error (&error);
        } else {
            printf ("  arv_camera_gv_set_packet_size(1400) OK\n");
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

    for (int i = 0; i < 8; i++)
        arv_stream_push_buffer (stream, arv_buffer_new_allocate (payload));

    /* Diagnostic: read back what Aravis configured for stream transport. */
    {
        gint64 scda_rb = 0, port_rb = 0, pkt_rb = 0, ccp_rb = 0;
        const char *acq_mode_rb = NULL;
        const char *acq_start_mode_rb = NULL;
        const char *trig_mode_rb = NULL;
        const char *xfer_mode_rb = NULL;
        const char *stream_proto_rb = NULL;
        const char **stream_proto_values = NULL;
        guint stream_proto_n = 0;
        GError *e = NULL;

        scda_rb = arv_device_get_integer_feature_value (device, "GevSCDA", &e);
        if (!e) {
            struct in_addr a;
            a.s_addr = htonl ((uint32_t) scda_rb);
            char b[INET_ADDRSTRLEN];
            printf ("  GevSCDA        = %s\n", inet_ntop (AF_INET, &a, b, sizeof b));
        }
        g_clear_error (&e);

        port_rb = arv_device_get_integer_feature_value (device, "GevSCPHostPort", &e);
        if (!e)
            printf ("  GevSCPHostPort = %" G_GINT64_FORMAT "\n", port_rb);
        g_clear_error (&e);

        pkt_rb = arv_device_get_integer_feature_value (device, "GevSCPSPacketSize", &e);
        if (!e)
            printf ("  GevSCPSPacketSize = %" G_GINT64_FORMAT "\n", pkt_rb);
        g_clear_error (&e);

        ccp_rb = arv_device_get_integer_feature_value (device, "GevCCP", &e);
        if (!e)
            printf ("  GevCCP = %" G_GINT64_FORMAT "\n", ccp_rb);
        g_clear_error (&e);

        acq_mode_rb = arv_device_get_string_feature_value (device, "AcquisitionMode", &e);
        if (!e && acq_mode_rb)
            printf ("  AcquisitionMode = %s\n", acq_mode_rb);
        g_clear_error (&e);

        acq_start_mode_rb = arv_device_get_string_feature_value (device, "AcquisitionStartMode", &e);
        if (!e && acq_start_mode_rb)
            printf ("  AcquisitionStartMode = %s\n", acq_start_mode_rb);
        g_clear_error (&e);

        trig_mode_rb = arv_device_get_string_feature_value (device, "TriggerMode", &e);
        if (!e && trig_mode_rb)
            printf ("  TriggerMode = %s\n", trig_mode_rb);
        g_clear_error (&e);

        xfer_mode_rb = arv_device_get_string_feature_value (device, "TransferControlMode", &e);
        if (!e && xfer_mode_rb)
            printf ("  TransferControlMode = %s\n", xfer_mode_rb);
        g_clear_error (&e);

        stream_proto_rb = arv_device_get_string_feature_value (device, "TransportStreamProtocol", &e);
        if (!e && stream_proto_rb)
            printf ("  TransportStreamProtocol = %s\n", stream_proto_rb);
        g_clear_error (&e);

        stream_proto_values =
            arv_device_dup_available_enumeration_feature_values_as_strings (
                device, "TransportStreamProtocol", &stream_proto_n, &e);
        if (!e && stream_proto_values && stream_proto_n > 0) {
            printf ("  TransportStreamProtocol options:");
            for (guint i = 0; i < stream_proto_n; i++)
                printf (" %s", stream_proto_values[i]);
            printf ("\n");
        }
        g_clear_error (&e);
        g_free (stream_proto_values);

        /* Verify image geometry was actually accepted by this camera. */
        gint64 width_rb = arv_device_get_integer_feature_value (device, "Width", &e);
        if (!e) printf ("  Width (readback)       = %" G_GINT64_FORMAT "\n", width_rb);
        g_clear_error (&e);
        gint64 height_rb = arv_device_get_integer_feature_value (device, "Height", &e);
        if (!e) printf ("  Height (readback)      = %" G_GINT64_FORMAT "\n", height_rb);
        g_clear_error (&e);
        const char *pf_rb = arv_device_get_string_feature_value (device, "PixelFormat", &e);
        if (!e && pf_rb) printf ("  PixelFormat (readback) = %s\n", pf_rb);
        g_clear_error (&e);
    }

    printf ("Starting acquisition...\n");
    arv_camera_start_acquisition (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to start acquisition: %s\n", error->message);
        g_clear_error (&error);
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    /* No-op in Automatic mode, but required if camera stays in UserControlled. */
    try_execute_optional_command (device, "TransferStart");

    ArvBuffer *buffer = NULL;
    ArvBuffer *partial_buf = NULL;  /* last incomplete frame, kept for debug save */

    for (int i = 0; i < 10; i++) {
        ArvBuffer *b = arv_stream_timeout_pop_buffer (stream, 5000000); /* 5 s */
        if (!b) {
            printf ("  attempt %d: no buffer\n", i);
            continue;
        }
        ArvBufferStatus st = arv_buffer_get_status (b);
        if (st == ARV_BUFFER_STATUS_SUCCESS) {
            if (partial_buf) {
                arv_stream_push_buffer (stream, partial_buf);
                partial_buf = NULL;
            }
            buffer = b;
            break;
        }

        size_t bdata_sz = 0;
        arv_buffer_get_data (b, &bdata_sz);
        ArvBufferPayloadType bpt = arv_buffer_get_payload_type (b);
        guint bw = 0, bh = 0;
        /* Only call image accessors if data arrived; a zero-byte timeout buffer
         * has payload type IMAGE but uninitialized part headers, which triggers
         * a GLib assertion inside arv_buffer_get_image_width/height. */
        if (bdata_sz > 0 &&
            (bpt == ARV_BUFFER_PAYLOAD_TYPE_IMAGE ||
             bpt == ARV_BUFFER_PAYLOAD_TYPE_EXTENDED_CHUNK_DATA)) {
            bw = arv_buffer_get_image_width (b);
            bh = arv_buffer_get_image_height (b);
        }
        printf ("  attempt %d: status=%d  payload=0x%x  frame_id=%" G_GUINT64_FORMAT
                "  recv=%zu bytes  %ux%u\n",
                i, (int) st, (unsigned) bpt,
                arv_buffer_get_frame_id (b),
                bdata_sz, bw, bh);

        /* Keep the last partial buffer; push back the previous one. */
        if (partial_buf)
            arv_stream_push_buffer (stream, partial_buf);
        partial_buf = b;

        {
            GError *qe = NULL;
            gint64 q = arv_device_get_integer_feature_value (device, "TransferQueueCurrentBlockCount", &qe);
            if (!qe)
                printf ("  transfer queue blocks = %" G_GINT64_FORMAT "\n", q);
            g_clear_error (&qe);
        }
    }

    if (!buffer) {
        fprintf (stderr, "error: timeout waiting for frame\n");

        /* Attempt to save whatever partial data arrived for visual inspection. */
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
            } else if (pd && ps > 0) {
                fprintf (stderr, "  (partial data too small to write as %ux%u PGM: %zu bytes)\n",
                         pw, ph, ps);
            }
            arv_stream_push_buffer (stream, partial_buf);
            partial_buf = NULL;
        }

        if (ARV_IS_GV_STREAM (stream)) {
            guint64 n_completed = 0, n_failures = 0, n_underruns = 0;
            arv_stream_get_statistics (stream, &n_completed, &n_failures, &n_underruns);
            fprintf (stderr, "  stream stats: completed=%" G_GUINT64_FORMAT
                     " failures=%" G_GUINT64_FORMAT
                     " underruns=%" G_GUINT64_FORMAT "\n",
                     n_completed, n_failures, n_underruns);

            guint64 resent = 0, missing = 0;
            arv_gv_stream_get_statistics (ARV_GV_STREAM (stream), &resent, &missing);
            fprintf (stderr, "  gv stats:     resent=%" G_GUINT64_FORMAT
                     " missing=%" G_GUINT64_FORMAT "\n", resent, missing);
        }
        arv_camera_stop_acquisition (camera, NULL);
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    size_t data_size = 0;
    const guint8 *data = arv_buffer_get_data (buffer, &data_size);
    guint width = arv_buffer_get_image_width (buffer);
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
        strftime (base, sizeof (base), "capture_%Y%m%d_%H%M%S", &tm_now);

        const char *pixel_format = arv_device_get_string_feature_value (device, "PixelFormat", NULL);
        if (pixel_format && strcmp (pixel_format, "DualBayerRG8") == 0) {
            rc = write_dual_bayer_pair (output_dir, base, data, width, height, enc);
        } else {
            const char *ext = (enc == ENC_PNG) ? "png" : (enc == ENC_JPG) ? "jpg" : "pgm";
            char *name = g_strdup_printf ("%s.%s", base, ext);
            char *path = g_build_filename (output_dir, name, NULL);
            if (enc == ENC_PGM)
                rc = write_pgm (path, data, width, height);
            else
                rc = write_color_image (enc, path, data, width, height);
            g_free (name);
            g_free (path);
        }
    }

    arv_stream_push_buffer (stream, buffer);
    arv_camera_stop_acquisition (camera, NULL);
    g_object_unref (stream);
    g_object_unref (camera);
    arv_shutdown ();
    return rc;
}

int
main (int argc, char **argv)
{
    const char *opt_serial    = NULL;
    const char *opt_address   = NULL;
    const char *opt_interface = NULL;
    const char *opt_output    = ".";
    const char *opt_encode    = NULL;
    const char *opt_exposure  = NULL;
    const char *opt_binning   = NULL;

    static const struct option long_opts[] = {
        { "serial",    required_argument, NULL, 's' },
        { "address",   required_argument, NULL, 'a' },
        { "interface", required_argument, NULL, 'i' },
        { "output",    required_argument, NULL, 'o' },
        { "encode",    required_argument, NULL, 'e' },
        { "exposure",  required_argument, NULL, 'x' },
        { "binning",   required_argument, NULL, 'b' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long (argc, argv, "s:a:i:o:e:x:b:", long_opts, NULL)) != -1) {
        switch (c) {
            case 's': opt_serial    = optarg; break;
            case 'a': opt_address   = optarg; break;
            case 'i': opt_interface = optarg; break;
            case 'o': opt_output    = optarg; break;
            case 'e': opt_encode    = optarg; break;
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

    double exposure_us = 0.0;
    if (opt_exposure) {
        exposure_us = atof (opt_exposure);
        if (exposure_us <= 0.0) {
            fprintf (stderr, "error: --exposure must be a positive number of microseconds\n\n");
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

    EncFormat enc = ENC_PGM;
    if (opt_encode) {
        if (strcmp (opt_encode, "png") == 0)
            enc = ENC_PNG;
        else if (strcmp (opt_encode, "jpg") == 0 || strcmp (opt_encode, "jpeg") == 0)
            enc = ENC_JPG;
        else {
            fprintf (stderr, "error: --encode must be 'png' or 'jpg'\n\n");
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

    if (g_mkdir_with_parents (opt_output, 0755) != 0) {
        fprintf (stderr, "error: cannot create output directory '%s': %s\n",
                 opt_output, strerror (errno));
        return EXIT_FAILURE;
    }

    if (opt_address) {
        char *resolved_id = resolve_device_id_by_address (opt_address, opt_interface);
        if (resolved_id) {
            printf ("Using discovered device id: %s\n", resolved_id);
            int rc = capture_one_frame (resolved_id, opt_output, iface_ip, enc, exposure_us, binning);
            g_free (resolved_id);
            return rc;
        }

        printf ("Device id not found in discovery; falling back to direct address.\n");
        return capture_one_frame (opt_address, opt_output, iface_ip, enc, exposure_us, binning);
    }

    arv_update_device_list ();
    guint n = arv_get_n_devices ();
    const char *matched_id = NULL;

    for (guint i = 0; i < n; i++) {
        const char *dev_id      = arv_get_device_id         (i);
        const char *dev_address = arv_get_device_address    (i);
        const char *dev_serial  = arv_get_device_serial_nbr (i);

        if (opt_interface && !device_on_interface (dev_address, opt_interface))
            continue;

        if (dev_serial && strcmp (dev_serial, opt_serial) == 0)
            matched_id = dev_id;
    }

    if (!matched_id) {
        fprintf (stderr,
                 "error: serial '%s' not found%s%s\n",
                 opt_serial,
                 opt_interface ? " on interface " : "",
                 opt_interface ? opt_interface : "");
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    return capture_one_frame (matched_id, opt_output, iface_ip, enc, exposure_us, binning);
}
