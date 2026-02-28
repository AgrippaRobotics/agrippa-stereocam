/*
 * stereo.h — multi-backend stereo disparity computation
 *
 * Supports in-process OpenCV StereoSGBM (when HAVE_OPENCV is defined)
 * and in-process ONNX Runtime neural backends (when HAVE_ONNXRUNTIME is
 * defined).  Any ONNX stereo model works (IGEV++, FoundationStereo, etc.).
 */

#ifndef AG_STEREO_H
#define AG_STEREO_H

#include <glib.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Backend enum                                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    AG_STEREO_SGBM,   /* OpenCV StereoSGBM (requires HAVE_OPENCV) */
    AG_STEREO_ONNX,   /* ONNX Runtime in-process (requires HAVE_ONNXRUNTIME) */
} AgStereoBackend;

/*
 * Parse a backend name string into the enum value.
 * Accepts: "sgbm", "onnx", "igev", "rt-igev", "foundation".
 * "igev", "rt-igev", and "foundation" are aliases for AG_STEREO_ONNX.
 * Returns 0 on success, -1 on unrecognised name.
 */
int ag_stereo_parse_backend (const char *name, AgStereoBackend *out);

/*
 * Return the default model path for a named ONNX alias, or NULL.
 *   "igev"       → "models/igev_plusplus.onnx"
 *   "rt-igev"    → "models/rt_igev_plusplus.onnx"
 *   "foundation" → "models/foundation_stereo.onnx"
 */
const char *ag_stereo_default_model_path (const char *name);

/* Return human-readable name for the backend enum value. */
const char *ag_stereo_backend_name (AgStereoBackend backend);

/* ------------------------------------------------------------------ */
/*  StereoSGBM parameters                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int min_disparity;        /* minimum disparity value */
    int num_disparities;      /* must be a positive multiple of 16 */
    int block_size;           /* matched block size, must be odd >=1 */
    int p1;                   /* penalty on disparity change ±1 (0=auto) */
    int p2;                   /* penalty on disparity change >1 (0=auto) */
    int disp12_max_diff;      /* L-R consistency check (-1 to disable) */
    int pre_filter_cap;       /* truncation value for prefiltered pixels */
    int uniqueness_ratio;     /* percent margin for best match */
    int speckle_window_size;  /* max speckle area to filter */
    int speckle_range;        /* max disparity variation within speckle */
    int mode;                 /* 0=SGBM, 1=HH, 2=SGBM_3WAY, 3=HH4 */
} AgSgbmParams;

/* Fill an AgSgbmParams struct with sensible defaults. */
void ag_sgbm_params_defaults (AgSgbmParams *p);

/* ------------------------------------------------------------------ */
/*  ONNX Runtime parameters (neural stereo backends)                   */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *model_path;      /* path to .onnx model file (required) */
} AgOnnxParams;

/* ------------------------------------------------------------------ */
/*  Disparity context (opaque)                                         */
/* ------------------------------------------------------------------ */

typedef struct AgDisparityContext AgDisparityContext;

/*
 * Create a disparity context for the given backend and image dimensions.
 * width, height are the per-eye grayscale image dimensions.
 *
 * sgbm_params is used only when backend == AG_STEREO_SGBM.
 *   Pass NULL to use defaults.
 * onnx_params is used when backend == AG_STEREO_ONNX.
 *   Must not be NULL for that backend.
 *
 * Returns NULL on error (prints its own diagnostic).
 */
AgDisparityContext *ag_disparity_create (AgStereoBackend backend,
                                         uint32_t width, uint32_t height,
                                         const AgSgbmParams *sgbm_params,
                                         const AgOnnxParams *onnx_params);

/*
 * Compute disparity from a rectified grayscale stereo pair.
 *
 * left and right are width*height uint8 buffers (rectified grayscale).
 * disparity_out is a pre-allocated width*height int16_t buffer.
 * Values are in Q4.4 fixed point: divide by 16.0 for pixel disparity.
 *
 * Returns 0 on success, -1 on error.
 */
int ag_disparity_compute (AgDisparityContext *ctx,
                           const uint8_t *left, const uint8_t *right,
                           int16_t *disparity_out);

/*
 * Update SGBM parameters on an existing context.
 * Applies only when ctx backend is AG_STEREO_SGBM.
 * Returns 0 on success, -1 on unsupported backend or error.
 */
int ag_disparity_update_sgbm_params (AgDisparityContext *ctx,
                                     const AgSgbmParams *params);

/* Destroy context and free all backend resources. */
void ag_disparity_destroy (AgDisparityContext *ctx);

/* ------------------------------------------------------------------ */
/*  Disparity visualization                                            */
/* ------------------------------------------------------------------ */

/*
 * Apply a JET colormap to Q4.4 disparity, producing RGB24 output.
 * Invalid disparity (< min_disparity * 16) is rendered as black.
 * rgb_out must be width*height*3 bytes.
 */
void ag_disparity_colorize (const int16_t *disparity, uint32_t width,
                             uint32_t height, int min_disparity,
                             int num_disparities, uint8_t *rgb_out);

/*
 * Convert a single Q4.4 disparity value to depth.
 * Returns depth in the same units as baseline (e.g. cm if baseline is in cm).
 * Returns 0.0 for invalid (non-positive) disparity.
 */
static inline double
ag_disparity_to_depth (int16_t disp_q4, double focal_length_px,
                        double baseline)
{
    double d = (double) disp_q4 / 16.0;
    if (d <= 0.0) return 0.0;
    return (focal_length_px * baseline) / d;
}

/* ------------------------------------------------------------------ */
/*  Disparity range from depth bounds                                  */
/* ------------------------------------------------------------------ */

/*
 * Compute SGBM min_disparity and num_disparities from depth limits.
 *
 * z_near_cm / z_far_cm:  working distance range (same units as baseline).
 * focal_length_px:       rectified focal length in pixels.
 * baseline_cm:           stereo baseline in cm.
 * out_min_disparity:     receives computed minimum disparity.
 * out_num_disparities:   receives computed num_disparities (multiple of 16).
 *
 * Returns 0 on success, -1 on invalid input (e.g. z_near <= 0).
 */
int ag_disparity_range_from_depth (double z_near_cm, double z_far_cm,
                                    double focal_length_px,
                                    double baseline_cm,
                                    int *out_min_disparity,
                                    int *out_num_disparities);

/* ------------------------------------------------------------------ */
/*  Internal: SGBM backend (stereo_sgbm.cpp, extern "C")              */
/* ------------------------------------------------------------------ */

#ifdef HAVE_OPENCV
void *ag_sgbm_create  (uint32_t width, uint32_t height,
                        const AgSgbmParams *params);
int   ag_sgbm_compute (void *sgbm_ptr, uint32_t width, uint32_t height,
                        const uint8_t *left, const uint8_t *right,
                        int16_t *disparity_out);
int   ag_sgbm_update_params (void *sgbm_ptr, const AgSgbmParams *params);
void  ag_sgbm_destroy (void *sgbm_ptr);

/*
 * Apply CLAHE (Contrast Limited Adaptive Histogram Equalization) to
 * a grayscale image.  Enhances local contrast for better stereo
 * matching on low-texture industrial surfaces.
 *
 * clip_limit: contrast limit (2.0 is conservative, 3-4 for low texture).
 * tile_size:  grid size for adaptive equalization (8 = ~180x135 tiles
 *             at 1440x1080 resolution).
 *
 * input and output may alias (in-place operation is safe).
 */
void ag_clahe_apply (const uint8_t *input, uint8_t *output,
                      uint32_t width, uint32_t height,
                      double clip_limit, int tile_size);
#endif

/* ------------------------------------------------------------------ */
/*  Internal: ONNX Runtime backend (stereo_onnx.c)                     */
/* ------------------------------------------------------------------ */

#ifdef HAVE_ONNXRUNTIME
void *ag_onnx_create  (uint32_t width, uint32_t height,
                        const AgOnnxParams *params);
int   ag_onnx_compute (void *onnx_ptr, uint32_t width, uint32_t height,
                        const uint8_t *left, const uint8_t *right,
                        int16_t *disparity_out);
void  ag_onnx_destroy (void *onnx_ptr);
#endif

#endif /* AG_STEREO_H */
