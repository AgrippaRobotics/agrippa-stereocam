/*
 * connect.c — minimal Aravis device connection example
 *
 * Usage:
 *   connect -s <serial>  [-i <interface>]
 *   connect -a <address> [-i <interface>]
 *
 * Options:
 *   -s, --serial     <serial>     match device by serial number (requires discovery)
 *   -a, --address    <address>    connect directly by camera IP (bypasses discovery)
 *   -i, --interface  <iface>      restrict serial scan to this NIC (e.g. eno1)
 *
 * Exactly one of --serial or --address is required.
 * --interface is optional and only meaningful with --serial.
 *
 * Build:
 *   make -C ..
 */

#include <arv.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Return the IPv4 address of a named NIC, or NULL if not found.
 * Pointer is into a static buffer — copy before reuse.
 * ---------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 * Return TRUE if device_addr_str falls within the subnet of iface_name.
 *
 * Aravis does not expose which local NIC was used to discover each device,
 * so we infer it by subnet: (device_ip & netmask) == (iface_ip & netmask).
 * ---------------------------------------------------------------------- */
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
             "  %s -s <serial>  [-i <interface>]\n"
             "  %s -a <address> [-i <interface>]\n"
             "\n"
             "Options:\n"
             "  -s, --serial     <serial>    match by serial number (uses discovery)\n"
             "  -a, --address    <address>   connect directly by camera IP\n"
             "  -i, --interface  <iface>     restrict serial scan to this NIC\n",
             prog, prog);
}

/* -------------------------------------------------------------------------
 * Connect and print device info.  Shared by both code paths.
 * ---------------------------------------------------------------------- */
static int
connect_and_print (const char *device_id)
{
    printf ("Connecting to %s ...\n", device_id);

    GError    *error  = NULL;
    ArvCamera *camera = arv_camera_new (device_id, &error);

    if (!camera) {
        fprintf (stderr, "error: %s\n",
                 error ? error->message : "failed to open device");
        g_clear_error (&error);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice  *device     = arv_camera_get_device (camera);
    const char *model      = arv_camera_get_model_name  (camera, NULL);
    const char *vendor     = arv_camera_get_vendor_name (camera, NULL);
    const char *serial_out = arv_device_get_string_feature_value (
                                 device, "DeviceSerialNumber", NULL);

    printf ("Connected!\n");
    printf ("  Vendor : %s\n", vendor     ? vendor     : "(unknown)");
    printf ("  Model  : %s\n", model      ? model      : "(unknown)");
    printf ("  Serial : %s\n", serial_out ? serial_out : "(unknown)");

    g_object_unref (camera);
    arv_shutdown ();
    return EXIT_SUCCESS;
}

int
main (int argc, char **argv)
{
    const char *opt_serial    = NULL;
    const char *opt_address   = NULL;
    const char *opt_interface = NULL;

    static const struct option long_opts[] = {
        { "serial",    required_argument, NULL, 's' },
        { "address",   required_argument, NULL, 'a' },
        { "interface", required_argument, NULL, 'i' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long (argc, argv, "s:a:i:", long_opts, NULL)) != -1) {
        switch (c) {
            case 's': opt_serial    = optarg; break;
            case 'a': opt_address   = optarg; break;
            case 'i': opt_interface = optarg; break;
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

    /* ------------------------------------------------------------------ */
    /* Path A: direct connection by IP address                             */
    /*                                                                     */
    /* GigE Vision discovery relies on UDP broadcast to port 3956, which   */
    /* firewalls often block even when the camera is pingable.  Passing    */
    /* the IP directly to arv_camera_new() opens a unicast GVCP session   */
    /* and bypasses broadcast discovery entirely.                          */
    /* ------------------------------------------------------------------ */
    if (opt_address) {
        if (opt_interface)
            printf ("Interface : %s\n", opt_interface);  /* informational only */
        printf ("Address   : %s\n\n", opt_address);
        return connect_and_print (opt_address);
    }

    /* ------------------------------------------------------------------ */
    /* Path B: discovery by serial number                                  */
    /*                                                                     */
    /* If no devices are found, try:                                       */
    /*   sudo ./bin/connect -s <serial>                                    */
    /* or verify with the Aravis CLI tool:                                 */
    /*   arv-tool-0.8 detect                                               */
    /* ------------------------------------------------------------------ */
    if (opt_interface) {
        const char *iface_ip = interface_ipv4_address (opt_interface);
        if (!iface_ip) {
            fprintf (stderr,
                     "error: interface '%s' not found or has no IPv4 address\n",
                     opt_interface);
            return EXIT_FAILURE;
        }
        printf ("Interface : %s  (%s)\n", opt_interface, iface_ip);
    } else {
        printf ("Interface : (any)\n");
    }
    printf ("Serial    : %s\n\n", opt_serial);

    arv_update_device_list ();

    guint n = arv_get_n_devices ();
    printf ("Discovered %u device(s):\n", n);

    const char *matched_id = NULL;

    for (guint i = 0; i < n; i++) {
        const char *dev_id      = arv_get_device_id        (i);
        const char *dev_address = arv_get_device_address   (i);
        const char *dev_serial  = arv_get_device_serial_nbr(i);
        const char *dev_model   = arv_get_device_model     (i);

        printf ("  [%u]  address=%-15s  serial=%-16s  model=%s\n",
                i,
                dev_address ? dev_address : "(null)",
                dev_serial  ? dev_serial  : "(null)",
                dev_model   ? dev_model   : "(null)");

        if (opt_interface && !device_on_interface (dev_address, opt_interface))
            continue;

        if (dev_serial && strcmp (dev_serial, opt_serial) == 0)
            matched_id = dev_id;
    }

    if (!matched_id) {
        fprintf (stderr,
                 "\nerror: serial '%s' not found%s%s\n"
                 "hint:  try sudo, or use -a <ip> if you know the camera's address\n",
                 opt_serial,
                 opt_interface ? " on interface " : "",
                 opt_interface ? opt_interface    : "");
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    printf ("\n");
    return connect_and_print (matched_id);
}
