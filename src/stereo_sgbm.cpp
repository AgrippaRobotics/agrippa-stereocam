/*
 * stereo_sgbm.cpp — thin C wrapper around OpenCV StereoSGBM
 *
 * This is the only C++ file in the project.  It exposes extern "C"
 * functions consumed by stereo_common.c so the rest of the codebase
 * remains pure C99.
 *
 * Requires: opencv4 (opencv_core, opencv_calib3d, opencv_imgproc)
 */

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <cstdint>
#include <cstdio>

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

extern "C" void
ag_sgbm_destroy (void *sgbm_ptr)
{
    delete static_cast<SgbmHandle *> (sgbm_ptr);
}
