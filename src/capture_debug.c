/*
 * capture.c — minimal Aravis single-frame capture tool
 *
 * Usage:
 *   capture -s <serial>  [-i <interface>] [-o <output_dir>] [--width <px>] [--height <px>]
 *   capture -a <address> [-i <interface>] [-o <output_dir>] [--width <px>] [--height <px>]
 *
 * Options:
 *   -s, --serial     <serial>     match device by serial number (requires discovery)
 *   -a, --address    <address>    connect directly by camera IP (bypasses discovery)
 *   -i, --interface  <iface>      force Aravis NIC selection (ARV_INTERFACE)
 *   -o, --output     <dir>        output directory (default: .)
 *       --width      <px>         force Width node before acquisition
 *       --height     <px>         force Height node before acquisition
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

static const char *
interface_ipv4_for_device (const char *iface_name, const char *device_addr_str)
{
    static char buf[INET_ADDRSTRLEN];
    struct in_addr device_addr;

    if (!iface_name || !device_addr_str)
        return NULL;
    if (inet_pton (AF_INET, device_addr_str, &device_addr) != 1)
        return NULL;

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs (&ifaddr) != 0)
        return NULL;

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

        if ((device_addr.s_addr & netmask.s_addr) !=
            (iface_addr.s_addr  & netmask.s_addr))
            continue;

        inet_ntop (AF_INET, &iface_addr, buf, sizeof (buf));
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

static void
print_usage (const char *prog)
{
    fprintf (stderr,
             "Usage:\n"
             "  %s -s <serial>  [-i <interface>] [-o <output_dir>] [--width <px>] [--height <px>]\n"
             "  %s -a <address> [-i <interface>] [-o <output_dir>] [--width <px>] [--height <px>]\n"
             "\n"
             "Options:\n"
             "  -s, --serial     <serial>    match by serial number (uses discovery)\n"
             "  -a, --address    <address>   connect directly by camera IP\n"
             "  -i, --interface  <iface>     force Aravis NIC selection (ARV_INTERFACE)\n"
             "  -o, --output     <dir>       output directory (default: .)\n"
             "      --width      <px>        force Width node before acquisition\n"
             "      --height     <px>        force Height node before acquisition\n",
             prog, prog);
}

static gboolean
feature_is_available (ArvDevice *device, const char *name)
{
    GError *error = NULL;
    gboolean ok = arv_device_is_feature_available (device, name, &error);
    if (error) {
        g_clear_error (&error);
        return FALSE;
    }
    return ok;
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
try_set_boolean_feature (ArvDevice *device, const char *name, gboolean value)
{
    GError *error = NULL;
    arv_device_set_boolean_feature_value (device, name, value, &error);
    if (error) {
        fprintf (stderr, "warn: failed to set %s=%s: %s\n",
                 name, value ? "true" : "false", error->message);
        g_clear_error (&error);
    } else {
        printf ("  %s = %s\n", name, value ? "true" : "false");
    }
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
    if (out_value)
        *out_value = value;
    return TRUE;
}

static gboolean
try_get_integer_bounds (ArvDevice *device, const char *name, gint64 *min_out, gint64 *max_out)
{
    GError *error = NULL;
    gint64 min_v = 0, max_v = 0;
    arv_device_get_integer_feature_bounds (device, name, &min_v, &max_v, &error);
    if (error) {
        g_clear_error (&error);
        return FALSE;
    }
    if (min_out)
        *min_out = min_v;
    if (max_out)
        *max_out = max_v;
    return TRUE;
}

static void
try_execute_command (ArvDevice *device, const char *name)
{
    GError *error = NULL;
    arv_device_execute_command (device, name, &error);
    if (error) {
        fprintf (stderr, "warn: command %s failed: %s\n", name, error->message);
        g_clear_error (&error);
    } else {
        printf ("  %s executed\n", name);
    }
}

static void
try_execute_optional_command (ArvDevice *device, const char *name)
{
    if (feature_is_available (device, name))
        try_execute_command (device, name);
}

static void
try_fire_test_packet (ArvDevice *device)
{
    if (!feature_is_available (device, "GevSCPSFireTestPacket"))
        return;

    GError *error = NULL;
    arv_device_execute_command (device, "GevSCPSFireTestPacket", &error);
    if (!error) {
        printf ("  GevSCPSFireTestPacket executed\n");
        return;
    }
    g_clear_error (&error);

    arv_device_set_boolean_feature_value (device, "GevSCPSFireTestPacket", TRUE, &error);
    if (!error) {
        printf ("  GevSCPSFireTestPacket = true\n");
        return;
    }

    fprintf (stderr, "warn: GevSCPSFireTestPacket unsupported type: %s\n", error->message);
    g_clear_error (&error);
}

static gboolean
ipv4_to_gige_u32 (const char *ip, guint32 *out)
{
    struct in_addr addr;
    if (!ip || !out)
        return FALSE;
    if (inet_pton (AF_INET, ip, &addr) != 1)
        return FALSE;
    *out = ntohl (addr.s_addr);
    return TRUE;
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
print_capture_state (ArvDevice *device, const char *phase)
{
    const char *acq_mode = arv_device_get_string_feature_value (device, "AcquisitionMode", NULL);
    const char *trig_mode = arv_device_get_string_feature_value (device, "TriggerMode", NULL);
    const char *trig_src = arv_device_get_string_feature_value (device, "TriggerSource", NULL);
    const char *trig_sel = arv_device_get_string_feature_value (device, "TriggerSelector", NULL);
    const char *pix_fmt = arv_device_get_string_feature_value (device, "PixelFormat", NULL);
    gint64 width = 0, height = 0, payload = 0;
    gint64 tl_locked = 0;

    gboolean has_w = try_get_integer_feature (device, "Width", &width);
    gboolean has_h = try_get_integer_feature (device, "Height", &height);
    gboolean has_p = try_get_integer_feature (device, "PayloadSize", &payload);
    gboolean has_tl_locked = try_get_integer_feature (device, "TLParamsLocked", &tl_locked);

    printf ("State (%s):\n", phase);
    printf ("  AcquisitionMode = %s\n", acq_mode ? acq_mode : "(n/a)");
    printf ("  TriggerSelector = %s\n", trig_sel ? trig_sel : "(n/a)");
    printf ("  TriggerMode     = %s\n", trig_mode ? trig_mode : "(n/a)");
    printf ("  TriggerSource   = %s\n", trig_src ? trig_src : "(n/a)");
    printf ("  PixelFormat     = %s\n", pix_fmt ? pix_fmt : "(n/a)");
    if (has_w && has_h)
        printf ("  Width/Height    = %" G_GINT64_FORMAT " x %" G_GINT64_FORMAT "\n", width, height);
    if (has_p)
        printf ("  PayloadSize     = %" G_GINT64_FORMAT "\n", payload);
    if (has_tl_locked)
        printf ("  TLParamsLocked  = %" G_GINT64_FORMAT "\n", tl_locked);
}

static ArvBuffer *
wait_for_success_buffer (ArvDevice *device, ArvStream *stream, gboolean software_trigger, int attempts)
{
    for (int i = 0; i < attempts; i++) {
        if (software_trigger)
            try_execute_command (device, "TriggerSoftware");

        ArvBuffer *buffer = arv_stream_timeout_pop_buffer (stream, 1000000);
        if (!buffer)
            continue;
        if (arv_buffer_get_status (buffer) == ARV_BUFFER_STATUS_SUCCESS)
            return buffer;

        arv_stream_push_buffer (stream, buffer);
    }

    return NULL;
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

static int
write_dual_bayer_pair (const char *output_dir,
                       const char *basename_no_ext,
                       const guint8 *interleaved,
                       guint width,
                       guint height)
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
        guint8 *lrow = left + ((size_t) y * (size_t) sub_w);
        guint8 *rrow = right + ((size_t) y * (size_t) sub_w);
        for (guint x = 0; x < sub_w; x++) {
            lrow[x] = row[2 * x];
            rrow[x] = row[2 * x + 1];
        }
    }

    char *left_base = g_strdup_printf ("%s_left.pgm", basename_no_ext);
    char *right_base = g_strdup_printf ("%s_right.pgm", basename_no_ext);
    char *left_path = g_build_filename (output_dir, left_base, NULL);
    char *right_path = g_build_filename (output_dir, right_base, NULL);

    int rc_left = write_pgm (left_path, left, sub_w, height);
    int rc_right = write_pgm (right_path, right, sub_w, height);

    if (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS) {
        printf ("Saved: %s  (%ux%u, BayerRG8 left)\n", left_path, sub_w, height);
        printf ("Saved: %s  (%ux%u, BayerRG8 right)\n", right_path, sub_w, height);
    }

    g_free (left_base);
    g_free (right_base);
    g_free (left_path);
    g_free (right_path);
    g_free (left);
    g_free (right);
    return (rc_left == EXIT_SUCCESS && rc_right == EXIT_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int
capture_one_frame (const char *device_id,
                   const char *output_dir,
                   const char *interface_ip,
                   gint64 forced_width,
                   gint64 forced_height)
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
    const char *vendor = arv_camera_get_vendor_name (camera, NULL);
    const char *model = arv_camera_get_model_name (camera, NULL);
    const char *serial = arv_device_get_string_feature_value (device, "DeviceSerialNumber", NULL);

    printf ("Connected.\n");
    printf ("  Vendor : %s\n", vendor ? vendor : "(unknown)");
    printf ("  Model  : %s\n", model ? model : "(unknown)");
    printf ("  Serial : %s\n", serial ? serial : "(unknown)");
    printf ("Configuring stream defaults...\n");

    /*
     * Known working on this setup:
     * - Control plane (GVCP) read/write works.
     * - GevSCDA and GevSCPHostPort read back as expected.
     * - GevSCPSFireTestPacket reaches host socket.
     *
     * Known not working yet:
     * - No completed GVSP frame buffers are received for image capture.
     */

    if (feature_is_available (device, "UserSetSelector") &&
        feature_is_available (device, "UserSetLoad")) {
        try_set_string_feature (device, "UserSetSelector", "Default");
        try_execute_optional_command (device, "UserSetLoad");
        g_usleep (200000);
    }

    try_set_string_feature (device, "AcquisitionMode", "Continuous");
    try_set_string_feature (device, "TriggerSelector", "FrameStart");
    try_set_string_feature (device, "TriggerMode", "Off");
    try_set_string_feature (device, "ImagerOutputSelector", "All");
    try_set_string_feature (device, "PixelFormat", "DualBayerRG8");

    if (forced_width > 0) {
        gint64 min_w = 0, max_w = 0;
        if (try_get_integer_bounds (device, "Width", &min_w, &max_w) &&
            (forced_width < min_w || forced_width > max_w)) {
            fprintf (stderr,
                     "warn: requested Width=%" G_GINT64_FORMAT
                     " out of range [%" G_GINT64_FORMAT ", %" G_GINT64_FORMAT "]\n",
                     forced_width, min_w, max_w);
        } else {
            try_set_integer_feature (device, "Width", forced_width);
        }
    }

    if (forced_height > 0) {
        gint64 min_h = 0, max_h = 0;
        if (try_get_integer_bounds (device, "Height", &min_h, &max_h) &&
            (forced_height < min_h || forced_height > max_h)) {
            fprintf (stderr,
                     "warn: requested Height=%" G_GINT64_FORMAT
                     " out of range [%" G_GINT64_FORMAT ", %" G_GINT64_FORMAT "]\n",
                     forced_height, min_h, max_h);
        } else {
            try_set_integer_feature (device, "Height", forced_height);
        }
    }

    try_set_integer_feature (device, "GevStreamChannelSelector", 0);
    if (interface_ip) {
        guint32 scda = 0;
        if (ipv4_to_gige_u32 (interface_ip, &scda))
            try_set_integer_feature (device, "GevSCDA", (gint64) scda);
    }

    error = NULL;
    arv_camera_gv_auto_packet_size (camera, &error);
    if (!error)
        printf ("  GvAutoPacketSize = negotiated\n");
    else
        g_clear_error (&error);

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
                      "socket-buffer", ARV_GV_STREAM_SOCKET_BUFFER_AUTO,
                      "packet-resend", ARV_GV_STREAM_PACKET_RESEND_ALWAYS,
                      NULL);

        guint16 stream_port = arv_gv_stream_get_port (ARV_GV_STREAM (stream));
        if (stream_port > 0) {
            try_set_integer_feature (device, "GevSCPHostPort", (gint64) stream_port);
            printf ("  GevSCPHostPort target = %u\n", (unsigned) stream_port);
        }
    }

    gint64 scda_readback = 0, port_readback = 0;
    if (try_get_integer_feature (device, "GevSCDA", &scda_readback))
        printf ("  GevSCDA (readback) = %" G_GINT64_FORMAT "\n", scda_readback);
    if (try_get_integer_feature (device, "GevSCPHostPort", &port_readback))
        printf ("  GevSCPHostPort (readback) = %" G_GINT64_FORMAT "\n", port_readback);
    try_fire_test_packet (device);

    size_t payload = (size_t) arv_camera_get_payload (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to read payload size: %s\n", error->message);
        g_clear_error (&error);
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }
    printf ("  arv_camera_get_payload = %zu\n", payload);

    for (int i = 0; i < 4; i++)
        arv_stream_push_buffer (stream, arv_buffer_new_allocate (payload));

    try_set_integer_feature (device, "TLParamsLocked", 1);
    print_capture_state (device, "free-run");

    arv_camera_start_acquisition (camera, &error);
    if (error) {
        fprintf (stderr, "error: failed to start acquisition: %s\n", error->message);
        g_clear_error (&error);
        try_set_integer_feature (device, "TLParamsLocked", 0);
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    g_usleep (100000);
    ArvBuffer *buffer = wait_for_success_buffer (device, stream, FALSE, 5);

    if (!buffer) {
        printf ("No frame in free-run mode, retrying with software trigger...\n");
        arv_camera_stop_acquisition (camera, NULL);
        try_set_string_feature (device, "TriggerSelector", "FrameStart");
        try_set_string_feature (device, "TriggerMode", "On");
        try_set_string_feature (device, "TriggerSource", "Software");
        try_set_string_feature (device, "AcquisitionMode", "Continuous");
        print_capture_state (device, "continuous + software trigger");

        arv_camera_start_acquisition (camera, &error);
        if (error) {
            fprintf (stderr, "error: failed to restart acquisition: %s\n", error->message);
            g_clear_error (&error);
            try_set_integer_feature (device, "TLParamsLocked", 0);
            g_object_unref (stream);
            g_object_unref (camera);
            arv_shutdown ();
            return EXIT_FAILURE;
        }

        g_usleep (100000);
        buffer = wait_for_success_buffer (device, stream, TRUE, 10);
    }

    if (!buffer) {
        fprintf (stderr, "error: timeout waiting for frame (no successful buffer)\n");
        if (ARV_IS_GV_STREAM (stream)) {
            guint64 resent = 0, missing = 0;
            arv_gv_stream_get_statistics (ARV_GV_STREAM (stream), &resent, &missing);
            fprintf (stderr, "debug: gv statistics: resent=%" G_GUINT64_FORMAT
                     " missing=%" G_GUINT64_FORMAT "\n", resent, missing);
        }
        arv_camera_stop_acquisition (camera, NULL);
        try_set_integer_feature (device, "TLParamsLocked", 0);
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    if (arv_buffer_get_status (buffer) != ARV_BUFFER_STATUS_SUCCESS) {
        fprintf (stderr, "error: frame acquisition failed (status=%d)\n",
                 arv_buffer_get_status (buffer));
        arv_stream_push_buffer (stream, buffer);
        arv_camera_stop_acquisition (camera, NULL);
        try_set_integer_feature (device, "TLParamsLocked", 0);
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
    const char *pixel_format = arv_device_get_string_feature_value (device, "PixelFormat", NULL);

    if (!data || data_size < needed) {
        fprintf (stderr, "error: unsupported frame buffer size (%zu bytes for %ux%u)\n",
                 data_size, width, height);
        arv_stream_push_buffer (stream, buffer);
        arv_camera_stop_acquisition (camera, NULL);
        try_set_integer_feature (device, "TLParamsLocked", 0);
        g_object_unref (stream);
        g_object_unref (camera);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    time_t now = time (NULL);
    struct tm tm_now;
    localtime_r (&now, &tm_now);
    char base[64];
    strftime (base, sizeof (base), "capture_%Y%m%d_%H%M%S", &tm_now);

    int rc = EXIT_FAILURE;
    if (pixel_format && strcmp (pixel_format, "DualBayerRG8") == 0) {
        printf ("PixelFormat: %s (interleaved dual stream)\n", pixel_format);
        rc = write_dual_bayer_pair (output_dir, base, data, width, height);
    } else {
        char *mono_base = g_strdup_printf ("%s.pgm", base);
        char *out_path = g_build_filename (output_dir, mono_base, NULL);
        rc = write_pgm (out_path, data, width, height);
        if (rc == EXIT_SUCCESS)
            printf ("Saved: %s  (%ux%u, raw PGM)\n", out_path, width, height);
        g_free (mono_base);
        g_free (out_path);
    }

    arv_stream_push_buffer (stream, buffer);
    arv_camera_stop_acquisition (camera, NULL);
    try_set_integer_feature (device, "TLParamsLocked", 0);
    g_object_unref (stream);
    g_object_unref (camera);
    arv_shutdown ();
    return rc;
}

int
main (int argc, char **argv)
{
    const char *opt_serial = NULL;
    const char *opt_address = NULL;
    const char *opt_interface = NULL;
    const char *opt_output = ".";
    const char *iface_ip = NULL;
    gint64 opt_width = -1;
    gint64 opt_height = -1;

    static const struct option long_opts[] = {
        { "serial",    required_argument, NULL, 's' },
        { "address",   required_argument, NULL, 'a' },
        { "interface", required_argument, NULL, 'i' },
        { "output",    required_argument, NULL, 'o' },
        { "width",     required_argument, NULL, 1001 },
        { "height",    required_argument, NULL, 1002 },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long (argc, argv, "s:a:i:o:", long_opts, NULL)) != -1) {
        switch (c) {
            case 's': opt_serial = optarg; break;
            case 'a': opt_address = optarg; break;
            case 'i': opt_interface = optarg; break;
            case 'o': opt_output = optarg; break;
            case 1001: opt_width = g_ascii_strtoll (optarg, NULL, 10); break;
            case 1002: opt_height = g_ascii_strtoll (optarg, NULL, 10); break;
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

    if (opt_interface) {
        if (opt_address)
            iface_ip = interface_ipv4_for_device (opt_interface, opt_address);
        if (!iface_ip)
            iface_ip = interface_ipv4_address (opt_interface);
        if (!iface_ip) {
            fprintf (stderr,
                     "error: interface '%s' not found or has no IPv4 address\n",
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

    if (opt_width > 0 || opt_height > 0)
        printf ("Frame request: %" G_GINT64_FORMAT " x %" G_GINT64_FORMAT "\n",
                opt_width, opt_height);

    if (opt_address) {
        if (opt_interface)
            printf ("Interface : %s  (%s)\n", opt_interface, iface_ip);
        printf ("Address   : %s\n", opt_address);
        printf ("Output    : %s\n\n", opt_output);

        char *resolved_id = resolve_device_id_by_address (opt_address, opt_interface);
        if (resolved_id) {
            printf ("Using discovered device id: %s\n\n", resolved_id);
            int rc = capture_one_frame (resolved_id, opt_output, iface_ip, opt_width, opt_height);
            g_free (resolved_id);
            return rc;
        }

        printf ("Device id not found in discovery; falling back to direct address.\n\n");
        return capture_one_frame (opt_address, opt_output, iface_ip, opt_width, opt_height);
    }

    if (opt_interface)
        printf ("Interface : %s  (%s)\n", opt_interface, iface_ip);
    else
        printf ("Interface : (any)\n");

    printf ("Serial    : %s\n", opt_serial);
    printf ("Output    : %s\n\n", opt_output);

    arv_update_device_list ();
    guint n = arv_get_n_devices ();
    printf ("Discovered %u device(s):\n", n);

    const char *matched_id = NULL;
    const char *matched_address = NULL;
    for (guint i = 0; i < n; i++) {
        const char *dev_id      = arv_get_device_id         (i);
        const char *dev_address = arv_get_device_address    (i);
        const char *dev_serial  = arv_get_device_serial_nbr (i);
        const char *dev_model   = arv_get_device_model      (i);

        printf ("  [%u]  address=%-15s  serial=%-16s  model=%s\n",
                i,
                dev_address ? dev_address : "(null)",
                dev_serial  ? dev_serial  : "(null)",
                dev_model   ? dev_model   : "(null)");

        if (opt_interface && !device_on_interface (dev_address, opt_interface))
            continue;

        if (dev_serial && strcmp (dev_serial, opt_serial) == 0) {
            matched_id = dev_id;
            matched_address = dev_address;
        }
    }

    if (!matched_id) {
        fprintf (stderr,
                 "\nerror: serial '%s' not found%s%s\n"
                 "hint:  try sudo, or use -a <ip> if you know the camera's address\n",
                 opt_serial,
                 opt_interface ? " on interface " : "",
                 opt_interface ? opt_interface : "");
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    if (opt_interface && matched_address) {
        const char *best_ip = interface_ipv4_for_device (opt_interface, matched_address);
        if (best_ip) {
            iface_ip = best_ip;
            printf ("Matched interface IPv4 for %s: %s\n", matched_address, iface_ip);
        }
    }

    printf ("\n");
    return capture_one_frame (matched_id, opt_output, iface_ip, opt_width, opt_height);
}
