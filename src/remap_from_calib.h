/*
 * remap_from_calib.h — compute NN remap table from calibration matrices
 *
 * Equivalent to OpenCV's initUndistortRectifyMap() with nearest-neighbour
 * output.  Takes the camera intrinsic matrix K, distortion coefficients,
 * rectification rotation R, and projection matrix P, and produces an
 * AgRemapTable suitable for ag_remap_rgb() / ag_remap_gray().
 *
 * Supports the full 14-parameter rational distortion model
 * (k1-k6, p1-p2, s1-s4, τx-τy).
 */

#ifndef AG_REMAP_FROM_CALIB_H
#define AG_REMAP_FROM_CALIB_H

#include "remap.h"

/*
 * Compute a nearest-neighbour remap table from raw calibration matrices.
 *
 * K      — 3×3 camera intrinsic matrix (row-major, 9 doubles)
 * dist   — distortion coefficients (up to 14 elements)
 * n_dist — number of distortion coefficients (5, 8, 12, or 14)
 * R      — 3×3 rectification rotation (row-major, 9 doubles)
 * P      — 3×4 projection matrix (row-major, 12 doubles)
 * width  — output image width  (pixels)
 * height — output image height (pixels)
 *
 * Returns a newly-allocated AgRemapTable (caller must ag_remap_table_free),
 * or NULL on error.
 */
AgRemapTable *ag_remap_from_calib (const double *K, const double *dist,
                                    int n_dist, const double *R,
                                    const double *P,
                                    int width, int height);

#endif /* AG_REMAP_FROM_CALIB_H */
