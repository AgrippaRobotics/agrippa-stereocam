/*
 * disparity_filter.c — spatial post-processing for Q4.4 disparity maps
 *
 * Pure C99, no OpenCV dependency.  All functions are hardware-independent
 * and suitable for unit testing.
 */

#include "disparity_filter.h"

#include <stdlib.h>
#include <string.h>

/* Q4.4 sentinel for "invalid disparity". */
#define INVALID_DISP  ((int16_t) -16)

static inline int
disp_valid (int16_t d)
{
    return d > 0;
}

/* ================================================================== */
/*  Specular highlight masking                                         */
/* ================================================================== */

void
ag_disparity_mask_specular (int16_t *disparity,
                             const uint8_t *rect_left,
                             const uint8_t *rect_right,
                             uint32_t width, uint32_t height,
                             uint8_t threshold, int radius)
{
    size_t n = (size_t) width * height;

    /* Build binary mask: 1 where either image is saturated. */
    uint8_t *mask = calloc (n, 1);
    if (!mask)
        return;

    for (size_t i = 0; i < n; i++) {
        if (rect_left[i] >= threshold || rect_right[i] >= threshold)
            mask[i] = 1;
    }

    /* Dilate mask by radius (simple box dilation). */
    if (radius > 0) {
        uint8_t *tmp = calloc (n, 1);
        if (!tmp) {
            free (mask);
            return;
        }

        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                if (mask[y * width + x]) {
                    int y0 = (int) y - radius;
                    int y1 = (int) y + radius;
                    int x0 = (int) x - radius;
                    int x1 = (int) x + radius;
                    if (y0 < 0) y0 = 0;
                    if (x0 < 0) x0 = 0;
                    if (y1 >= (int) height) y1 = (int) height - 1;
                    if (x1 >= (int) width)  x1 = (int) width  - 1;

                    for (int yy = y0; yy <= y1; yy++)
                        for (int xx = x0; xx <= x1; xx++)
                            tmp[yy * (int) width + xx] = 1;
                }
            }
        }

        memcpy (mask, tmp, n);
        free (tmp);
    }

    /* Apply mask: invalidate disparity. */
    for (size_t i = 0; i < n; i++) {
        if (mask[i])
            disparity[i] = INVALID_DISP;
    }

    free (mask);
}

/* ================================================================== */
/*  Median filter                                                      */
/* ================================================================== */

/* Simple insertion sort for small arrays. */
static void
sort_int16 (int16_t *arr, int n)
{
    for (int i = 1; i < n; i++) {
        int16_t key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

void
ag_disparity_median_filter (const int16_t *input, int16_t *output,
                              uint32_t width, uint32_t height,
                              int kernel_size)
{
    int half = kernel_size / 2;
    int max_neighbors = kernel_size * kernel_size;
    int16_t *buf = malloc ((size_t) max_neighbors * sizeof (int16_t));
    if (!buf)
        return;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            size_t idx = (size_t) y * width + x;
            int16_t center = input[idx];

            if (!disp_valid (center)) {
                output[idx] = center;   /* preserve invalid */
                continue;
            }

            /* Collect valid neighbors. */
            int count = 0;
            for (int dy = -half; dy <= half; dy++) {
                int yy = (int) y + dy;
                if (yy < 0 || yy >= (int) height)
                    continue;
                for (int dx = -half; dx <= half; dx++) {
                    int xx = (int) x + dx;
                    if (xx < 0 || xx >= (int) width)
                        continue;
                    int16_t v = input[(size_t) yy * width + (size_t) xx];
                    if (disp_valid (v))
                        buf[count++] = v;
                }
            }

            /* Need at least half the window to be valid. */
            if (count < max_neighbors / 2) {
                output[idx] = INVALID_DISP;
                continue;
            }

            sort_int16 (buf, count);
            output[idx] = buf[count / 2];
        }
    }

    free (buf);
}

/* ================================================================== */
/*  Morphological cleanup                                              */
/* ================================================================== */

/*
 * Binary dilation: for each pixel set in src, set all pixels within
 * radius in dst.
 */
static void
dilate_mask (const uint8_t *src, uint8_t *dst,
              uint32_t width, uint32_t height, int radius)
{
    memset (dst, 0, (size_t) width * height);

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            if (!src[y * width + x])
                continue;

            int y0 = (int) y - radius;
            int y1 = (int) y + radius;
            int x0 = (int) x - radius;
            int x1 = (int) x + radius;
            if (y0 < 0) y0 = 0;
            if (x0 < 0) x0 = 0;
            if (y1 >= (int) height) y1 = (int) height - 1;
            if (x1 >= (int) width)  x1 = (int) width  - 1;

            for (int yy = y0; yy <= y1; yy++)
                for (int xx = x0; xx <= x1; xx++)
                    dst[(size_t) yy * width + xx] = 1;
        }
    }
}

/*
 * Binary erosion: a pixel is set in dst only if all pixels within
 * radius in src are set.
 */
static void
erode_mask (const uint8_t *src, uint8_t *dst,
             uint32_t width, uint32_t height, int radius)
{
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            int y0 = (int) y - radius;
            int y1 = (int) y + radius;
            int x0 = (int) x - radius;
            int x1 = (int) x + radius;
            if (y0 < 0) y0 = 0;
            if (x0 < 0) x0 = 0;
            if (y1 >= (int) height) y1 = (int) height - 1;
            if (x1 >= (int) width)  x1 = (int) width  - 1;

            int all_set = 1;
            for (int yy = y0; yy <= y1 && all_set; yy++)
                for (int xx = x0; xx <= x1 && all_set; xx++)
                    if (!src[(size_t) yy * width + xx])
                        all_set = 0;

            dst[y * width + x] = (uint8_t) all_set;
        }
    }
}

void
ag_disparity_morph_cleanup (int16_t *disparity,
                              uint32_t width, uint32_t height,
                              int close_radius, int open_radius)
{
    size_t n = (size_t) width * height;
    uint8_t *valid     = malloc (n);
    uint8_t *valid_old = malloc (n);
    uint8_t *tmp       = malloc (n);

    if (!valid || !valid_old || !tmp)
        goto out;

    /* Build validity mask. */
    for (size_t i = 0; i < n; i++) {
        valid[i]     = disp_valid (disparity[i]) ? 1 : 0;
        valid_old[i] = valid[i];
    }

    /* Close (dilate then erode): fills small holes. */
    if (close_radius > 0) {
        dilate_mask (valid, tmp,   width, height, close_radius);
        erode_mask  (tmp,   valid, width, height, close_radius);
    }

    /* Open (erode then dilate): removes small bumps. */
    if (open_radius > 0) {
        erode_mask  (valid, tmp,   width, height, open_radius);
        dilate_mask (tmp,   valid, width, height, open_radius);
    }

    /* Apply changes. */
    for (size_t i = 0; i < n; i++) {
        if (valid[i] && !valid_old[i]) {
            /* Pixel was invalid, now should be valid: fill with
             * local mean of valid neighbours. */
            uint32_t x = (uint32_t) (i % width);
            uint32_t y = (uint32_t) (i / width);
            int32_t sum = 0;
            int count = 0;
            int r = close_radius > 0 ? close_radius : 1;
            int y0 = (int) y - r;
            int y1 = (int) y + r;
            int x0 = (int) x - r;
            int x1 = (int) x + r;
            if (y0 < 0) y0 = 0;
            if (x0 < 0) x0 = 0;
            if (y1 >= (int) height) y1 = (int) height - 1;
            if (x1 >= (int) width)  x1 = (int) width  - 1;

            for (int yy = y0; yy <= y1; yy++) {
                for (int xx = x0; xx <= x1; xx++) {
                    size_t ni = (size_t) yy * width + (size_t) xx;
                    if (valid_old[ni]) {
                        sum += disparity[ni];
                        count++;
                    }
                }
            }
            disparity[i] = count > 0 ? (int16_t) (sum / count) : INVALID_DISP;
        } else if (!valid[i] && valid_old[i]) {
            /* Pixel was valid, now should be invalid. */
            disparity[i] = INVALID_DISP;
        }
    }

out:
    free (valid);
    free (valid_old);
    free (tmp);
}
