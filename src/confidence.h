/*
 * confidence.h — per-pixel disparity confidence / quality map
 *
 * Computes a 0–255 quality score for each pixel based on:
 *   1. Texture strength (Sobel gradient magnitude of the left image)
 *   2. Disparity validity (invalid pixels get confidence 0)
 *   3. Local disparity variance (noisy regions get lower confidence)
 *
 * The confidence map can be used to:
 *   - Mask unreliable depth regions before downstream processing
 *   - Weight depth values in point cloud fusion or 3D reconstruction
 *   - Visualize which parts of the scene have reliable depth
 */

#ifndef AG_CONFIDENCE_H
#define AG_CONFIDENCE_H

#include <stdint.h>

/*
 * Compute a per-pixel confidence map from a disparity map and its
 * corresponding rectified left grayscale image.
 *
 * disparity:      Q4.4 fixed-point disparity map (width*height int16_t).
 * left_gray:      rectified left grayscale image (width*height uint8_t).
 * width, height:  image dimensions.
 * confidence_out: pre-allocated width*height uint8_t buffer.
 *                 Output values: 0 = no confidence, 255 = high confidence.
 */
void ag_confidence_compute (const int16_t *disparity,
                             const uint8_t *left_gray,
                             uint32_t width, uint32_t height,
                             uint8_t *confidence_out);

/*
 * Apply a JET colormap to the confidence map for visualization.
 * Low confidence is blue/black, high confidence is red/yellow.
 *
 * confidence: width*height uint8_t values (0-255).
 * rgb_out:    pre-allocated width*height*3 uint8_t buffer.
 */
void ag_confidence_colorize (const uint8_t *confidence,
                              uint32_t width, uint32_t height,
                              uint8_t *rgb_out);

#endif /* AG_CONFIDENCE_H */
