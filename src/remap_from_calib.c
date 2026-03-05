/*
 * remap_from_calib.c — compute NN remap table from calibration matrices
 *
 * Implements the same mapping as OpenCV's initUndistortRectifyMap() for
 * nearest-neighbour output.  The computation is closed-form (no iteration):
 *
 *   For each destination pixel (u, v):
 *     1. Map through P_3x3^(-1) to normalized rectified coordinates
 *     2. Rotate by R^T to normalized camera coordinates
 *     3. Apply the forward distortion model (rational 14-param)
 *     4. Map through K to source pixel coordinates
 *     5. Round to nearest integer → linear offset, or sentinel if OOB
 */

#include "remap_from_calib.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 3×3 matrix inverse via Cramer's rule (row-major storage). */
static int
invert_3x3 (const double *m, double *inv)
{
    double a = m[0], b = m[1], c = m[2];
    double d = m[3], e = m[4], f = m[5];
    double g = m[6], h = m[7], k = m[8];

    double det = a * (e * k - f * h)
               - b * (d * k - f * g)
               + c * (d * h - e * g);

    if (fabs (det) < 1e-12)
        return -1;

    double inv_det = 1.0 / det;

    inv[0] = (e * k - f * h) * inv_det;
    inv[1] = (c * h - b * k) * inv_det;
    inv[2] = (b * f - c * e) * inv_det;
    inv[3] = (f * g - d * k) * inv_det;
    inv[4] = (a * k - c * g) * inv_det;
    inv[5] = (c * d - a * f) * inv_det;
    inv[6] = (d * h - e * g) * inv_det;
    inv[7] = (b * g - a * h) * inv_det;
    inv[8] = (a * e - b * d) * inv_det;

    return 0;
}

/* Safe coefficient access: returns 0 for out-of-range indices. */
static inline double
coeff (const double *dist, int n_dist, int idx)
{
    return (idx < n_dist) ? dist[idx] : 0.0;
}

AgRemapTable *
ag_remap_from_calib (const double *K, const double *dist, int n_dist,
                     const double *R, const double *P,
                     int width, int height)
{
    if (!K || !dist || !R || !P || width <= 0 || height <= 0) {
        fprintf (stderr, "remap_from_calib: invalid parameters\n");
        return NULL;
    }

    /* Extract the left 3×3 submatrix of P (projection → new camera matrix). */
    double P33[9] = {
        P[0], P[1], P[2],
        P[4], P[5], P[6],
        P[8], P[9], P[10],
    };

    /* P_3x3^(-1): maps destination pixel to normalized rectified coords. */
    double P33_inv[9];
    if (invert_3x3 (P33, P33_inv) != 0) {
        fprintf (stderr, "remap_from_calib: P 3×3 submatrix is singular\n");
        return NULL;
    }

    /* R^T: inverse rectification rotation (row-major). */
    double Rt[9] = {
        R[0], R[3], R[6],
        R[1], R[4], R[7],
        R[2], R[5], R[8],
    };

    /* Original camera intrinsics. */
    double fx = K[0], cx = K[2];
    double fy = K[4], cy = K[5];

    /* Distortion coefficients (14-parameter rational model).
     * Layout: k1 k2 p1 p2 k3 k4 k5 k6 s1 s2 s3 s4 τx τy */
    double k1 = coeff (dist, n_dist, 0);
    double k2 = coeff (dist, n_dist, 1);
    double p1 = coeff (dist, n_dist, 2);
    double p2 = coeff (dist, n_dist, 3);
    double k3 = coeff (dist, n_dist, 4);
    double k4 = coeff (dist, n_dist, 5);
    double k5 = coeff (dist, n_dist, 6);
    double k6 = coeff (dist, n_dist, 7);
    double s1 = coeff (dist, n_dist, 8);
    double s2 = coeff (dist, n_dist, 9);
    double s3 = coeff (dist, n_dist, 10);
    double s4 = coeff (dist, n_dist, 11);
    /* τx, τy (indices 12-13) are for tilted sensor model; ignored. */

    size_t n_pixels = (size_t) width * (size_t) height;
    uint32_t *offsets = g_malloc (n_pixels * sizeof (uint32_t));

    for (int v = 0; v < height; v++) {
        for (int u = 0; u < width; u++) {
            double du = (double) u;
            double dv = (double) v;

            /* Step 1: P_3x3^(-1) * [u, v, 1]^T → normalized rectified. */
            double X = P33_inv[0] * du + P33_inv[1] * dv + P33_inv[2];
            double Y = P33_inv[3] * du + P33_inv[4] * dv + P33_inv[5];
            double W = P33_inv[6] * du + P33_inv[7] * dv + P33_inv[8];
            double xr = X / W;
            double yr = Y / W;

            /* Step 2: R^T * [xr, yr, 1]^T → normalized camera coords. */
            double Xc = Rt[0] * xr + Rt[1] * yr + Rt[2];
            double Yc = Rt[3] * xr + Rt[4] * yr + Rt[5];
            double Zc = Rt[6] * xr + Rt[7] * yr + Rt[8];
            double x = Xc / Zc;
            double y = Yc / Zc;

            /* Step 3: Apply forward distortion model. */
            double r2 = x * x + y * y;
            double r4 = r2 * r2;
            double r6 = r4 * r2;

            double num = 1.0 + k1 * r2 + k2 * r4 + k3 * r6;
            double den = 1.0 + k4 * r2 + k5 * r4 + k6 * r6;
            double radial = num / den;

            double xd = x * radial
                       + 2.0 * p1 * x * y + p2 * (r2 + 2.0 * x * x)
                       + s1 * r2 + s2 * r4;
            double yd = y * radial
                       + p1 * (r2 + 2.0 * y * y) + 2.0 * p2 * x * y
                       + s3 * r2 + s4 * r4;

            /* Step 4: Map to source pixel via original K. */
            double map_x = fx * xd + cx;
            double map_y = fy * yd + cy;

            /* Step 5: NN rounding → linear offset. */
            int sx = (int) round (map_x);
            int sy = (int) round (map_y);

            size_t idx = (size_t) v * (size_t) width + (size_t) u;
            if (sx >= 0 && sx < width && sy >= 0 && sy < height)
                offsets[idx] = (uint32_t) ((size_t) sy * (size_t) width
                                         + (size_t) sx);
            else
                offsets[idx] = AG_REMAP_SENTINEL;
        }
    }

    AgRemapTable *table = g_malloc (sizeof (AgRemapTable));
    table->width   = (uint32_t) width;
    table->height  = (uint32_t) height;
    table->offsets = offsets;
    return table;
}
