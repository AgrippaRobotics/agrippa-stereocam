/*
 * calib_solve.hpp — solve stereo intrinsics, extrinsics and hand-eye from a sweep.
 *
 * Consumes a session written by the on-robot capture RPC (planar Bayer view_NNN.raw
 * plus a capture.jsonl carrying base_T_flange per view) and emits the calib_result/
 * directory that `ag-cam-tools calibration-stash upload` packs into the camera.
 *
 * The output contract is fixed by the camera's own unpacker (calib_archive.c / npy.c):
 * every array is little-endian float64 C-order, K 3x3, R 3x3, P 3x4, D 1xN. A float32
 * array does not fail loudly there -- it parses as garbage -- which is why ag_npy_save
 * has no dtype parameter.
 *
 * Ported from makeit-operate's makeit_operate/calibration/solve_stereo.py. OpenCV-only
 * (HAVE_OPENCV), same as the grid detector it sits on.
 */

#ifndef AG_CALIB_SOLVE_HPP
#define AG_CALIB_SOLVE_HPP

#ifdef HAVE_OPENCV

#include <string>
#include "aprilgrid.hpp"

struct AgCalibSolveOpts {
    AgGridSpec spec;
    int    eye_w = 1440;
    int    eye_h = 1080;
    double alpha = 0.0;          /* stereoRectify alpha; PINNED -- see the .cpp */
    bool   keep_outliers = false;
    bool   refine_intrinsics = false;
    int    min_views = 8;
    bool   verbose = true;
};

/* Solve `session_dir` into `out_dir` (default <session>/calib_result if empty).
 * Returns 0 on success, -1 on failure (diagnostic on stderr). */
int ag_calib_solve (const std::string &session_dir, const std::string &out_dir,
                    const AgCalibSolveOpts &opts);

#endif /* HAVE_OPENCV */
#endif /* AG_CALIB_SOLVE_HPP */
