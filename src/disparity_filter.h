/*
 * disparity_filter.h — spatial post-processing filters for Q4.4 disparity maps
 *
 * All functions operate on int16_t Q4.4 fixed-point disparity.
 * Invalid pixels are represented as values <= 0.
 */

#ifndef AG_DISPARITY_FILTER_H
#define AG_DISPARITY_FILTER_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Specular highlight masking                                         */
/* ------------------------------------------------------------------ */

/*
 * Invalidate disparity at pixels where either the left or right
 * rectified image is near saturation (specular highlights).
 *
 * Specular reflections are viewpoint-dependent — they appear at
 * different positions in left/right images, causing systematic
 * false matches.
 *
 * threshold: pixel value at or above which a pixel is considered
 *            specular (recommended: 250 for 8-bit images).
 * radius:    dilate the mask by this many pixels to also catch the
 *            gradient edges of highlights (0 to disable dilation).
 *
 * Disparity is modified in-place: affected pixels set to -16
 * (invalid sentinel in Q4.4).
 */
void ag_disparity_mask_specular (int16_t *disparity,
                                 const uint8_t *rect_left,
                                 const uint8_t *rect_right,
                                 uint32_t width, uint32_t height,
                                 uint8_t threshold, int radius);

/* ------------------------------------------------------------------ */
/*  Median filter                                                      */
/* ------------------------------------------------------------------ */

/*
 * Apply a spatial median filter to the disparity map.
 *
 * For each valid pixel, collects valid neighbors within a
 * kernel_size × kernel_size window, computes the median, and
 * writes it to output.  Invalid pixels remain invalid.
 * If fewer than half the neighbors are valid, output is invalid.
 *
 * kernel_size must be odd (3 or 5 recommended).
 * input and output may NOT alias.
 */
void ag_disparity_median_filter (const int16_t *input, int16_t *output,
                                  uint32_t width, uint32_t height,
                                  int kernel_size);

/* ------------------------------------------------------------------ */
/*  Morphological cleanup                                              */
/* ------------------------------------------------------------------ */

/*
 * Clean up disparity by applying morphological close then open on
 * the valid-pixel mask.
 *
 * Close (dilate then erode) fills small holes — isolated invalid
 * pixels surrounded by valid values get filled with the local mean.
 * Open (erode then dilate) removes small bumps — isolated valid
 * pixels surrounded by invalid are cleared.
 *
 * close_radius / open_radius: structuring element radius in pixels.
 *   Use 0 to skip that operation.  Recommended: 1–2 each.
 *
 * Disparity is modified in-place.
 */
void ag_disparity_morph_cleanup (int16_t *disparity,
                                  uint32_t width, uint32_t height,
                                  int close_radius, int open_radius);

#endif /* AG_DISPARITY_FILTER_H */
