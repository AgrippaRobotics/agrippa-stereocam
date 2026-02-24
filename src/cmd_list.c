/*
 * cmd_list.c — "ag-cam-tools list" subcommand
 */

#include "common.h"
#include "../vendor/argtable3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *ip;
    const char *model;
    const char *serial;
} CameraRow;

static gboolean
is_gige_protocol (const char *protocol)
{
    if (!protocol)
        return FALSE;
    return g_ascii_strcasecmp (protocol, "GigEVision") == 0 ||
           g_ascii_strcasecmp (protocol, "GEV") == 0;
}

static void
print_separator (size_t ip_w, size_t model_w, size_t serial_w)
{
    printf ("+");
    for (size_t i = 0; i < ip_w + 2; i++) printf ("-");
    printf ("+");
    for (size_t i = 0; i < model_w + 2; i++) printf ("-");
    printf ("+");
    for (size_t i = 0; i < serial_w + 2; i++) printf ("-");
    printf ("+\n");
}

int
cmd_list (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) ctx;

    struct arg_str *cmd       = arg_str1 (NULL, NULL, "list", NULL);
    struct arg_str *interface = arg_str0 ("i", "interface", "<iface>",
                                          "restrict to this NIC");
    struct arg_lit *machine   = arg_lit0 (NULL, "machine-readable",
                                          "tab-separated output for completions");
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (5);
    void *argtable[] = { cmd, interface, machine, help, end };

    int exitcode = EXIT_SUCCESS;
    if (arg_nullcheck (argtable) != 0) {
        arg_dstr_catf (res, "error: insufficient memory\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    int nerrors = arg_parse (argc, argv, argtable);
    if (arg_make_syntax_err_help_msg (res, "list", help->count, nerrors,
                                       argtable, end, &exitcode))
        goto done;

    const char *opt_interface = interface->count ? interface->sval[0] : NULL;

    if (opt_interface) {
        const char *iface_ip = interface_ipv4_address (opt_interface);
        if (!iface_ip) {
            arg_dstr_catf (res, "error: interface '%s' not found or has no IPv4\n",
                           opt_interface);
            exitcode = EXIT_FAILURE;
            goto done;
        }
        if (!machine->count)
            printf ("Interface: %s (%s)\n", opt_interface, iface_ip);
    } else {
        if (!machine->count)
            printf ("Interface: (any)\n");
    }

    arv_update_device_list ();
    guint n = arv_get_n_devices ();

    CameraRow *rows = g_new0 (CameraRow, n);
    guint row_count = 0;

    size_t ip_w     = strlen ("IP");
    size_t model_w  = strlen ("MODEL");
    size_t serial_w = strlen ("SERIAL");

    for (guint i = 0; i < n; i++) {
        const char *protocol = arv_get_device_protocol (i);
        const char *ip       = arv_get_device_address (i);
        const char *model_s  = arv_get_device_model (i);
        const char *serial_s = arv_get_device_serial_nbr (i);

        if (!is_gige_protocol (protocol))
            continue;
        if (opt_interface && !device_on_interface (ip, opt_interface))
            continue;

        rows[row_count].ip     = ip     ? ip     : "(unknown)";
        rows[row_count].model  = model_s  ? model_s  : "(unknown)";
        rows[row_count].serial = serial_s ? serial_s : "(unknown)";

        size_t ip_len     = strlen (rows[row_count].ip);
        size_t model_len  = strlen (rows[row_count].model);
        size_t serial_len = strlen (rows[row_count].serial);
        if (ip_len     > ip_w)     ip_w     = ip_len;
        if (model_len  > model_w)  model_w  = model_len;
        if (serial_len > serial_w) serial_w = serial_len;

        row_count++;
    }

    if (machine->count) {
        /* Tab-separated, no headers — for shell completions. */
        for (guint i = 0; i < row_count; i++)
            printf ("%s\t%s\t%s\n", rows[i].ip, rows[i].model, rows[i].serial);
    } else {
        printf ("GigE cameras: %u\n", row_count);

        print_separator (ip_w, model_w, serial_w);
        printf ("| %-*s | %-*s | %-*s |\n",
                (int) ip_w, "IP",
                (int) model_w, "MODEL",
                (int) serial_w, "SERIAL");
        print_separator (ip_w, model_w, serial_w);

        for (guint i = 0; i < row_count; i++)
            printf ("| %-*s | %-*s | %-*s |\n",
                    (int) ip_w, rows[i].ip,
                    (int) model_w, rows[i].model,
                    (int) serial_w, rows[i].serial);

        print_separator (ip_w, model_w, serial_w);
    }

    g_free (rows);
    arv_shutdown ();

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
