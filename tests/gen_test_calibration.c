/*
 * gen_test_calibration.c — generate a minimal 128×128 calibration session
 *
 * Creates a tiny but valid calibration session directory suitable for
 * hardware integration tests.  The remap files are only ~64 KB each
 * (vs ~6 MB for real 1440×1080 data), so upload/download cycles are
 * fast even over a slow GenICam file channel.
 *
 * Usage:
 *   gen_test_calibration <output-dir>
 *
 * Creates:
 *   <output-dir>/calib_result/remap_left.bin
 *   <output-dir>/calib_result/remap_right.bin
 *   <output-dir>/calib_result/calibration_meta.json
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#define WIDTH   128
#define HEIGHT  128

static int
write_remap (const char *path, uint32_t width, uint32_t height)
{
    FILE *f = fopen (path, "wb");
    if (!f) {
        fprintf (stderr, "error: cannot create %s\n", path);
        return -1;
    }

    /* Header: magic(4) + width(4) + height(4) + flags(4). */
    const char magic[4] = "RMAP";
    uint32_t hdr[3] = { width, height, 0 };   /* flags = 0 */

    fwrite (magic, 1, 4, f);
    fwrite (hdr, sizeof (uint32_t), 3, f);

    /* Identity mapping: pixel i maps to offset i. */
    size_t n = (size_t) width * height;
    for (uint32_t i = 0; i < n; i++)
        fwrite (&i, sizeof (uint32_t), 1, f);

    fclose (f);
    return 0;
}

static int
write_meta (const char *path, uint32_t width, uint32_t height)
{
    FILE *f = fopen (path, "w");
    if (!f) {
        fprintf (stderr, "error: cannot create %s\n", path);
        return -1;
    }

    fprintf (f,
        "{\n"
        "  \"image_size\": [%u, %u],\n"
        "  \"num_pairs_used\": 5,\n"
        "  \"rms_stereo_px\": 0.25,\n"
        "  \"mean_epipolar_error_px\": 0.30,\n"
        "  \"baseline_cm\": 4.0,\n"
        "  \"focal_length_px\": 100.0,\n"
        "  \"disparity_range\": {\n"
        "    \"min_disparity\": 4,\n"
        "    \"num_disparities\": 32\n"
        "  }\n"
        "}\n",
        width, height);

    fclose (f);
    return 0;
}

static int
mkdirs (const char *path)
{
    char *tmp = strdup (path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir (tmp, 0755);
            *p = '/';
        }
    }
    mkdir (tmp, 0755);
    free (tmp);
    return 0;
}

int
main (int argc, char *argv[])
{
    if (argc != 2) {
        fprintf (stderr, "usage: gen_test_calibration <output-dir>\n");
        return 1;
    }

    const char *out_dir = argv[1];

    /* Create <out_dir>/calib_result/ */
    char calib_dir[4096];
    snprintf (calib_dir, sizeof calib_dir, "%s/calib_result", out_dir);
    mkdirs (calib_dir);

    char path[4096];

    snprintf (path, sizeof path, "%s/remap_left.bin", calib_dir);
    if (write_remap (path, WIDTH, HEIGHT) != 0) return 1;

    snprintf (path, sizeof path, "%s/remap_right.bin", calib_dir);
    if (write_remap (path, WIDTH, HEIGHT) != 0) return 1;

    snprintf (path, sizeof path, "%s/calibration_meta.json", calib_dir);
    if (write_meta (path, WIDTH, HEIGHT) != 0) return 1;

    printf ("Generated 128x128 test calibration in %s\n", out_dir);
    return 0;
}
