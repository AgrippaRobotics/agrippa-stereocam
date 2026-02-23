/*
 * list_cameras.c — list discovered GigE cameras as a table
 *
 * Usage:
 *   list_cameras [-i <interface>]
 *
 * Options:
 *   -i, --interface <iface>   restrict to cameras reachable on this NIC
 *       --interfave <iface>   alias for --interface (typo compatibility)
 */

#include <arv.h>

#include <arpa/inet.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *ip;
    const char *model;
    const char *serial;
} CameraRow;

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

static gboolean
is_gige_protocol (const char *protocol)
{
    if (!protocol)
        return FALSE;

    return g_ascii_strcasecmp (protocol, "GigEVision") == 0 ||
           g_ascii_strcasecmp (protocol, "GEV") == 0;
}

static void
print_usage (const char *prog)
{
    fprintf (stderr,
             "Usage:\n"
             "  %s [-i <interface>]\n"
             "\n"
             "Options:\n"
             "  -i, --interface  <iface>   restrict to this NIC\n"
             "      --interfave  <iface>   alias for --interface\n",
             prog);
}

static void
print_separator (size_t ip_w, size_t model_w, size_t serial_w)
{
    size_t i;
    printf ("+");
    for (i = 0; i < ip_w + 2; i++) printf ("-");
    printf ("+");
    for (i = 0; i < model_w + 2; i++) printf ("-");
    printf ("+");
    for (i = 0; i < serial_w + 2; i++) printf ("-");
    printf ("+\n");
}

int
main (int argc, char **argv)
{
    const char *opt_interface = NULL;

    static const struct option long_opts[] = {
        { "interface", required_argument, NULL, 'i' },
        { "interfave", required_argument, NULL, 'i' },
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long (argc, argv, "i:", long_opts, NULL)) != -1) {
        switch (c) {
            case 'i': opt_interface = optarg; break;
            default:
                print_usage (argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (opt_interface) {
        const char *iface_ip = interface_ipv4_address (opt_interface);
        if (!iface_ip) {
            fprintf (stderr,
                     "error: interface '%s' not found or has no IPv4 address\n",
                     opt_interface);
            return EXIT_FAILURE;
        }
        printf ("Interface: %s (%s)\n", opt_interface, iface_ip);
    } else {
        printf ("Interface: (any)\n");
    }

    arv_update_device_list ();
    guint n = arv_get_n_devices ();

    CameraRow *rows = g_new0 (CameraRow, n);
    guint row_count = 0;

    size_t ip_w = strlen ("IP");
    size_t model_w = strlen ("MODEL");
    size_t serial_w = strlen ("SERIAL");

    for (guint i = 0; i < n; i++) {
        const char *protocol = arv_get_device_protocol (i);
        const char *ip = arv_get_device_address (i);
        const char *model = arv_get_device_model (i);
        const char *serial = arv_get_device_serial_nbr (i);

        if (!is_gige_protocol (protocol))
            continue;
        if (opt_interface && !device_on_interface (ip, opt_interface))
            continue;

        rows[row_count].ip = ip ? ip : "(unknown)";
        rows[row_count].model = model ? model : "(unknown)";
        rows[row_count].serial = serial ? serial : "(unknown)";

        size_t ip_len = strlen (rows[row_count].ip);
        size_t model_len = strlen (rows[row_count].model);
        size_t serial_len = strlen (rows[row_count].serial);

        if (ip_len > ip_w)
            ip_w = ip_len;
        if (model_len > model_w)
            model_w = model_len;
        if (serial_len > serial_w)
            serial_w = serial_len;

        row_count++;
    }

    printf ("GigE cameras: %u\n", row_count);

    print_separator (ip_w, model_w, serial_w);
    printf ("| %-*s | %-*s | %-*s |\n",
            (int) ip_w, "IP",
            (int) model_w, "MODEL",
            (int) serial_w, "SERIAL");
    print_separator (ip_w, model_w, serial_w);

    for (guint i = 0; i < row_count; i++) {
        printf ("| %-*s | %-*s | %-*s |\n",
                (int) ip_w, rows[i].ip,
                (int) model_w, rows[i].model,
                (int) serial_w, rows[i].serial);
    }

    print_separator (ip_w, model_w, serial_w);

    g_free (rows);
    arv_shutdown ();
    return EXIT_SUCCESS;
}
