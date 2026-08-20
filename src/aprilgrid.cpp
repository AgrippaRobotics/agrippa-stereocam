/* aprilgrid.cpp — see aprilgrid.hpp. */

#ifdef HAVE_OPENCV

#include "aprilgrid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
extern "C" {
#include <apriltag.h>
#include <tag36h11.h>
}

namespace {

const int DATA_CELLS   = 6;    /* tag36h11 payload is 6x6 */
const int BORDER_CELLS = 2;    /* Kalibr AprilGrid black border; apriltag3 assumes 1 */

const int    kGuidedPasses = 4;    /* each accepted tag improves its neighbours */
const double kMinContrast  = 25.0; /* data-cell span below which a read is junk */
const int    kMaxHamming   = 2;
const int    kNeighbours   = 6;
const int    kCellPx       = 12;   /* warp resolution per cell */
const double kMinPerimeter = 40.0; /* contour smaller than this cannot be a tag */
const double kMinArea      = 200.0;
const double kSeedTolPx    = 4.0;  /* neighbours place a good seed to well under 1 px */

/* Plain-pass sweep. Which adaptive-threshold window works depends on how big the tags
 * land in the frame, and corner refinement helps on some frames and loses tags on
 * others, so try the grid and keep whichever reads most. */
const int kThreshWindows[4][3] = {{3, 23, 10}, {11, 51, 10}, {15, 75, 10}, {21, 101, 20}};

/*
 * The 6x6 data matrix tag36h11 holds for each id, straight out of libapriltag's own
 * code table.
 *
 * Previously this rendered an ArUco marker at borderBits=1 and cropped it, which is
 * what the Python does because cv2 exposes no bit table. In C the table IS the
 * dependency we already have: tf->codes[id] with tf->bit_x/bit_y, and stock tag36h11
 * puts its 36 payload bits at 1..6 inside an 8-wide border box. Verified against tags
 * an independent decoder had already read: hamming 0, every tag.
 */
const std::vector<cv::Mat> &expected_bits_table ()
{
    static std::vector<cv::Mat> table;
    if (!table.empty ())
        return table;
    apriltag_family_t *tf = tag36h11_create ();
    table.resize (tf->ncodes);
    for (uint32_t id = 0; id < tf->ncodes; id++) {
        cv::Mat b (DATA_CELLS, DATA_CELLS, CV_8UC1, cv::Scalar (0));
        const uint64_t code = tf->codes[id];
        for (uint32_t i = 0; i < tf->nbits; i++)
            b.at<uchar> (static_cast<int> (tf->bit_y[i]) - 1,
                         static_cast<int> (tf->bit_x[i]) - 1) =
                (uchar) ((code >> (tf->nbits - 1 - i)) & 1ULL);
        /* 180 degrees. libapriltag's payload convention is rot90(k=2) from the one the
         * board is printed in -- verified over all 70 ids: same codes, no id
         * permutation, a constant two-quarter-turn apart. The guided pass would absorb
         * this (it matches over all four rotations) but the seed pass must not: it pins
         * each seed's winding by a DIRECT comparison, and a seed wound 180 degrees out
         * makes the local homography fit a lattice half a tag off. Measured: every one
         * of 70 tags came back cyclically shifted by 2, and 12.7 px from the corner it
         * should be. */
        cv::Mat r;
        cv::flip (b, r, -1);
        table[id] = r;
    }
    tag36h11_destroy (tf);
    return table;
}

const cv::Mat &expected_bits (int tag_id) { return expected_bits_table ()[tag_id]; }

cv::Point2f centroid (const std::vector<cv::Point2f> &p)
{
    cv::Point2f c (0.f, 0.f);
    for (size_t i = 0; i < p.size (); i++) c += p[i];
    return c * (1.0f / static_cast<float> (p.size ()));
}

/*
 * Subpixel corners. Deliberately UNCLAMPED.
 *
 * A leash on how far a corner may travel was tried and made things worse: it fires on
 * some frames and not others, so the same corner lands in two different places and the
 * frame-to-frame spread goes bimodal (p90 0.03 px -> 0.85 px over 6 static frames). A
 * few edge-tag corners do jump a cell when cornerSubPix latches onto the diagonally
 * touching gap square; leave those to the solver's outlier rejection rather than
 * making every corner less repeatable.
 */
void refine (const cv::Mat &gray, std::vector<cv::Point2f> &quad)
{
    cv::cornerSubPix (gray, quad, cv::Size (5, 5), cv::Size (-1, -1),
                      cv::TermCriteria (cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER,
                                        30, 0.01));
}

/* Warp the tag's black square flat and read its data cells. Returns the contrast
 * (span between darkest and brightest cell); `bits` gets the 6x6 payload. */
double read_cells (const cv::Mat &gray, const std::vector<cv::Point2f> &quad, cv::Mat &bits)
{
    const int px = kCellPx;
    const int side = (DATA_CELLS + 2 * BORDER_CELLS) * px;
    std::vector<cv::Point2f> target;
    target.push_back (cv::Point2f (0, 0));
    target.push_back (cv::Point2f (side, 0));
    target.push_back (cv::Point2f (side, side));
    target.push_back (cv::Point2f (0, side));

    cv::Mat warp;
    cv::warpPerspective (gray, warp, cv::getPerspectiveTransform (quad, target),
                         cv::Size (side, side));

    cv::Mat vals (DATA_CELLS, DATA_CELLS, CV_64FC1);
    double lo = std::numeric_limits<double>::infinity (), hi = -lo;
    for (int r = 0; r < DATA_CELLS; r++)
        for (int c = 0; c < DATA_CELLS; c++) {
            const int y = (r + BORDER_CELLS) * px, x = (c + BORDER_CELLS) * px;
            const double v = cv::mean (warp (cv::Rect (x + px / 4, y + px / 4,
                                                       px / 2, px / 2)))[0];
            vals.at<double> (r, c) = v;
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
    const double thresh = 0.5 * (lo + hi);
    bits.create (DATA_CELLS, DATA_CELLS, CV_8UC1);
    for (int r = 0; r < DATA_CELLS; r++)
        for (int c = 0; c < DATA_CELLS; c++)
            bits.at<uchar> (r, c) = vals.at<double> (r, c) > thresh ? 1 : 0;
    return hi - lo;
}

/* np.rot90 with k quarter-turns, counter-clockwise, on a square 8-bit matrix. */
cv::Mat rot90 (const cv::Mat &m, int k)
{
    cv::Mat out = m.clone ();
    for (int i = 0; i < k; i++) {
        cv::Mat t;
        cv::transpose (out, t);
        cv::flip (t, out, 0);          /* transpose + flip vertical == CCW */
    }
    return out;
}

/* Best match over the four rotations -- a predicted quad's winding is arbitrary. */
int hamming (const cv::Mat &bits, const cv::Mat &want)
{
    int best = DATA_CELLS * DATA_CELLS + 1;
    for (int k = 0; k < 4; k++) {
        const cv::Mat r = rot90 (bits, k);
        int d = 0;
        for (int y = 0; y < DATA_CELLS; y++)
            for (int x = 0; x < DATA_CELLS; x++)
                d += (r.at<uchar> (y, x) != want.at<uchar> (y, x));
        best = std::min (best, d);
    }
    return best;
}

/*
 * Board->image fit from the nearest found tags.
 *
 * Local on purpose: this lens distorts enough that ONE global homography leaves a
 * ~3.5 px median residual, far too coarse to predict a tag quad well enough to decode.
 */
bool local_homography (const AgGridSpec &spec, int tag_id,
                       const std::map<int, std::vector<cv::Point2f> > &found,
                       bool flip_x, bool flip_y, cv::Mat &H)
{
    std::vector<cv::Point2f> here;
    ag_grid_board_corners (spec, tag_id, flip_x, flip_y, here);
    const cv::Point2f c = centroid (here);

    std::vector<std::pair<double, int> > bydist;
    for (std::map<int, std::vector<cv::Point2f> >::const_iterator it = found.begin ();
         it != found.end (); ++it) {
        std::vector<cv::Point2f> bc;
        ag_grid_board_corners (spec, it->first, flip_x, flip_y, bc);
        bydist.push_back (std::make_pair (cv::norm (centroid (bc) - c), it->first));
    }
    std::sort (bydist.begin (), bydist.end ());
    const size_t take = std::min (static_cast<size_t> (kNeighbours), bydist.size ());
    if (take < 3)
        return false;

    std::vector<cv::Point2f> src, dst;
    for (size_t i = 0; i < take; i++) {
        std::vector<cv::Point2f> bc;
        ag_grid_board_corners (spec, bydist[i].second, flip_x, flip_y, bc);
        const std::vector<cv::Point2f> &im = found.find (bydist[i].second)->second;
        src.insert (src.end (), bc.begin (), bc.end ());
        dst.insert (dst.end (), im.begin (), im.end ());
    }
    H = cv::findHomography (src, dst, 0);
    return !H.empty ();
}

/*
 * Seed pass: candidate quads from plain imgproc, decoded with the same reader the
 * guided pass uses.
 *
 * Deliberately NOT ArUco. ArUco would work, but it lives in opencv_contrib on the
 * OpenCV that Ubuntu 22.04 packages, and pulling contrib in drags GDAL, GTK-3, x265 and
 * GDCM -- 279 packages, +314 MB on a controller with a 5 GB rootfs -- to get one
 * adaptive-threshold-and-decode that is a few dozen lines of imgproc. Dropping it keeps
 * the whole detector on core+imgproc+calib3d, which the distro packages plainly.
 *
 * It also reads MORE, because the decoder is better matched to this board than either
 * ArUco's or libapriltag's: measured over 20 eyes of sweep_am this seeds 21-46 tags per
 * eye where ArUco managed 8-31 and libapriltag 0-11. The guided pass needs four.
 */
std::map<int, std::vector<cv::Point2f> > blind_detect (const cv::Mat &gray, int num_tags)
{
    std::map<int, std::vector<cv::Point2f> > found;
    const std::vector<cv::Mat> &want = expected_bits_table ();

    for (int w = 0; w < 4; w++) {
        cv::Mat th;
        cv::adaptiveThreshold (gray, th, 255, cv::ADAPTIVE_THRESH_MEAN_C,
                               cv::THRESH_BINARY_INV, kThreshWindows[w][1] | 1,
                               kThreshWindows[w][2]);
        std::vector<std::vector<cv::Point> > contours;
        cv::findContours (th, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

        for (size_t i = 0; i < contours.size (); i++) {
            const double per = cv::arcLength (contours[i], true);
            if (per < kMinPerimeter)
                continue;
            std::vector<cv::Point> ap;
            cv::approxPolyDP (contours[i], ap, 0.05 * per, true);
            if (ap.size () != 4 || !cv::isContourConvex (ap) || cv::contourArea (ap) < kMinArea)
                continue;

            std::vector<cv::Point2f> quad;
            for (int k = 0; k < 4; k++)
                quad.push_back (cv::Point2f ((float) ap[k].x, (float) ap[k].y));
            /* One handedness, so the remaining ambiguity is a cyclic shift only. */
            if (cv::contourArea (quad, true) < 0)
                std::reverse (quad.begin (), quad.end ());

            /* Try each cyclic shift and keep the one that decodes DIRECTLY, so the
             * accepted corners are already in board order -- the local homography the
             * guided pass fits is only as good as the seed winding. */
            for (int shift = 0; shift < 4; shift++) {
                std::vector<cv::Point2f> q;
                for (int k = 0; k < 4; k++)
                    q.push_back (quad[(k + shift) % 4]);
                cv::Mat bits;
                if (read_cells (gray, q, bits) <= kMinContrast)
                    break;                       /* contrast does not depend on shift */
                bool hit = false;
                for (int id = 0; id < num_tags && !hit; id++) {
                    if (found.count (id))
                        continue;
                    int d = 0;
                    for (int y = 0; y < DATA_CELLS; y++)
                        for (int x = 0; x < DATA_CELLS; x++)
                            d += (bits.at<uchar> (y, x) != want[id].at<uchar> (y, x));
                    if (d <= kMaxHamming) {
                        /* approxPolyDP corners are integer, and the guided pass fits
                         * its local homography on these -- a whole-pixel seed corner
                         * is enough to make a neighbour fail verification. */
                        refine (gray, q);
                        found[id] = q;
                        hit = true;
                    }
                }
                if (hit)
                    break;
            }
        }
    }
    return found;
}

}  // namespace

void
ag_grid_board_corners (const AgGridSpec &spec, int tag_id, bool flip_x, bool flip_y,
                       std::vector<cv::Point2f> &out)
{
    int row = tag_id / spec.columns;
    int col = tag_id % spec.columns;
    if (flip_x) col = spec.columns - 1 - col;
    if (flip_y) row = spec.rows - 1 - row;
    const float x0 = static_cast<float> (col * spec.pitch_mm ());
    const float y0 = static_cast<float> (row * spec.pitch_mm ());
    const float t = static_cast<float> (spec.tag_mm);
    out.clear ();
    out.push_back (cv::Point2f (x0, y0));
    out.push_back (cv::Point2f (x0 + t, y0));
    out.push_back (cv::Point2f (x0 + t, y0 + t));
    out.push_back (cv::Point2f (x0, y0 + t));
}

double
ag_grid_pick_orientation (const AgGridSpec &spec,
                          const std::map<int, std::vector<cv::Point2f> > &found,
                          bool *flip_x, bool *flip_y)
{
    double best = std::numeric_limits<double>::infinity ();
    *flip_x = true; *flip_y = false;
    for (int fx = 0; fx < 2; fx++) {
        for (int fy = 0; fy < 2; fy++) {
            std::vector<cv::Point2f> src, dst;
            for (std::map<int, std::vector<cv::Point2f> >::const_iterator it = found.begin ();
                 it != found.end (); ++it) {
                std::vector<cv::Point2f> bc;
                ag_grid_board_corners (spec, it->first, fx != 0, fy != 0, bc);
                src.insert (src.end (), bc.begin (), bc.end ());
                dst.insert (dst.end (), it->second.begin (), it->second.end ());
            }
            if (src.size () < 4)
                continue;
            cv::Mat H = cv::findHomography (src, dst, cv::RANSAC, 5.0);
            if (H.empty ())
                continue;
            std::vector<cv::Point2f> proj;
            cv::perspectiveTransform (src, proj, H);
            std::vector<double> err (proj.size ());
            for (size_t i = 0; i < proj.size (); i++)
                err[i] = cv::norm (proj[i] - dst[i]);
            std::nth_element (err.begin (), err.begin () + err.size () / 2, err.end ());
            const double median = err[err.size () / 2];
            if (median < best) { best = median; *flip_x = fx != 0; *flip_y = fy != 0; }
        }
    }
    return best;
}

AgGridDetection
ag_grid_detect (const cv::Mat &gray, const AgGridSpec &spec)
{
    AgGridDetection out;
    out.spec = spec;
    out.corners = blind_detect (gray, spec.num_tags ());
    for (std::map<int, std::vector<cv::Point2f> >::const_iterator it = out.corners.begin ();
         it != out.corners.end (); ++it)
        out.blind_ids.insert (it->first);
    if (out.corners.size () < 4)
        return out;

    ag_grid_pick_orientation (spec, out.corners, &out.flip_x, &out.flip_y);

    /*
     * Throw out seeds the rest of the board disagrees with.
     *
     * The seed pass reads each candidate quad in isolation, so a quad that landed on
     * the wrong square can still decode -- and once an id is seeded the guided pass
     * never revisits it, so one bad seed both squats on its id AND drags the local
     * homography of every neighbour that uses it. Measured against the reference
     * detector, the bad ones sit ~12 px out while the good ones agree to 0.0006 px, so
     * they separate cleanly: fit each seed from its neighbours ONLY and drop the ones
     * the fit cannot place. A dropped id is not lost -- the guided pass re-derives it
     * from the survivors, which is exactly the machinery that recovers 20-40 tags a
     * frame anyway.
     */
    {
        std::map<int, std::vector<cv::Point2f> > seeds = out.corners;
        for (std::map<int, std::vector<cv::Point2f> >::const_iterator it = seeds.begin ();
             it != seeds.end (); ++it) {
            std::map<int, std::vector<cv::Point2f> > others = seeds;
            others.erase (it->first);
            cv::Mat H;
            if (!local_homography (spec, it->first, others, out.flip_x, out.flip_y, H))
                continue;
            std::vector<cv::Point2f> bc, pred;
            ag_grid_board_corners (spec, it->first, out.flip_x, out.flip_y, bc);
            cv::perspectiveTransform (bc, pred, H);
            double worst = 0;
            for (size_t k = 0; k < 4; k++)
                worst = std::max (worst, cv::norm (pred[k] - it->second[k]));
            if (worst > kSeedTolPx) {
                out.corners.erase (it->first);
                out.blind_ids.erase (it->first);
            }
        }
    }
    if (out.corners.size () < 4)
        return out;

    for (int pass = 0; pass < kGuidedPasses; pass++) {
        bool progress = false;
        for (int id = 0; id < spec.num_tags (); id++) {
            if (out.corners.count (id))
                continue;
            cv::Mat H;
            if (!local_homography (spec, id, out.corners, out.flip_x, out.flip_y, H))
                continue;
            std::vector<cv::Point2f> bc, quad;
            ag_grid_board_corners (spec, id, out.flip_x, out.flip_y, bc);
            cv::perspectiveTransform (bc, quad, H);

            bool inside = true;
            for (size_t i = 0; i < quad.size (); i++)
                if (quad[i].x <= 1 || quad[i].x >= gray.cols - 2 ||
                    quad[i].y <= 1 || quad[i].y >= gray.rows - 2)
                    inside = false;
            if (!inside)
                continue;                       /* predicted outside the frame */

            refine (gray, quad);
            cv::Mat bits;
            const double contrast = read_cells (gray, quad, bits);
            if (contrast <= kMinContrast || hamming (bits, expected_bits (id)) > kMaxHamming)
                continue;
            out.corners[id] = quad;
            out.guided_ids.insert (id);
            progress = true;
        }
        if (!progress)
            break;
    }

    /* One refinement convention for every tag. The plain sweep keeps whichever
     * threshold window read the most tags, and that choice moves frame to frame -- so
     * without this a tag read blind in one frame and predicted in the next comes back
     * with corners from two different estimators. Measured over 6 frames of a static
     * scene, unifying the pass cuts the p90 corner spread by roughly half. */
    for (std::map<int, std::vector<cv::Point2f> >::iterator it = out.corners.begin ();
         it != out.corners.end (); ++it)
        refine (gray, it->second);

    return out;
}

#endif /* HAVE_OPENCV */
