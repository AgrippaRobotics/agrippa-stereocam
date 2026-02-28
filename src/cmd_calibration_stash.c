/*
 * cmd_calibration_stash.c — "ag-cam-tools calibration-stash" subcommand
 *
 * Upload, list, or delete calibration data stored on the camera's
 * persistent UserFile storage.  Supports up to AG_MAX_SLOTS calibration
 * slots in a single file using the AGMS multi-slot container format.
 *
 * Usage:
 *   ag-cam-tools calibration-stash list     [--slot N] [device-opts]
 *   ag-cam-tools calibration-stash upload   [--slot N] [device-opts] <session>
 *   ag-cam-tools calibration-stash download [--slot N] -o <dir> [device-opts]
 *   ag-cam-tools calibration-stash delete    --slot N  [device-opts]
 */

#include "common.h"
#include "calib_archive.h"
#include "device_file.h"
#include "../vendor/argtable3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USER_FILE  "UserFile1"

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void
print_stash_usage (void)
{
    printf ("Usage:\n"
            "  ag-cam-tools calibration-stash list     [--slot N] [device-opts]\n"
            "  ag-cam-tools calibration-stash upload   [--slot N] [device-opts] <session>\n"
            "  ag-cam-tools calibration-stash download [--slot N] -o <dir> [device-opts]\n"
            "  ag-cam-tools calibration-stash delete    --slot N  [device-opts]\n"
            "  ag-cam-tools calibration-stash purge     [device-opts]\n"
            "\n"
            "Actions:\n"
            "  list      Show storage info and calibration slot contents\n"
            "  upload    Pack a calibration session and write it to a slot\n"
            "  download  Download a calibration slot to a local directory\n"
            "  delete    Remove a calibration slot from the camera\n"
            "  purge     Delete the entire calibration file from the camera\n"
            "\n"
            "Options:\n"
            "      --slot <0|1|2>       Calibration slot (default: 0)\n"
            "  -o, --output <dir>       Output directory (for download)\n"
            "  -s, --serial <serial>    Match by serial number\n"
            "  -a, --address <address>  Connect by camera IP\n"
            "  -i, --interface <iface>  Force NIC selection\n"
            "  -h, --help               Print this help\n");
}

/*
 * Connect to a camera and return an ArvCamera*.
 * Handles interface setup and device resolution.
 * On error returns NULL and prints a diagnostic.
 */
static ArvCamera *
connect_camera (const char *opt_serial, const char *opt_address,
                const char *opt_interface)
{
    if (opt_interface) {
        if (!setup_interface (opt_interface))
            return NULL;
    }

    char *device_id = resolve_device (opt_serial, opt_address,
                                       opt_interface, TRUE);
    if (!device_id)
        return NULL;

    GError *error = NULL;
    ArvCamera *camera = arv_camera_new (device_id, &error);
    g_free (device_id);

    if (!camera) {
        fprintf (stderr, "error: %s\n",
                 error ? error->message : "failed to open device");
        g_clear_error (&error);
        return NULL;
    }

    return camera;
}

/* ------------------------------------------------------------------ */
/*  list                                                               */
/* ------------------------------------------------------------------ */

static int
stash_list (const char *opt_serial, const char *opt_address,
            const char *opt_interface)
{
    ArvCamera *camera = connect_camera (opt_serial, opt_address, opt_interface);
    if (!camera) {
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);
    int exitcode = EXIT_SUCCESS;

    /* Storage info. */
    int64_t file_size = 0, total = 0, used = 0, avail = 0;
    ag_device_file_info (device, USER_FILE,
                         &file_size, &total, &used, &avail);

    printf ("Camera file storage (%s):\n", USER_FILE);
    printf ("  Total:     %8" G_GINT64_FORMAT " bytes (%.1f MB)\n",
            (gint64) total, (double) total / (1024.0 * 1024.0));
    printf ("  Used:      %8" G_GINT64_FORMAT " bytes (%.1f MB)\n",
            (gint64) used, (double) used / (1024.0 * 1024.0));
    printf ("  Available: %8" G_GINT64_FORMAT " bytes (%.1f MB)\n",
            (gint64) avail, (double) avail / (1024.0 * 1024.0));
    printf ("  File size: %8" G_GINT64_FORMAT " bytes\n", (gint64) file_size);

    if (file_size > 0) {
        printf ("\n");

        /*
         * Fast path: read only the first 4 KB to identify the format
         * and show calibration summary without downloading the full file.
         */
        uint8_t *hdr_data = NULL;
        size_t   hdr_len  = 0;

        int head_rc = ag_device_file_read_head (device, USER_FILE,
                                                  AG_MULTISLOT_HEADER_SIZE,
                                                  &hdr_data, &hdr_len);
        if (head_rc != 0 || hdr_len < 4) {
            /* Can't read header — try full download as fallback. */
            g_free (hdr_data);
            uint8_t *data = NULL;
            size_t len = 0;
            if (ag_device_file_read (device, USER_FILE, &data, &len) == 0) {
                ag_calib_archive_list (data, len);
                g_free (data);
            } else {
                fprintf (stderr, "warn: could not read file contents\n");
            }
        } else if (memcmp (hdr_data, AG_MULTISLOT_MAGIC,
                           AG_MULTISLOT_MAGIC_LEN) == 0) {
            /* AGMS multi-slot format. */
            ag_multislot_list_header (hdr_data, hdr_len);
            g_free (hdr_data);
        } else if (memcmp (hdr_data, AG_STASH_MAGIC,
                           AG_STASH_MAGIC_LEN) == 0) {
            /* Legacy single-slot AGST format. */
            printf ("  (legacy single-slot format)\n");
            ag_calib_archive_list_header (hdr_data, hdr_len);
            g_free (hdr_data);
        } else {
            /* Unknown format — fall back to full download. */
            g_free (hdr_data);
            uint8_t *data = NULL;
            size_t len = 0;
            if (ag_device_file_read (device, USER_FILE, &data, &len) == 0) {
                ag_calib_archive_list (data, len);
                g_free (data);
            } else {
                fprintf (stderr, "warn: could not read file contents\n");
            }
        }
    } else {
        printf ("\n  No calibration data stored on camera.\n");
    }

    g_object_unref (camera);
    arv_shutdown ();
    return exitcode;
}

/* ------------------------------------------------------------------ */
/*  upload                                                             */
/* ------------------------------------------------------------------ */

static int
stash_upload (const char *opt_serial, const char *opt_address,
              const char *opt_interface, int slot,
              const char *session_path)
{
    /* Pack the calibration session into an AGST archive. */
    uint8_t *archive = NULL;
    size_t   archive_len = 0;

    printf ("Packing calibration session: %s\n", session_path);
    if (ag_calib_archive_pack (session_path, &archive, &archive_len) != 0) {
        fprintf (stderr, "error: failed to pack calibration session\n");
        return EXIT_FAILURE;
    }

    printf ("Archive size: %zu bytes (%.1f MB)\n",
            archive_len, (double) archive_len / (1024.0 * 1024.0));

    ArvCamera *camera = connect_camera (opt_serial, opt_address, opt_interface);
    if (!camera) {
        g_free (archive);
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);
    int exitcode = EXIT_SUCCESS;

    /* Read existing file (for multi-slot merge).  OK if empty. */
    uint8_t *existing = NULL;
    size_t   existing_len = 0;

    int64_t file_size = 0;
    ag_device_file_info (device, USER_FILE, &file_size, NULL, NULL, NULL);

    if (file_size > 0) {
        printf ("Reading existing calibration data...\n");
        if (ag_device_file_read (device, USER_FILE,
                                   &existing, &existing_len) != 0) {
            fprintf (stderr, "error: failed to read existing file\n");
            exitcode = EXIT_FAILURE;
            goto upload_done;
        }
    }

    /* Build the new AGMS file with the updated slot. */
    uint8_t *new_file = NULL;
    size_t   new_len  = 0;

    if (ag_multislot_build (existing, existing_len, slot,
                              archive, archive_len,
                              &new_file, &new_len) != 0) {
        fprintf (stderr, "error: failed to build multi-slot archive\n");
        exitcode = EXIT_FAILURE;
        goto upload_done;
    }

    printf ("Writing to camera (slot %d, %.1f MB total)...\n",
            slot, (double) new_len / (1024.0 * 1024.0));
    if (ag_device_file_write (device, USER_FILE, new_file, new_len) != 0) {
        fprintf (stderr, "error: failed to write calibration to camera\n");
        exitcode = EXIT_FAILURE;
    } else {
        printf ("Done. Calibration data written to %s slot %d (%zu bytes).\n",
                USER_FILE, slot, archive_len);
    }

    g_free (new_file);

upload_done:
    g_free (existing);
    g_free (archive);
    g_object_unref (camera);
    arv_shutdown ();
    return exitcode;
}

/* ------------------------------------------------------------------ */
/*  delete                                                             */
/* ------------------------------------------------------------------ */

static int
stash_delete (const char *opt_serial, const char *opt_address,
              const char *opt_interface, int slot)
{
    ArvCamera *camera = connect_camera (opt_serial, opt_address, opt_interface);
    if (!camera) {
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);
    int exitcode = EXIT_SUCCESS;

    int64_t file_size = 0;
    ag_device_file_info (device, USER_FILE, &file_size, NULL, NULL, NULL);

    if (file_size <= 0) {
        printf ("No calibration data on camera — nothing to delete.\n");
        goto delete_done;
    }

    /*
     * Read just the header (4 KB) first to identify the format and
     * decide whether we actually need to download the entire file.
     * For the common case of deleting the last (or only) occupied
     * slot, we can skip straight to ag_device_file_delete().
     */
    uint8_t *hdr_data = NULL;
    size_t   hdr_len  = 0;
    int need_full_read = 1;

    if (ag_device_file_read_head (device, USER_FILE,
                                    AG_MULTISLOT_HEADER_SIZE,
                                    &hdr_data, &hdr_len) == 0 && hdr_len >= 4) {

        if (memcmp (hdr_data, AG_STASH_MAGIC, AG_STASH_MAGIC_LEN) == 0) {
            /* Legacy single-slot AGST — slot 0 is the only slot. */
            if (slot == 0) {
                need_full_read = 0;   /* just delete the whole file */
            } else {
                fprintf (stderr,
                    "error: legacy single-slot file — only slot 0 exists\n");
                g_free (hdr_data);
                exitcode = EXIT_FAILURE;
                goto delete_done;
            }
        } else if (memcmp (hdr_data, AG_MULTISLOT_MAGIC,
                           AG_MULTISLOT_MAGIC_LEN) == 0) {
            /* AGMS — check how many slots are occupied. */
            AgMultiSlotIndex idx;
            if (ag_multislot_parse_index (hdr_data, hdr_len, &idx) == 0) {
                if (!idx.slots[slot].occupied) {
                    printf ("Slot %d is already empty — nothing to delete.\n",
                            slot);
                    g_free (hdr_data);
                    goto delete_done;
                }
                /* Count how many OTHER slots are still occupied. */
                int others = 0;
                for (int i = 0; i < idx.num_slots; i++)
                    if (i != slot && idx.slots[i].occupied)
                        others++;
                if (others == 0)
                    need_full_read = 0;   /* last slot — just delete */
            }
        }
        /* Unknown format falls through to full read-modify-write. */
    }
    g_free (hdr_data);

    if (!need_full_read) {
        /* Deleting the only / last slot — remove the entire file. */
        printf ("Removing %s from camera (last slot)...\n", USER_FILE);
        if (ag_device_file_delete (device, USER_FILE) != 0) {
            fprintf (stderr, "error: failed to delete calibration file\n");
            exitcode = EXIT_FAILURE;
        } else {
            printf ("Done. Slot %d deleted. All calibration data removed.\n",
                    slot);
        }
        goto delete_done;
    }

    /* Multiple slots remain — full read-modify-write. */
    uint8_t *existing = NULL;
    size_t   existing_len = 0;

    printf ("Reading existing calibration data...\n");
    if (ag_device_file_read (device, USER_FILE,
                               &existing, &existing_len) != 0) {
        fprintf (stderr, "error: failed to read existing file\n");
        exitcode = EXIT_FAILURE;
        goto delete_done;
    }

    uint8_t *new_file = NULL;
    size_t   new_len  = 0;

    if (ag_multislot_build (existing, existing_len, slot,
                              NULL, 0,
                              &new_file, &new_len) != 0) {
        fprintf (stderr, "error: failed to rebuild multi-slot archive\n");
        g_free (existing);
        exitcode = EXIT_FAILURE;
        goto delete_done;
    }

    g_free (existing);

    printf ("Writing updated calibration data (slot %d removed)...\n", slot);
    if (ag_device_file_write (device, USER_FILE,
                                new_file, new_len) != 0) {
        fprintf (stderr, "error: failed to write updated file\n");
        exitcode = EXIT_FAILURE;
    } else {
        printf ("Done. Slot %d deleted.\n", slot);
    }
    g_free (new_file);

delete_done:
    g_object_unref (camera);
    arv_shutdown ();
    return exitcode;
}

/* ------------------------------------------------------------------ */
/*  download                                                           */
/* ------------------------------------------------------------------ */

static int
stash_download (const char *opt_serial, const char *opt_address,
                const char *opt_interface, int slot,
                const char *output_path)
{
    ArvCamera *camera = connect_camera (opt_serial, opt_address, opt_interface);
    if (!camera) {
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);
    int exitcode = EXIT_SUCCESS;

    /* Download the full file from camera. */
    uint8_t *data = NULL;
    size_t   data_len = 0;

    printf ("Reading calibration data from camera...\n");
    if (ag_device_file_read (device, USER_FILE, &data, &data_len) != 0) {
        fprintf (stderr, "error: failed to read calibration file\n");
        exitcode = EXIT_FAILURE;
        goto download_done;
    }

    /* Extract the target slot's AGST blob. */
    const uint8_t *slot_data = NULL;
    size_t         slot_len  = 0;

    if (ag_multislot_extract_slot (data, data_len, slot,
                                    &slot_data, &slot_len) != 0) {
        fprintf (stderr, "error: slot %d is empty or not present\n", slot);
        exitcode = EXIT_FAILURE;
        goto download_free;
    }

    /* Extract the archive entries to the output directory. */
    printf ("Extracting slot %d to %s:\n", slot, output_path);
    if (ag_calib_archive_extract_to_dir (slot_data, slot_len,
                                          output_path) != 0) {
        fprintf (stderr, "error: failed to extract calibration data\n");
        exitcode = EXIT_FAILURE;
    } else {
        printf ("Done. Calibration slot %d downloaded to %s/calib_result/\n",
                slot, output_path);
    }

download_free:
    g_free (data);

download_done:
    g_object_unref (camera);
    arv_shutdown ();
    return exitcode;
}

/* ------------------------------------------------------------------ */
/*  purge                                                              */
/* ------------------------------------------------------------------ */

static int
stash_purge (const char *opt_serial, const char *opt_address,
             const char *opt_interface)
{
    ArvCamera *camera = connect_camera (opt_serial, opt_address, opt_interface);
    if (!camera) {
        arv_shutdown ();
        return EXIT_FAILURE;
    }

    ArvDevice *device = arv_camera_get_device (camera);
    int exitcode = EXIT_SUCCESS;

    int64_t file_size = 0;
    ag_device_file_info (device, USER_FILE, &file_size, NULL, NULL, NULL);

    if (file_size <= 0) {
        printf ("No calibration data on camera — nothing to purge.\n");
        goto purge_done;
    }

    printf ("Purging %s (%" G_GINT64_FORMAT " bytes)...\n",
            USER_FILE, (gint64) file_size);

    if (ag_device_file_delete (device, USER_FILE) != 0) {
        fprintf (stderr, "error: failed to purge calibration file\n");
        exitcode = EXIT_FAILURE;
    } else {
        printf ("Done. All calibration data purged from %s.\n", USER_FILE);
    }

purge_done:
    g_object_unref (camera);
    arv_shutdown ();
    return exitcode;
}

/* ------------------------------------------------------------------ */
/*  Subcommand entry point                                             */
/* ------------------------------------------------------------------ */

int
cmd_calibration_stash (int argc, char *argv[], arg_dstr_t res, void *ctx)
{
    (void) ctx;

    /*
     * This command has sub-actions: list, upload, download, delete, purge.
     * argv[0] = program name
     * argv[1] = "calibration-stash"
     * argv[2] = action
     * argv[3..] = action-specific args
     *
     * We parse the device options and action with argtable, then
     * dispatch to the appropriate handler.
     */

    struct arg_str *cmd      = arg_str1 (NULL, NULL, "calibration-stash", NULL);
    struct arg_str *action   = arg_str1 (NULL, NULL, "<action>",
                                         "list|upload|download|delete|purge");
    struct arg_str *serial   = arg_str0 ("s", "serial",    "<serial>",
                                         "match by serial number");
    struct arg_str *address  = arg_str0 ("a", "address",   "<address>",
                                         "connect by camera IP");
    struct arg_str *iface    = arg_str0 ("i", "interface",  "<iface>",
                                         "force NIC selection");
    struct arg_int *slot_arg = arg_int0 (NULL, "slot", "<0|1|2>",
                                         "calibration slot (default: 0)");
    struct arg_str *output   = arg_str0 ("o", "output",  "<dir>",
                                         "output directory (for download)");
    struct arg_str *session  = arg_str0 (NULL, NULL, "<session>",
                                         "calibration session folder (for upload)");
    struct arg_lit *help     = arg_lit0 ("h", "help", "print this help");
    struct arg_end *end      = arg_end (10);

    void *argtable[] = { cmd, action, serial, address, iface, slot_arg,
                         output, session, help, end };

    int exitcode = EXIT_SUCCESS;
    if (arg_nullcheck (argtable) != 0) {
        arg_dstr_catf (res, "error: insufficient memory\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    int nerrors = arg_parse (argc, argv, argtable);

    /* Help. */
    if (help->count || (nerrors > 0 && argc <= 2)) {
        print_stash_usage ();
        goto done;
    }

    if (nerrors > 0 && action->count == 0) {
        arg_dstr_catf (res, "error: missing action "
                       "(list|upload|download|delete|purge)\n\n");
        print_stash_usage ();
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Validate mutual exclusion. */
    if (serial->count && address->count) {
        arg_dstr_catf (res, "error: --serial and --address are mutually exclusive\n");
        exitcode = EXIT_FAILURE;
        goto done;
    }

    /* Validate slot range. */
    int slot = 0;
    if (slot_arg->count) {
        slot = slot_arg->ival[0];
        if (slot < 0 || slot >= AG_MAX_SLOTS) {
            arg_dstr_catf (res, "error: --slot must be 0..%d\n",
                           AG_MAX_SLOTS - 1);
            exitcode = EXIT_FAILURE;
            goto done;
        }
    }

    const char *opt_serial    = serial->count  ? serial->sval[0]  : NULL;
    const char *opt_address   = address->count ? address->sval[0] : NULL;
    const char *opt_interface = iface->count   ? iface->sval[0]   : NULL;
    const char *act           = action->sval[0];

    if (strcmp (act, "list") == 0) {
        exitcode = stash_list (opt_serial, opt_address, opt_interface);
    } else if (strcmp (act, "upload") == 0) {
        if (session->count == 0) {
            arg_dstr_catf (res,
                "error: 'upload' requires a calibration session path\n"
                "  usage: ag-cam-tools calibration-stash upload "
                "[--slot N] <session>\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
        exitcode = stash_upload (opt_serial, opt_address, opt_interface,
                                  slot, session->sval[0]);
    } else if (strcmp (act, "download") == 0) {
        if (output->count == 0) {
            arg_dstr_catf (res,
                "error: 'download' requires -o <output-dir>\n"
                "  usage: ag-cam-tools calibration-stash download "
                "[--slot N] -o <dir>\n");
            exitcode = EXIT_FAILURE;
            goto done;
        }
        exitcode = stash_download (opt_serial, opt_address, opt_interface,
                                    slot, output->sval[0]);
    } else if (strcmp (act, "delete") == 0) {
        exitcode = stash_delete (opt_serial, opt_address, opt_interface,
                                  slot);
    } else if (strcmp (act, "purge") == 0) {
        exitcode = stash_purge (opt_serial, opt_address, opt_interface);
    } else {
        arg_dstr_catf (res, "error: unknown action '%s' "
                       "(expected list, upload, download, delete, or purge)\n",
                       act);
        exitcode = EXIT_FAILURE;
    }

done:
    arg_freetable (argtable, sizeof argtable / sizeof argtable[0]);
    return exitcode;
}
