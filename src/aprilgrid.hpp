/*
 * aprilgrid.hpp — whole-board detection for calib.io / Kalibr AprilGrid targets.
 *
 * The border-2 family in apriltag_detect.c makes a SINGLE tag decodable. This is the
 * other half: it knows the BOARD, so a tag the plain pass missed can be predicted from
 * its neighbours and verified rather than lost. On the bench that is the difference
 * between a handful of tags and all 70 -- and the calibration solve wants all 70,
 * because the ones a plain pass drops are the oblique, dim, frame-edge tags that carry
 * most of the distortion signal.
 *
 * Needs only OpenCV core+imgproc+calib3d (guarded by HAVE_OPENCV_CALIB), plus libapriltag's
 * tag36h11 code table -- which is already vendored, and which the b2 family leaves
 * untouched (verified: 0 of 587 codes differ, so stock and b2 are the same table).
 * Deliberately no ArUco: it would do the seed pass, but on the OpenCV that Ubuntu
 * 22.04 packages ArUco lives in opencv_contrib, and pulling contrib in drags GDAL,
 * GTK-3, x265 and GDCM -- 279 packages, +314 MB on a box with a 5 GB rootfs -- for one
 * adaptive-threshold-and-decode. Without it the three modules we do need are stock apt
 * packages: +18 MB, no source build, no version pin.
 *
 * libapriltag's DETECTOR is also not used here, and that is not an oversight: it finds
 * the quads and then decodes almost none of them at the ~3.5 px/cell this board lands
 * at -- 27-34 quads a frame for 0-11 decodes, exactly zero on 7 of 11 frames, including
 * clean fronto-parallel ones. quad_sigma, decode_sharpening, min_white_black_diff,
 * min_cluster_pixels and 2x upsampling each made it worse. The reader in this file
 * manages 21-49 seeds a frame on the same images. The guided pass needs four, so the
 * seed pass is where the whole thing lives or dies.
 */

#ifndef AG_APRILGRID_HPP
#define AG_APRILGRID_HPP

#ifdef HAVE_OPENCV_CALIB

#include <map>
#include <set>
#include <vector>
#include <opencv2/core.hpp>

/* Physical geometry of the target. Millimetres. Defaults match the calib.io board on
 * the bench: 7x10, 25 mm tags, 7.5 mm gaps, tag36h11 in the two-cell-border
 * convention. */
struct AgGridSpec {
    int    columns = 10;
    int    rows    = 7;
    double tag_mm  = 25.0;
    double gap_mm  = 7.5;

    double pitch_mm () const { return tag_mm + gap_mm; }
    int    num_tags () const { return rows * columns; }
};

struct AgGridDetection {
    /* tag id -> 4 corners, ArUco order, pixels. */
    std::map<int, std::vector<cv::Point2f> > corners;
    std::set<int> blind_ids;      /* read by the plain pass */
    std::set<int> guided_ids;     /* recovered by prediction + verification */
    bool flip_x = true;
    bool flip_y = false;
    AgGridSpec spec;
};

/*
 * The tag's four black-square corners in board millimetres, ArUco order.
 *
 * flip_x/flip_y express how the board is mounted relative to the camera. Do not
 * assume them -- ag_grid_pick_orientation() chooses by homography residual.
 */
void ag_grid_board_corners (const AgGridSpec &spec, int tag_id,
                            bool flip_x, bool flip_y,
                            std::vector<cv::Point2f> &out);

/* Which way the ids run on this board, by lowest global homography residual.
 * Returns that residual in pixels; sets flip_x/flip_y. */
double ag_grid_pick_orientation (const AgGridSpec &spec,
                                 const std::map<int, std::vector<cv::Point2f> > &found,
                                 bool *flip_x, bool *flip_y);

/* Plain ArUco pass with the two-cell border model, then neighbour-guided prediction
 * with per-tag verification. `gray` is 8-bit single channel. */
AgGridDetection ag_grid_detect (const cv::Mat &gray, const AgGridSpec &spec);

#endif /* HAVE_OPENCV_CALIB */
#endif /* AG_APRILGRID_HPP */
