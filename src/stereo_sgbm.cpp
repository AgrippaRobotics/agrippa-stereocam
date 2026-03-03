/*
 * stereo_sgbm.cpp — thin C wrapper around OpenCV StereoSGBM
 *
 * This is the only C++ file in the project.  It exposes extern "C"
 * functions consumed by stereo_common.c so the rest of the codebase
 * remains pure C99.
 *
 * Requires: opencv4 (opencv_core, opencv_calib3d, opencv_imgproc,
 *                     opencv_ximgproc for WLS filter)
 */

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc/disparity_filter.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>

/* Import the C header for types. */
extern "C" {
#include "stereo.h"
}

/* ------------------------------------------------------------------ */
/*  Opaque handle wraps cv::Ptr<cv::StereoSGBM>                       */
/* ------------------------------------------------------------------ */

struct SgbmHandle {
    cv::Ptr<cv::StereoSGBM> sgbm;
    uint32_t width;
    uint32_t height;
};

/* ------------------------------------------------------------------ */
/*  extern "C" API                                                     */
/* ------------------------------------------------------------------ */

extern "C" void *
ag_sgbm_create (uint32_t width, uint32_t height,
                const AgSgbmParams *params)
{
    int p1 = params->p1;
    int p2 = params->p2;

    /* Auto-derive penalties from block_size if not explicitly set. */
    if (p1 == 0)
        p1 = 8 * params->block_size * params->block_size;
    if (p2 == 0)
        p2 = 32 * params->block_size * params->block_size;

    auto sgbm = cv::StereoSGBM::create (
        params->min_disparity,
        params->num_disparities,
        params->block_size,
        p1,
        p2,
        params->disp12_max_diff,
        params->pre_filter_cap,
        params->uniqueness_ratio,
        params->speckle_window_size,
        params->speckle_range,
        params->mode);

    if (!sgbm) {
        fprintf (stderr, "sgbm: failed to create StereoSGBM\n");
        return nullptr;
    }

    auto *handle = new SgbmHandle;
    handle->sgbm   = sgbm;
    handle->width  = width;
    handle->height = height;

    printf ("StereoSGBM: numDisp=%d blockSize=%d P1=%d P2=%d mode=%d\n",
            params->num_disparities, params->block_size, p1, p2, params->mode);

    return handle;
}

extern "C" int
ag_sgbm_compute (void *sgbm_ptr, uint32_t width, uint32_t height,
                 const uint8_t *left, const uint8_t *right,
                 int16_t *disparity_out)
{
    auto *handle = static_cast<SgbmHandle *> (sgbm_ptr);

    cv::Mat left_mat  (height, width, CV_8UC1, const_cast<uint8_t *> (left));
    cv::Mat right_mat (height, width, CV_8UC1, const_cast<uint8_t *> (right));
    cv::Mat disp_mat;

    handle->sgbm->compute (left_mat, right_mat, disp_mat);

    /* StereoSGBM::compute produces CV_16S with values in Q4.4 fixed point
     * (i.e. real disparity = mat_value / 16.0).  This maps directly to
     * our int16_t output format. */
    if (disp_mat.type () != CV_16S ||
        (uint32_t) disp_mat.cols != width ||
        (uint32_t) disp_mat.rows != height) {
        fprintf (stderr, "sgbm: unexpected output format\n");
        return -1;
    }

    /* Copy row-by-row in case of non-contiguous Mat. */
    for (uint32_t y = 0; y < height; y++) {
        const int16_t *src = disp_mat.ptr<int16_t> (y);
        int16_t *dst = disparity_out + (size_t) y * width;
        memcpy (dst, src, width * sizeof (int16_t));
    }

    return 0;
}

extern "C" int
ag_sgbm_update_params (void *sgbm_ptr, const AgSgbmParams *params)
{
    if (!sgbm_ptr || !params)
        return -1;

    auto *handle = static_cast<SgbmHandle *> (sgbm_ptr);

    int p1 = params->p1;
    int p2 = params->p2;
    if (p1 == 0)
        p1 = 8 * params->block_size * params->block_size;
    if (p2 == 0)
        p2 = 32 * params->block_size * params->block_size;

    handle->sgbm->setMinDisparity (params->min_disparity);
    handle->sgbm->setNumDisparities (params->num_disparities);
    handle->sgbm->setBlockSize (params->block_size);
    handle->sgbm->setP1 (p1);
    handle->sgbm->setP2 (p2);
    handle->sgbm->setDisp12MaxDiff (params->disp12_max_diff);
    handle->sgbm->setPreFilterCap (params->pre_filter_cap);
    handle->sgbm->setUniquenessRatio (params->uniqueness_ratio);
    handle->sgbm->setSpeckleWindowSize (params->speckle_window_size);
    handle->sgbm->setSpeckleRange (params->speckle_range);
    handle->sgbm->setMode (params->mode);
    return 0;
}

extern "C" void
ag_sgbm_destroy (void *sgbm_ptr)
{
    delete static_cast<SgbmHandle *> (sgbm_ptr);
}

/* ------------------------------------------------------------------ */
/*  Left-right disparity computation (for WLS filter)                  */
/* ------------------------------------------------------------------ */

extern "C" int
ag_sgbm_compute_lr (void *sgbm_ptr, uint32_t width, uint32_t height,
                     const uint8_t *left, const uint8_t *right,
                     int16_t *disp_lr_out, int16_t *disp_rl_out)
{
    auto *handle = static_cast<SgbmHandle *> (sgbm_ptr);

    cv::Mat left_mat  (height, width, CV_8UC1, const_cast<uint8_t *> (left));
    cv::Mat right_mat (height, width, CV_8UC1, const_cast<uint8_t *> (right));
    cv::Mat disp_lr, disp_rl;

    /* Left-to-right (normal). */
    handle->sgbm->compute (left_mat, right_mat, disp_lr);

    /* Right-to-left (create matching right matcher). */
    auto right_matcher = cv::ximgproc::createRightMatcher (handle->sgbm);
    right_matcher->compute (right_mat, left_mat, disp_rl);

    if (disp_lr.type () != CV_16S || disp_rl.type () != CV_16S ||
        (uint32_t) disp_lr.cols != width ||
        (uint32_t) disp_lr.rows != height) {
        fprintf (stderr, "sgbm: unexpected LR output format\n");
        return -1;
    }

    for (uint32_t y = 0; y < height; y++) {
        memcpy (disp_lr_out + (size_t) y * width,
                disp_lr.ptr<int16_t> (y),
                width * sizeof (int16_t));
        memcpy (disp_rl_out + (size_t) y * width,
                disp_rl.ptr<int16_t> (y),
                width * sizeof (int16_t));
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  WLS (Weighted Least Squares) disparity filter                      */
/* ------------------------------------------------------------------ */

extern "C" int
ag_sgbm_wls_filter (void *sgbm_ptr,
                     const uint8_t *left_guide,
                     const int16_t *disp_lr,
                     const int16_t *disp_rl,
                     int16_t *filtered_out,
                     uint32_t width, uint32_t height,
                     double lambda, double sigma_color)
{
    auto *handle = static_cast<SgbmHandle *> (sgbm_ptr);

    cv::Mat guide (height, width, CV_8UC1,
                   const_cast<uint8_t *> (left_guide));
    cv::Mat lr_mat (height, width, CV_16SC1,
                    const_cast<int16_t *> (disp_lr));
    cv::Mat rl_mat (height, width, CV_16SC1,
                    const_cast<int16_t *> (disp_rl));
    cv::Mat filtered;

    auto wls = cv::ximgproc::createDisparityWLSFilter (handle->sgbm);
    wls->setLambda (lambda);
    wls->setSigmaColor (sigma_color);
    wls->filter (lr_mat, guide, filtered, rl_mat);

    if (filtered.type () != CV_16S) {
        fprintf (stderr, "sgbm: WLS filter produced unexpected type\n");
        return -1;
    }

    for (uint32_t y = 0; y < height; y++) {
        memcpy (filtered_out + (size_t) y * width,
                filtered.ptr<int16_t> (y),
                width * sizeof (int16_t));
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  CLAHE pre-processing                                               */
/* ------------------------------------------------------------------ */

extern "C" void
ag_clahe_apply (const uint8_t *input, uint8_t *output,
                uint32_t width, uint32_t height,
                double clip_limit, int tile_size)
{
    cv::Mat in_mat (height, width, CV_8UC1,
                    const_cast<uint8_t *> (input));
    cv::Mat out_mat;

    auto clahe = cv::createCLAHE (clip_limit,
                                   cv::Size (tile_size, tile_size));
    clahe->apply (in_mat, out_mat);

    /* Copy result row-by-row in case of non-contiguous Mat. */
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *src = out_mat.ptr<uint8_t> (y);
        uint8_t *dst = output + (size_t) y * width;
        memcpy (dst, src, width);
    }
}
