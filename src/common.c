/*
 * common.c — shared implementations for ag-cam-tools
 */

#include "common.h"

#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  Network helpers                                                   */
/* ================================================================== */

const char *
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

gboolean
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

char *
resolve_device_id_by_address (const char *address, const char *opt_interface)
{
    arv_update_device_list ();
    guint n = arv_get_n_devices ();

    for (guint i = 0; i < n; i++) {
        const char *dev_id   = arv_get_device_id (i);
        const char *dev_addr = arv_get_device_address (i);

        if (!dev_addr || strcmp (dev_addr, address) != 0)
            continue;
        if (opt_interface && !device_on_interface (dev_addr, opt_interface))
            continue;

        return g_strdup (dev_id);
    }

    return NULL;
}

const char *
setup_interface (const char *interface_name)
{
    const char *iface_ip = interface_ipv4_address (interface_name);
    if (!iface_ip) {
        fprintf (stderr, "error: interface '%s' not found or has no IPv4 address\n",
                 interface_name);
        return NULL;
    }
    if (setenv ("ARV_INTERFACE", interface_name, 1) != 0) {
        fprintf (stderr, "error: failed to set ARV_INTERFACE=%s: %s\n",
                 interface_name, strerror (errno));
        return NULL;
    }
    printf ("ARV_INTERFACE forced to %s (%s)\n", interface_name, iface_ip);
    return iface_ip;
}

char *
resolve_device (const char *serial, const char *address,
                const char *interface_name, gboolean interactive)
{
    /* Direct address path. */
    if (address) {
        char *id = resolve_device_id_by_address (address, interface_name);
        if (id) {
            printf ("Using discovered device id: %s\n", id);
            return id;
        }
        printf ("Device not found in discovery; using address directly.\n");
        return g_strdup (address);
    }

    /* Serial or interactive discovery. */
    arv_update_device_list ();
    guint n = arv_get_n_devices ();

    /* If serial supplied, match it. */
    if (serial) {
        for (guint i = 0; i < n; i++) {
            const char *dev_id      = arv_get_device_id (i);
            const char *dev_address = arv_get_device_address (i);
            const char *dev_serial  = arv_get_device_serial_nbr (i);

            if (interface_name && !device_on_interface (dev_address, interface_name))
                continue;
            if (dev_serial && strcmp (dev_serial, serial) == 0)
                return g_strdup (dev_id);
        }
        fprintf (stderr, "error: serial '%s' not found%s%s\n",
                 serial,
                 interface_name ? " on interface " : "",
                 interface_name ? interface_name : "");
        return NULL;
    }

    /* Interactive picker. */
    if (!interactive) {
        fprintf (stderr, "error: one of --serial or --address is required\n");
        return NULL;
    }

    /* Collect GigE devices visible on the interface. */
    typedef struct { guint idx; const char *id; const char *addr; const char *serial; const char *model; } Row;
    Row *rows = g_new0 (Row, n);
    guint count = 0;

    for (guint i = 0; i < n; i++) {
        const char *dev_addr = arv_get_device_address (i);
        if (interface_name && !device_on_interface (dev_addr, interface_name))
            continue;
        rows[count].idx    = count;
        rows[count].id     = arv_get_device_id (i);
        rows[count].addr   = dev_addr ? dev_addr : "(unknown)";
        rows[count].serial = arv_get_device_serial_nbr (i);
        rows[count].model  = arv_get_device_model (i);
        if (!rows[count].serial) rows[count].serial = "(unknown)";
        if (!rows[count].model)  rows[count].model  = "(unknown)";
        count++;
    }

    if (count == 0) {
        fprintf (stderr, "error: no cameras discovered%s%s\n",
                 interface_name ? " on interface " : "",
                 interface_name ? interface_name : "");
        g_free (rows);
        return NULL;
    }

    if (count == 1) {
        printf ("Auto-selecting the only camera: %s (%s)\n",
                rows[0].addr, rows[0].model);
        char *result = g_strdup (rows[0].id);
        g_free (rows);
        return result;
    }

    printf ("Available cameras:\n");
    for (guint i = 0; i < count; i++)
        printf ("  [%u]  %-15s  serial=%-16s  model=%s\n",
                i, rows[i].addr, rows[i].serial, rows[i].model);

    printf ("Select camera [0-%u]: ", count - 1);
    fflush (stdout);

    char line[32];
    if (!fgets (line, sizeof line, stdin)) {
        fprintf (stderr, "error: no input\n");
        g_free (rows);
        return NULL;
    }

    unsigned sel = 0;
    if (sscanf (line, "%u", &sel) != 1 || sel >= count) {
        fprintf (stderr, "error: invalid selection\n");
        g_free (rows);
        return NULL;
    }

    char *result = g_strdup (rows[sel].id);
    g_free (rows);
    return result;
}

/* ================================================================== */
/*  Aravis feature helpers                                            */
/* ================================================================== */

void
try_set_string_feature (ArvDevice *device, const char *name, const char *value)
{
    GError *error = NULL;
    arv_device_set_string_feature_value (device, name, value, &error);
    if (error) {
        fprintf (stderr, "warn: failed to set %s=%s: %s\n",
                 name, value, error->message);
        g_clear_error (&error);
    } else {
        printf ("  %s = %s\n", name, value);
    }
}

void
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

void
try_set_float_feature (ArvDevice *device, const char *name, double value)
{
    GError *error = NULL;
    arv_device_set_float_feature_value (device, name, value, &error);
    if (error) {
        fprintf (stderr, "warn: failed to set %s=%g: %s\n",
                 name, value, error->message);
        g_clear_error (&error);
    } else {
        printf ("  %s = %g\n", name, value);
    }
}

gint64
read_integer_feature_or_default (ArvDevice *device, const char *name,
                                 gint64 fallback)
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

gboolean
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

void
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

/* ================================================================== */
/*  Gamma / LUT                                                       */
/* ================================================================== */

static const double k_raw_gamma = 2.5;

const guint8 *
gamma_lut_2p5 (void)
{
    static gboolean initialized = FALSE;
    static guint8 lut[256];
    if (!initialized) {
        double inv_gamma = 1.0 / k_raw_gamma;
        for (int i = 0; i < 256; i++) {
            double x = (double) i / 255.0;
            double y = pow (x, inv_gamma) * 255.0;
            if (y < 0.0) y = 0.0;
            if (y > 255.0) y = 255.0;
            lut[i] = (guint8) y;
        }
        initialized = TRUE;
    }
    return lut;
}

void
apply_lut_inplace (guint8 *data, size_t n, const guint8 lut[256])
{
    for (size_t i = 0; i < n; i++)
        data[i] = lut[data[i]];
}

/* ================================================================== */
/*  Debayer                                                           */
/* ================================================================== */

void
debayer_rg8_to_rgb (const guint8 *bayer, guint8 *rgb,
                    guint width, guint height)
{
    for (guint y = 0; y < height; y++) {
        for (guint x = 0; x < width; x++) {
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

/* ================================================================== */
/*  DualBayer helpers                                                  */
/* ================================================================== */

void
deinterleave_dual_bayer (const guint8 *interleaved, guint width,
                         guint height, guint8 *left, guint8 *right)
{
    guint sub_w = width / 2;
    for (guint y = 0; y < height; y++) {
        const guint8 *row = interleaved + ((size_t) y * (size_t) width);
        guint8 *lrow = left  + ((size_t) y * (size_t) sub_w);
        guint8 *rrow = right + ((size_t) y * (size_t) sub_w);
        for (guint x = 0; x < sub_w; x++) {
            lrow[x] = row[2 * x];
            rrow[x] = row[2 * x + 1];
        }
    }
}

void
software_bin_2x2 (const guint8 *src, guint src_w, guint src_h,
                   guint8 *dst, guint dst_w, guint dst_h)
{
    (void) src_h;
    for (guint y = 0; y < dst_h; y++) {
        guint sy = 2 * y;
        for (guint x = 0; x < dst_w; x++) {
            guint sx = 2 * x;
            size_t i00 = (size_t) sy * src_w + sx;
            size_t i01 = i00 + 1;
            size_t i10 = i00 + src_w;
            size_t i11 = i10 + 1;
            dst[(size_t) y * dst_w + x] = (guint8)
                ((src[i00] + src[i01] + src[i10] + src[i11]) / 4);
        }
    }
}

/* ================================================================== */
/*  Unified camera configuration                                      */
/* ================================================================== */

int
camera_configure (ArvCamera *camera, AgAcquisitionMode mode,
                  int binning, double exposure_us,
                  const char *iface_ip, gboolean verbose,
                  AgCameraConfig *out)
{
    GError *error = NULL;
    ArvDevice *device = arv_camera_get_device (camera);

    memset (out, 0, sizeof *out);
    out->software_binning = 1;

    /* Stop any stale acquisition. */
    printf ("Stopping any stale acquisition...\n");
    arv_camera_stop_acquisition (camera, NULL);
    try_execute_optional_command (device, "TransferStop");
    g_usleep (100000);

    printf ("Configuring...\n");

    /* Acquisition mode. */
    const char *acq_mode = (mode == AG_MODE_SINGLE_FRAME)
                           ? "SingleFrame" : "Continuous";
    try_set_string_feature  (device, "AcquisitionMode", acq_mode);
    try_set_string_feature  (device, "AcquisitionStartMode", "Normal");
    try_set_string_feature  (device, "TriggerSelector", "FrameStart");
    try_set_string_feature  (device, "TriggerMode", "On");
    try_set_string_feature  (device, "TriggerSource", "Software");
    try_set_string_feature  (device, "ImagerOutputSelector", "All");

    /* Binning. */
    try_set_string_feature  (device, "BinningSelector",       "Sensor");
    try_set_integer_feature (device, "BinningHorizontal",     (gint64) binning);
    try_set_integer_feature (device, "BinningVertical",       (gint64) binning);
    try_set_string_feature  (device, "BinningHorizontalMode", "Average");
    try_set_string_feature  (device, "BinningVerticalMode",   "Average");

    gint64 eff_bin_h = 1, eff_bin_v = 1;
    gboolean has_bin_h = try_get_integer_feature (device, "BinningHorizontal", &eff_bin_h);
    gboolean has_bin_v = try_get_integer_feature (device, "BinningVertical",   &eff_bin_v);
    if (binning > 1 &&
        (!has_bin_h || !has_bin_v || eff_bin_h != binning || eff_bin_v != binning)) {
        out->software_binning = binning;
        eff_bin_h = 1;
        eff_bin_v = 1;
        fprintf (stderr,
                 "warn: hardware binning unavailable/ineffective "
                 "(H=%" G_GINT64_FORMAT " V=%" G_GINT64_FORMAT "); "
                 "using %dx software binning\n",
                 eff_bin_h, eff_bin_v, out->software_binning);
    }

    /* Geometry. */
    try_set_integer_feature (device, "OffsetX", 0);
    try_set_integer_feature (device, "OffsetY", 0);
    gint64 target_w = (eff_bin_h > 0) ? (AG_SENSOR_WIDTH  / eff_bin_h) : AG_SENSOR_WIDTH;
    gint64 target_h = (eff_bin_v > 0) ? (AG_SENSOR_HEIGHT / eff_bin_v) : AG_SENSOR_HEIGHT;
    try_set_integer_feature (device, "Width",  target_w);
    try_set_integer_feature (device, "Height", target_h);

    gint64 width_rb  = read_integer_feature_or_default (device, "Width",  target_w);
    gint64 height_rb = read_integer_feature_or_default (device, "Height", target_h);
    if (width_rb != target_w || height_rb != target_h) {
        fprintf (stderr,
                 "warn: geometry readback is %" G_GINT64_FORMAT "x%" G_GINT64_FORMAT
                 " (requested %" G_GINT64_FORMAT "x%" G_GINT64_FORMAT ")\n",
                 width_rb, height_rb, target_w, target_h);
    }
    out->frame_w = (guint) width_rb;
    out->frame_h = (guint) height_rb;

    try_set_string_feature (device, "PixelFormat", "DualBayerRG8");

    if (exposure_us > 0.0)
        try_set_float_feature (device, "ExposureTime", exposure_us);

    /* Transport. */
    try_set_string_feature  (device, "TransferSelector", "Stream0");
    try_set_integer_feature (device, "TransferSelector", 0);
    try_set_string_feature  (device, "TransferControlMode", "Automatic");
    try_set_string_feature  (device, "TransferQueueMode", "FirstInFirstOut");
    try_set_integer_feature (device, "GevSCPSPacketSize", 1400);

    /* macOS: disable PF_PACKET sockets. */
    arv_camera_gv_set_stream_options (camera,
                                       ARV_GV_STREAM_OPTION_PACKET_SOCKET_DISABLED);

    /* Create stream. */
    ArvStream *stream = arv_camera_create_stream (camera, NULL, NULL, &error);
    if (!stream) {
        fprintf (stderr, "error: failed to create stream: %s\n",
                 error ? error->message : "(unknown)");
        g_clear_error (&error);
        return EXIT_FAILURE;
    }

    if (ARV_IS_GV_STREAM (stream)) {
        g_object_set (stream,
                      "packet-resend",   ARV_GV_STREAM_PACKET_RESEND_ALWAYS,
                      "packet-timeout",  (guint) 200000,    /* 200 ms */
                      "frame-retention", (guint) 10000000,  /* 10 s   */
                      NULL);
        if (verbose) {
            guint pt = 0, fr = 0;
            g_object_get (stream, "packet-timeout", &pt, "frame-retention", &fr, NULL);
            printf ("  stream packet-timeout  = %u us\n", pt);
            printf ("  stream frame-retention = %u us\n", fr);
        }
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
            fprintf (stderr, "warn: arv_camera_gv_set_packet_size failed: %s\n",
                     error->message);
            g_clear_error (&error);
        } else {
            printf ("  arv_camera_gv_set_packet_size(1400) OK\n");
        }
    }

    /* Payload and buffers. */
    size_t payload = (size_t) arv_camera_get_payload (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to read payload size: %s\n", error->message);
        g_clear_error (&error);
        g_object_unref (stream);
        return EXIT_FAILURE;
    }
    printf ("  payload = %zu bytes\n", payload);
    out->payload = payload;

    int nbuf = (mode == AG_MODE_SINGLE_FRAME) ? 8 : 16;
    for (int i = 0; i < nbuf; i++)
        arv_stream_push_buffer (stream, arv_buffer_new_allocate (payload));

    /* Verbose diagnostic readback. */
    if (verbose) {
        GError *e = NULL;
        gint64 v;

        v = arv_device_get_integer_feature_value (device, "GevSCDA", &e);
        if (!e) {
            struct in_addr a;
            a.s_addr = htonl ((uint32_t) v);
            char b[INET_ADDRSTRLEN];
            printf ("  GevSCDA        = %s\n", inet_ntop (AF_INET, &a, b, sizeof b));
        }
        g_clear_error (&e);

        v = arv_device_get_integer_feature_value (device, "GevSCPHostPort", &e);
        if (!e) printf ("  GevSCPHostPort = %" G_GINT64_FORMAT "\n", v);
        g_clear_error (&e);

        v = arv_device_get_integer_feature_value (device, "GevSCPSPacketSize", &e);
        if (!e) printf ("  GevSCPSPacketSize = %" G_GINT64_FORMAT "\n", v);
        g_clear_error (&e);

        v = arv_device_get_integer_feature_value (device, "GevCCP", &e);
        if (!e) printf ("  GevCCP = %" G_GINT64_FORMAT "\n", v);
        g_clear_error (&e);

        const char *s;
        s = arv_device_get_string_feature_value (device, "AcquisitionMode", &e);
        if (!e && s) printf ("  AcquisitionMode = %s\n", s);
        g_clear_error (&e);

        s = arv_device_get_string_feature_value (device, "AcquisitionStartMode", &e);
        if (!e && s) printf ("  AcquisitionStartMode = %s\n", s);
        g_clear_error (&e);

        s = arv_device_get_string_feature_value (device, "TriggerMode", &e);
        if (!e && s) printf ("  TriggerMode = %s\n", s);
        g_clear_error (&e);

        s = arv_device_get_string_feature_value (device, "TransferControlMode", &e);
        if (!e && s) printf ("  TransferControlMode = %s\n", s);
        g_clear_error (&e);

        s = arv_device_get_string_feature_value (device, "PixelFormat", &e);
        if (!e && s) printf ("  PixelFormat (readback) = %s\n", s);
        g_clear_error (&e);

        gint64 wrb = arv_device_get_integer_feature_value (device, "Width", &e);
        if (!e) printf ("  Width (readback)       = %" G_GINT64_FORMAT "\n", wrb);
        g_clear_error (&e);
        gint64 hrb = arv_device_get_integer_feature_value (device, "Height", &e);
        if (!e) printf ("  Height (readback)      = %" G_GINT64_FORMAT "\n", hrb);
        g_clear_error (&e);
    }

    out->stream = stream;
    return EXIT_SUCCESS;
}
