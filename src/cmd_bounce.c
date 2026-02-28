/*
 * cmd_bounce.c — "ag-cam-tools bounce" subcommand
 *
 * Issues a GenICam DeviceReset to power-cycle the camera over GigE,
 * then optionally waits for the camera to reappear on the network.
 */

#include "common.h"
#include "../vendor/argtable3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
cmd_bounce (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) ctx;

    struct arg_str *cmd       = arg_str1 (NULL, NULL, "bounce", NULL);
    struct arg_str *serial    = arg_str0 ("s", "serial",    "<serial>",
                                          "match by serial number");
    struct arg_str *address   = arg_str0 ("a", "address",   "<address>",
                                          "connect by camera IP");
    struct arg_str *interface = arg_str0 ("i", "interface",  "<iface>",
                                          "restrict to this NIC");
    struct arg_lit *no_wait   = arg_lit0 (NULL, "no-wait",
                                          "exit immediately after reset");
    struct arg_int *timeout   = arg_int0 (NULL, "timeout",   "<seconds>",
                                          "wait timeout (default 30)");
    struct arg_lit *help      = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end       = arg_end (5);
    void *argtable[] = { cmd, serial, address, interface, no_wait, timeout,
                         help, end };

    int exitcode = EXIT_SUCCESS;
    if (arg_nullcheck (argtable) != 0) {
        arg_dstr_catf (res, "error: insufficient memory\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    int nerrors = arg_parse (argc, argv, argtable);
    if (arg_make_syntax_err_help_msg (res, "bounce", help->count, nerrors,
                                       argtable, end, &exitcode))
        goto done;

    if (serial->count && address->count) {
        arg_dstr_catf (res, "error: --serial and --address are mutually "
                            "exclusive\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    const char *opt_serial    = serial->count    ? serial->sval[0]    : NULL;
    const char *opt_address   = address->count   ? address->sval[0]   : NULL;
    const char *opt_interface = interface->count  ? interface->sval[0] : NULL;
    int         wait          = !no_wait->count;
    int         timeout_s     = timeout->count    ? timeout->ival[0]   : 30;

    if (opt_interface) {
        const char *ip = setup_interface (opt_interface);
        if (!ip) { exitcode = EXIT_FAILURE; goto done; }
    }

    char *device_id = resolve_device (opt_serial, opt_address, opt_interface,
                                      TRUE);
    if (!device_id) {
        exitcode = EXIT_FAILURE;
        goto done;
    }

    printf ("Connecting to %s ...\n", device_id);

    GError    *error  = NULL;
    ArvCamera *camera = arv_camera_new (device_id, &error);
    if (!camera) {
        fprintf (stderr, "error: %s\n",
                 error ? error->message : "failed to open device");
        g_clear_error (&error);
        g_free (device_id);
        arv_shutdown ();
        exitcode = EXIT_FAILURE;
        goto done;
    }

    ArvDevice  *device     = arv_camera_get_device (camera);
    const char *model      = arv_camera_get_model_name  (camera, NULL);
    const char *vendor     = arv_camera_get_vendor_name (camera, NULL);
    const char *serial_str = arv_device_get_string_feature_value (
                                 device, "DeviceSerialNumber", NULL);

    printf ("  Vendor : %s\n", vendor     ? vendor     : "(unknown)");
    printf ("  Model  : %s\n", model      ? model      : "(unknown)");
    printf ("  Serial : %s\n", serial_str ? serial_str : "(unknown)");

    /* Check that the camera supports DeviceReset. */
    gboolean has_reset = arv_device_is_feature_available (device,
                                                          "DeviceReset",
                                                          NULL);
    if (!has_reset) {
        fprintf (stderr, "error: camera does not support DeviceReset\n");
        g_object_unref (camera);
        g_free (device_id);
        arv_shutdown ();
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Save serial for rediscovery after the reset. */
    char *saved_serial = serial_str ? g_strdup (serial_str) : NULL;

    printf ("Resetting camera...\n");
    arv_device_execute_command (device, "DeviceReset", &error);
    if (error) {
        fprintf (stderr, "error: DeviceReset failed: %s\n", error->message);
        g_clear_error (&error);
        g_free (saved_serial);
        g_object_unref (camera);
        g_free (device_id);
        arv_shutdown ();
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Camera handle is now invalid — unref and move on. */
    g_object_unref (camera);
    camera = NULL;

    if (!wait) {
        printf ("Reset issued.  (--no-wait: not waiting for reboot)\n");
    } else if (!saved_serial) {
        fprintf (stderr, "warn: could not read serial — cannot wait for "
                         "reboot\n");
    } else {
        printf ("Waiting up to %d s for camera to come back...\n", timeout_s);

        gboolean found = FALSE;
        for (int elapsed = 0; elapsed < timeout_s; elapsed++) {
            g_usleep (1000000);   /* 1 s */
            arv_update_device_list ();
            unsigned int n = arv_get_n_devices ();
            for (unsigned int i = 0; i < n; i++) {
                const char *sn = arv_get_device_serial_nbr (i);
                if (sn && strcmp (sn, saved_serial) == 0) {
                    printf ("Camera back online (%d s).\n", elapsed + 1);
                    found = TRUE;
                    break;
                }
            }
            if (found)
                break;
        }

        if (!found) {
            fprintf (stderr, "Timed out after %d s waiting for camera.\n",
                     timeout_s);
            exitcode = EXIT_FAILURE;
        }
    }

    g_free (saved_serial);
    g_free (device_id);
    arv_shutdown ();

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
