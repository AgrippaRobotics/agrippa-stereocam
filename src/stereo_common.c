/*
 * stereo_common.c — disparity backend lifecycle dispatch and utilities
 *
 * Dispatches ag_disparity_create / compute / destroy to the selected
 * backend.  Also provides the JET colormap for disparity visualisation.
 */

#include "stereo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  Backend name parsing                                               */
/* ================================================================== */

int
ag_stereo_parse_backend (const char *name, AgStereoBackend *out)
{
    if (strcmp (name, "sgbm") == 0) {
        *out = AG_STEREO_SGBM;
        return 0;
    }
    /* "onnx" is the canonical name; model-specific names are aliases. */
    if (strcmp (name, "onnx") == 0 ||
        strcmp (name, "igev") == 0 ||
        strcmp (name, "rt-igev") == 0 ||
        strcmp (name, "foundation") == 0) {
        *out = AG_STEREO_ONNX;
        return 0;
    }
    return -1;
}

const char *
ag_stereo_default_model_path (const char *name)
{
    if (strcmp (name, "igev") == 0)
        return "models/igev_plusplus.onnx";
    if (strcmp (name, "rt-igev") == 0)
        return "models/rt_igev_plusplus.onnx";
    if (strcmp (name, "foundation") == 0)
        return "models/foundation_stereo.onnx";
    return NULL;
}

const char *
ag_stereo_backend_name (AgStereoBackend backend)
{
    switch (backend) {
    case AG_STEREO_SGBM: return "sgbm";
    case AG_STEREO_ONNX: return "onnx";
    }
    return "unknown";
}

/* ================================================================== */
/*  SGBM parameter defaults                                            */
/* ================================================================== */

void
ag_sgbm_params_defaults (AgSgbmParams *p)
{
    memset (p, 0, sizeof (*p));
    p->min_disparity       = 0;
    p->num_disparities     = 128;
    p->block_size          = 5;
    p->p1                  = 0;     /* auto-derived from block_size */
    p->p2                  = 0;
    p->disp12_max_diff     = 1;
    p->pre_filter_cap      = 63;
    p->uniqueness_ratio    = 10;
    p->speckle_window_size = 100;
    p->speckle_range       = 32;
    p->mode                = 2;     /* SGBM_3WAY */
}

/* ================================================================== */
/*  Disparity context                                                  */
/* ================================================================== */

struct AgDisparityContext {
    AgStereoBackend backend;
    uint32_t width;
    uint32_t height;

    union {
#ifdef HAVE_OPENCV
        struct {
            void *sgbm_ptr;
        } sgbm;
#endif
#ifdef HAVE_ONNXRUNTIME
        struct {
            void *onnx_ptr;
        } onnx;
#endif
    } u;
};

AgDisparityContext *
ag_disparity_create (AgStereoBackend backend,
                     uint32_t width, uint32_t height,
                     const AgSgbmParams *sgbm_params,
                     const AgOnnxParams *onnx_params)
{
    AgDisparityContext *ctx = g_malloc0 (sizeof (AgDisparityContext));
    ctx->backend = backend;
    ctx->width   = width;
    ctx->height  = height;

    switch (backend) {
    case AG_STEREO_SGBM:
#ifdef HAVE_OPENCV
        {
            AgSgbmParams defaults;
            if (!sgbm_params) {
                ag_sgbm_params_defaults (&defaults);
                sgbm_params = &defaults;
            }
            ctx->u.sgbm.sgbm_ptr = ag_sgbm_create (width, height, sgbm_params);
            if (!ctx->u.sgbm.sgbm_ptr) {
                g_free (ctx);
                return NULL;
            }
        }
#else
        fprintf (stderr, "error: StereoSGBM backend requires OpenCV "
                 "(build with HAVE_OPENCV=1)\n");
        g_free (ctx);
        return NULL;
#endif
        break;

    case AG_STEREO_ONNX:
#ifdef HAVE_ONNXRUNTIME
        if (!onnx_params || !onnx_params->model_path) {
            fprintf (stderr, "error: ONNX backend requires --model-path\n");
            g_free (ctx);
            return NULL;
        }
        ctx->u.onnx.onnx_ptr = ag_onnx_create (width, height, onnx_params);
        if (!ctx->u.onnx.onnx_ptr) {
            g_free (ctx);
            return NULL;
        }
#else
        fprintf (stderr, "error: ONNX backend requires ONNX Runtime "
                 "(build with HAVE_ONNXRUNTIME=1)\n");
        g_free (ctx);
        return NULL;
#endif
        break;
    }

    return ctx;
}

int
ag_disparity_compute (AgDisparityContext *ctx,
                      const uint8_t *left, const uint8_t *right,
                      int16_t *disparity_out)
{
    switch (ctx->backend) {
#ifdef HAVE_OPENCV
    case AG_STEREO_SGBM:
        return ag_sgbm_compute (ctx->u.sgbm.sgbm_ptr,
                                ctx->width, ctx->height,
                                left, right, disparity_out);
#else
    case AG_STEREO_SGBM:
        return -1;
#endif

#ifdef HAVE_ONNXRUNTIME
    case AG_STEREO_ONNX:
        return ag_onnx_compute (ctx->u.onnx.onnx_ptr,
                                ctx->width, ctx->height,
                                left, right, disparity_out);
#else
    case AG_STEREO_ONNX:
        return -1;
#endif
    }

    return -1;
}

int
ag_disparity_update_sgbm_params (AgDisparityContext *ctx,
                                 const AgSgbmParams *params)
{
    if (!ctx || !params)
        return -1;

    switch (ctx->backend) {
#ifdef HAVE_OPENCV
    case AG_STEREO_SGBM:
        return ag_sgbm_update_params (ctx->u.sgbm.sgbm_ptr, params);
#else
    case AG_STEREO_SGBM:
        return -1;
#endif
    case AG_STEREO_ONNX:
        return -1;
    }

    return -1;
}

void *
ag_disparity_get_sgbm_handle (AgDisparityContext *ctx)
{
    if (!ctx)
        return NULL;
#ifdef HAVE_OPENCV
    if (ctx->backend == AG_STEREO_SGBM)
        return ctx->u.sgbm.sgbm_ptr;
#endif
    return NULL;
}

void
ag_disparity_destroy (AgDisparityContext *ctx)
{
    if (!ctx)
        return;

    switch (ctx->backend) {
#ifdef HAVE_OPENCV
    case AG_STEREO_SGBM:
        ag_sgbm_destroy (ctx->u.sgbm.sgbm_ptr);
        break;
#else
    case AG_STEREO_SGBM:
        break;
#endif

#ifdef HAVE_ONNXRUNTIME
    case AG_STEREO_ONNX:
        ag_onnx_destroy (ctx->u.onnx.onnx_ptr);
        break;
#else
    case AG_STEREO_ONNX:
        break;
#endif
    }

    g_free (ctx);
}

/* ================================================================== */
/*  Disparity range from depth bounds                                  */
/* ================================================================== */

int
ag_disparity_range_from_depth (double z_near_cm, double z_far_cm,
                                double focal_length_px,
                                double baseline_cm,
                                int *out_min_disparity,
                                int *out_num_disparities)
{
    if (z_near_cm <= 0.0 || z_far_cm <= 0.0 ||
        z_near_cm >= z_far_cm ||
        focal_length_px <= 0.0 || baseline_cm <= 0.0)
        return -1;

    /* disparity = f * B / z.  Close objects → high disparity. */
    double d_max = focal_length_px * baseline_cm / z_near_cm;
    double d_min = focal_length_px * baseline_cm / z_far_cm;

    int min_disp  = (int) floor (d_min);
    int range     = (int) ceil (d_max) - min_disp;

    /* Round up to next multiple of 16 (SGBM requirement). */
    if (range < 16)
        range = 16;
    range = ((range + 15) / 16) * 16;

    *out_min_disparity   = min_disp;
    *out_num_disparities = range;
    return 0;
}

/* ================================================================== */
/*  JET colourmap for disparity visualisation                          */
/* ================================================================== */

/*
 * Pre-computed 256-entry JET colourmap (RGB).  Index 0 is deep blue,
 * index 255 is deep red.  Generated from the standard Matplotlib jet.
 */
static const uint8_t jet_lut[256][3] = {
    {  0,   0, 131}, {  0,   0, 135}, {  0,   0, 139}, {  0,   0, 143},
    {  0,   0, 147}, {  0,   0, 151}, {  0,   0, 155}, {  0,   0, 159},
    {  0,   0, 163}, {  0,   0, 167}, {  0,   0, 171}, {  0,   0, 175},
    {  0,   0, 179}, {  0,   0, 183}, {  0,   0, 187}, {  0,   0, 191},
    {  0,   0, 195}, {  0,   0, 199}, {  0,   0, 203}, {  0,   0, 207},
    {  0,   0, 211}, {  0,   0, 215}, {  0,   0, 219}, {  0,   0, 223},
    {  0,   0, 227}, {  0,   0, 231}, {  0,   0, 235}, {  0,   0, 239},
    {  0,   0, 243}, {  0,   0, 247}, {  0,   0, 251}, {  0,   0, 255},
    {  0,   4, 255}, {  0,   8, 255}, {  0,  12, 255}, {  0,  16, 255},
    {  0,  20, 255}, {  0,  24, 255}, {  0,  28, 255}, {  0,  32, 255},
    {  0,  36, 255}, {  0,  40, 255}, {  0,  44, 255}, {  0,  48, 255},
    {  0,  52, 255}, {  0,  56, 255}, {  0,  60, 255}, {  0,  64, 255},
    {  0,  68, 255}, {  0,  72, 255}, {  0,  76, 255}, {  0,  80, 255},
    {  0,  84, 255}, {  0,  88, 255}, {  0,  92, 255}, {  0,  96, 255},
    {  0, 100, 255}, {  0, 104, 255}, {  0, 108, 255}, {  0, 112, 255},
    {  0, 116, 255}, {  0, 120, 255}, {  0, 124, 255}, {  0, 128, 255},
    {  0, 131, 255}, {  0, 135, 255}, {  0, 139, 255}, {  0, 143, 255},
    {  0, 147, 255}, {  0, 151, 255}, {  0, 155, 255}, {  0, 159, 255},
    {  0, 163, 255}, {  0, 167, 255}, {  0, 171, 255}, {  0, 175, 255},
    {  0, 179, 255}, {  0, 183, 255}, {  0, 187, 255}, {  0, 191, 255},
    {  0, 195, 255}, {  0, 199, 255}, {  0, 203, 255}, {  0, 207, 255},
    {  0, 211, 255}, {  0, 215, 255}, {  0, 219, 255}, {  0, 223, 255},
    {  0, 227, 255}, {  0, 231, 255}, {  0, 235, 255}, {  0, 239, 255},
    {  0, 243, 255}, {  0, 247, 255}, {  0, 251, 255}, {  0, 255, 255},
    {  4, 255, 251}, {  8, 255, 247}, { 12, 255, 243}, { 16, 255, 239},
    { 20, 255, 235}, { 24, 255, 231}, { 28, 255, 227}, { 32, 255, 223},
    { 36, 255, 219}, { 40, 255, 215}, { 44, 255, 211}, { 48, 255, 207},
    { 52, 255, 203}, { 56, 255, 199}, { 60, 255, 195}, { 64, 255, 191},
    { 68, 255, 187}, { 72, 255, 183}, { 76, 255, 179}, { 80, 255, 175},
    { 84, 255, 171}, { 88, 255, 167}, { 92, 255, 163}, { 96, 255, 159},
    {100, 255, 155}, {104, 255, 151}, {108, 255, 147}, {112, 255, 143},
    {116, 255, 139}, {120, 255, 135}, {124, 255, 131}, {128, 255, 128},
    {131, 255, 124}, {135, 255, 120}, {139, 255, 116}, {143, 255, 112},
    {147, 255, 108}, {151, 255, 104}, {155, 255, 100}, {159, 255,  96},
    {163, 255,  92}, {167, 255,  88}, {171, 255,  84}, {175, 255,  80},
    {179, 255,  76}, {183, 255,  72}, {187, 255,  68}, {191, 255,  64},
    {195, 255,  60}, {199, 255,  56}, {203, 255,  52}, {207, 255,  48},
    {211, 255,  44}, {215, 255,  40}, {219, 255,  36}, {223, 255,  32},
    {227, 255,  28}, {231, 255,  24}, {235, 255,  20}, {239, 255,  16},
    {243, 255,  12}, {247, 255,   8}, {251, 255,   4}, {255, 255,   0},
    {255, 251,   0}, {255, 247,   0}, {255, 243,   0}, {255, 239,   0},
    {255, 235,   0}, {255, 231,   0}, {255, 227,   0}, {255, 223,   0},
    {255, 219,   0}, {255, 215,   0}, {255, 211,   0}, {255, 207,   0},
    {255, 203,   0}, {255, 199,   0}, {255, 195,   0}, {255, 191,   0},
    {255, 187,   0}, {255, 183,   0}, {255, 179,   0}, {255, 175,   0},
    {255, 171,   0}, {255, 167,   0}, {255, 163,   0}, {255, 159,   0},
    {255, 155,   0}, {255, 151,   0}, {255, 147,   0}, {255, 143,   0},
    {255, 139,   0}, {255, 135,   0}, {255, 131,   0}, {255, 128,   0},
    {255, 124,   0}, {255, 120,   0}, {255, 116,   0}, {255, 112,   0},
    {255, 108,   0}, {255, 104,   0}, {255, 100,   0}, {255,  96,   0},
    {255,  92,   0}, {255,  88,   0}, {255,  84,   0}, {255,  80,   0},
    {255,  76,   0}, {255,  72,   0}, {255,  68,   0}, {255,  64,   0},
    {255,  60,   0}, {255,  56,   0}, {255,  52,   0}, {255,  48,   0},
    {255,  44,   0}, {255,  40,   0}, {255,  36,   0}, {255,  32,   0},
    {255,  28,   0}, {255,  24,   0}, {255,  20,   0}, {255,  16,   0},
    {255,  12,   0}, {255,   8,   0}, {255,   4,   0}, {255,   0,   0},
    {251,   0,   0}, {247,   0,   0}, {243,   0,   0}, {239,   0,   0},
    {235,   0,   0}, {231,   0,   0}, {227,   0,   0}, {223,   0,   0},
    {219,   0,   0}, {215,   0,   0}, {211,   0,   0}, {207,   0,   0},
    {203,   0,   0}, {199,   0,   0}, {195,   0,   0}, {191,   0,   0},
    {187,   0,   0}, {183,   0,   0}, {179,   0,   0}, {175,   0,   0},
    {171,   0,   0}, {167,   0,   0}, {163,   0,   0}, {159,   0,   0},
    {155,   0,   0}, {151,   0,   0}, {147,   0,   0}, {143,   0,   0},
    {139,   0,   0}, {135,   0,   0}, {131,   0,   0}, {128,   0,   0},
};

void
ag_disparity_colorize (const int16_t *disparity, uint32_t width,
                       uint32_t height, int min_disparity,
                       int num_disparities, uint8_t *rgb_out)
{
    int16_t d_min = (int16_t) (min_disparity * 16);
    int     range = num_disparities * 16;

    for (uint32_t i = 0; i < width * height; i++) {
        uint8_t *dst = rgb_out + (size_t) i * 3;
        int16_t d = disparity[i];

        if (d <= d_min || range <= 0) {
            dst[0] = 0;
            dst[1] = 0;
            dst[2] = 0;
            continue;
        }

        int idx = ((int) (d - d_min) * 255) / range;
        if (idx < 0)   idx = 0;
        if (idx > 255) idx = 255;

        dst[0] = jet_lut[idx][0];
        dst[1] = jet_lut[idx][1];
        dst[2] = jet_lut[idx][2];
    }
}
