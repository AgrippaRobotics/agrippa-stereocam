/*
 * main.c — ag-cam-tools entry point and subcommand dispatch
 */

#include "../vendor/argtable3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Subcommand handlers (defined in cmd_*.c). */
int cmd_connect (int argc, char *argv[], arg_dstr_t res, void *ctx);
int cmd_list    (int argc, char *argv[], arg_dstr_t res, void *ctx);
int cmd_capture (int argc, char *argv[], arg_dstr_t res, void *ctx);
int cmd_stream  (int argc, char *argv[], arg_dstr_t res, void *ctx);
int cmd_focus   (int argc, char *argv[], arg_dstr_t res, void *ctx);
int cmd_calibration_capture (int argc, char *argv[], arg_dstr_t res, void *ctx);
int cmd_depth_preview (int argc, char *argv[], arg_dstr_t res, void *ctx);

static void
print_usage (void)
{
    printf ("ag-cam-tools v1.0.0 — PDH016S stereo camera toolkit\n"
            "\n"
            "Usage:\n"
            "  ag-cam-tools <command> [options]\n"
            "\n"
            "Commands:\n"
            "  connect   Connect to a camera and print device info\n"
            "  list      Discover and list GigE cameras\n"
            "  capture   Capture a single stereo frame pair\n"
            "  stream    Real-time stereo preview via SDL2\n"
            "  focus     Real-time focus scoring for lens adjustment\n"
            "  calibration-capture\n"
            "            Interactive stereo pair capture for calibration\n"
            "  depth-preview\n"
            "            Live depth map with selectable stereo backend\n"
            "\n"
            "Run 'ag-cam-tools <command> --help' for command-specific options.\n");
}

int
main (int argc, char *argv[])
{
    arg_set_module_name ("ag-cam-tools");
    arg_set_module_version (1, 0, 0, "");

    arg_cmd_init ();
    arg_cmd_register ("connect", cmd_connect,
                      "Connect to a camera and print device info", NULL);
    arg_cmd_register ("list",    cmd_list,
                      "Discover and list GigE cameras", NULL);
    arg_cmd_register ("capture", cmd_capture,
                      "Capture a single stereo frame pair", NULL);
    arg_cmd_register ("stream",  cmd_stream,
                      "Real-time stereo preview via SDL2", NULL);
    arg_cmd_register ("focus",   cmd_focus,
                      "Real-time focus scoring for lens adjustment", NULL);
    arg_cmd_register ("calibration-capture", cmd_calibration_capture,
                      "Interactive stereo pair capture for calibration", NULL);
    arg_cmd_register ("depth-preview", cmd_depth_preview,
                      "Live depth map with selectable stereo backend", NULL);

    if (argc < 2 ||
        strcmp (argv[1], "--help") == 0 ||
        strcmp (argv[1], "-h") == 0) {
        print_usage ();
        arg_cmd_uninit ();
        return EXIT_SUCCESS;
    }

    /* Check that the subcommand exists before dispatching. */
    if (!arg_cmd_info (argv[1])) {
        fprintf (stderr, "error: unknown command '%s'\n\n", argv[1]);
        print_usage ();
        arg_cmd_uninit ();
        return EXIT_FAILURE;
    }

    arg_dstr_t res = arg_dstr_create ();
    int rv = arg_cmd_dispatch (argv[1], argc, argv, res);
    const char *output = arg_dstr_cstr (res);
    if (output && output[0])
        printf ("%s\n", output);

    arg_dstr_destroy (res);
    arg_cmd_uninit ();
    return rv;
}
