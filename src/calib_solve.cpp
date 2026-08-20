/* calib_solve.cpp — see calib_solve.hpp. */

#ifdef HAVE_OPENCV

#include "calib_solve.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <ctime>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

extern "C" {
#include "npy.h"
#include "../vendor/cJSON.h"
}

namespace {

/*
 * The distortion model must be IDENTICAL in the per-eye fit and the stereo fit.
 * stereoCalibrate without this flag reads only the first 5 coefficients of an 8+
 * coefficient vector and silently ignores k4..k6 -- the stereo fit then optimises
 * against a different lens model than the intrinsics were fitted under. Measured on a
 * real session: 44.0 px RMS and a 267 mm baseline, versus 0.27 px and 40 mm with the
 * flag. It fails as a plausible-looking number, not as an error.
 */
const int DIST_MODEL_FLAG = cv::CALIB_RATIONAL_MODEL;

const char *const ARCHIVE_FILES[8] = {
    "cam_mats_left", "cam_mats_right", "dist_coefs_left", "dist_coefs_right",
    "rect_trans_left", "rect_trans_right", "proj_mats_left", "proj_mats_right"};

struct View {
    int index;
    cv::Matx44d base_T_flange;
    /* tags seen by BOTH eyes -- the stereo pair */
    std::vector<cv::Point3f> obj;
    std::vector<cv::Point2f> left, right;
    /* per-eye sets for the intrinsics fit (every tag that eye saw) */
    std::vector<cv::Point3f> obj_l, obj_r;
    std::vector<cv::Point2f> img_l, img_r;
};

cv::Matx44d rt (const cv::Vec3d &rvec, const cv::Vec3d &tvec)
{
    cv::Matx33d R;
    cv::Rodrigues (rvec, R);
    cv::Matx44d T = cv::Matx44d::eye ();
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) T(r, c) = R(r, c);
        T(r, 3) = tvec[r];
    }
    return T;
}

cv::Vec3d translation_of (const cv::Matx44d &T) { return cv::Vec3d (T(0,3), T(1,3), T(2,3)); }

/*
 * Demosaic one eye out of an `ag-cam-tools serve` /latest.raw buffer.
 *
 * The buffer is PLANAR -- the whole left image, then the whole right -- and despite the
 * gray8 header it is a Bayer RGGB mosaic. COLOR_BayerBG2GRAY is the phasing that
 * matches this sensor.
 */
bool load_raw_frame (const std::string &path, int eye, int w, int h, cv::Mat &gray)
{
    std::ifstream f (path.c_str (), std::ios::binary);
    if (!f)
        return false;
    std::vector<unsigned char> buf ((std::istreambuf_iterator<char> (f)),
                                     std::istreambuf_iterator<char> ());
    const size_t want = (size_t) 2 * w * h;
    if (buf.size () != want) {
        fprintf (stderr, "%s: expected %zu bytes for 2x%dx%d, got %zu\n",
                 path.c_str (), want, w, h, buf.size ());
        return false;
    }
    cv::Mat bayer (h, w, CV_8UC1, buf.data () + (size_t) eye * w * h);
    cv::cvtColor (bayer, gray, cv::COLOR_BayerBG2GRAY);
    return true;
}

/* Board corners for `ids`, in metres with z=0, paired with their image corners. */
void pack (const AgGridDetection &d, const std::vector<int> &ids,
           std::vector<cv::Point3f> &obj, std::vector<cv::Point2f> &img)
{
    obj.clear (); img.clear ();
    for (size_t i = 0; i < ids.size (); i++) {
        std::vector<cv::Point2f> bc;
        ag_grid_board_corners (d.spec, ids[i], d.flip_x, d.flip_y, bc);
        const std::vector<cv::Point2f> &im = d.corners.find (ids[i])->second;
        for (int k = 0; k < 4; k++) {
            obj.push_back (cv::Point3f (bc[k].x / 1000.f, bc[k].y / 1000.f, 0.f));
            img.push_back (im[k]);
        }
    }
}

bool parse_capture_line (const std::string &line, int *index, cv::Matx44d *pose)
{
    cJSON *root = cJSON_Parse (line.c_str ());
    if (!root)
        return false;
    bool ok = false;
    cJSON *idx = cJSON_GetObjectItem (root, "index");
    cJSON *btf = cJSON_GetObjectItem (root, "base_T_flange");
    if (cJSON_IsNumber (idx) && cJSON_IsArray (btf) && cJSON_GetArraySize (btf) == 6) {
        double v[6];
        for (int i = 0; i < 6; i++)
            v[i] = cJSON_GetArrayItem (btf, i)->valuedouble;
        *index = idx->valueint;
        /* [x y z rx ry rz] -- translation first, axis-angle second. */
        *pose = rt (cv::Vec3d (v[3], v[4], v[5]), cv::Vec3d (v[0], v[1], v[2]));
        ok = true;
    }
    cJSON_Delete (root);
    return ok;
}

/* Detect both eyes of every capture and pair the tags up by id. */
std::vector<View> load_views (const std::string &session, const AgCalibSolveOpts &o)
{
    std::vector<View> views;
    std::ifstream jl ((session + "/capture.jsonl").c_str ());
    if (!jl) {
        fprintf (stderr, "%s/capture.jsonl not found\n", session.c_str ());
        return views;
    }
    std::string line;
    while (std::getline (jl, line)) {
        if (line.empty ())
            continue;
        int index = -1;
        cv::Matx44d pose;
        if (!parse_capture_line (line, &index, &pose))
            continue;

        char name[512];
        snprintf (name, sizeof name, "%s/view_%03d.raw", session.c_str (), index);
        cv::Mat gray_l, gray_r;
        if (!load_raw_frame (name, 0, o.eye_w, o.eye_h, gray_l) ||
            !load_raw_frame (name, 1, o.eye_w, o.eye_h, gray_r)) {
            if (o.verbose) printf ("  view %d: frame missing, skipped\n", index);
            continue;
        }
        AgGridDetection dl = ag_grid_detect (gray_l, o.spec);
        AgGridDetection dr = ag_grid_detect (gray_r, o.spec);

        std::vector<int> shared, ids_l, ids_r;
        for (std::map<int, std::vector<cv::Point2f> >::const_iterator it = dl.corners.begin ();
             it != dl.corners.end (); ++it) {
            ids_l.push_back (it->first);
            if (dr.corners.count (it->first))
                shared.push_back (it->first);
        }
        for (std::map<int, std::vector<cv::Point2f> >::const_iterator it = dr.corners.begin ();
             it != dr.corners.end (); ++it)
            ids_r.push_back (it->first);

        if (shared.size () < 6) {
            if (o.verbose)
                printf ("  view %d: only %zu shared tags, skipped\n", index, shared.size ());
            continue;
        }
        View v;
        v.index = index;
        v.base_T_flange = pose;
        std::vector<cv::Point2f> tmp;
        pack (dl, shared, v.obj, v.left);
        pack (dr, shared, v.obj, v.right);      /* same obj, right-eye pixels */
        pack (dl, ids_l, v.obj_l, v.img_l);
        pack (dr, ids_r, v.obj_r, v.img_r);
        views.push_back (v);
        if (o.verbose)
            printf ("  view %2d: %2zu/%2zu tags, %2zu shared -> %zu pairs\n",
                    index, dl.corners.size (), dr.corners.size (), shared.size (),
                    4 * shared.size ());
    }
    return views;
}

struct Fit {
    cv::Mat K1, D1, K2, D2, R, T;
    cv::Mat K1m, D1m, K2m, D2m;               /* the mono fits, before the stereo pass */
    double rms_l, rms_r, rms_s;
    std::vector<cv::Mat> rvecs, tvecs;        /* left-eye board poses, for hand-eye */
};

double solve_intrinsics (const std::vector<std::vector<cv::Point3f> > &objs,
                         const std::vector<std::vector<cv::Point2f> > &imgs,
                         cv::Size size, const char *label, bool verbose,
                         cv::Mat &K, cv::Mat &D,
                         std::vector<cv::Mat> &rvecs, std::vector<cv::Mat> &tvecs)
{
    const double rms = cv::calibrateCamera (objs, imgs, size, K, D, rvecs, tvecs,
                                            DIST_MODEL_FLAG);
    double worst = 0; size_t worst_i = 0, n = 0; double sum = 0;
    for (size_t i = 0; i < objs.size (); i++) {
        std::vector<cv::Point2f> proj;
        cv::projectPoints (objs[i], rvecs[i], tvecs[i], K, D, proj);
        double m = 0;
        for (size_t j = 0; j < proj.size (); j++)
            m += cv::norm (proj[j] - imgs[i][j]);
        m /= (double) proj.size ();
        sum += m; n++;
        if (m > worst) { worst = m; worst_i = i; }
    }
    if (verbose)
        printf ("  %s: RMS %.3f px, per-view mean %.3f (worst %.3f on view %zu)\n",
                label, rms, sum / (double) n, worst, worst_i);
    return rms;
}

bool run_fit (const std::vector<View> &vs, cv::Size size, const AgCalibSolveOpts &o, Fit &f)
{
    std::vector<std::vector<cv::Point3f> > ol, orr, os;
    std::vector<std::vector<cv::Point2f> > il, ir, sl, sr;
    for (size_t i = 0; i < vs.size (); i++) {
        ol.push_back (vs[i].obj_l);  il.push_back (vs[i].img_l);
        orr.push_back (vs[i].obj_r); ir.push_back (vs[i].img_r);
        os.push_back (vs[i].obj);    sl.push_back (vs[i].left); sr.push_back (vs[i].right);
    }
    std::vector<cv::Mat> rv2, tv2;
    f.rms_l = solve_intrinsics (ol, il, size, "left ", o.verbose, f.K1, f.D1, f.rvecs, f.tvecs);
    f.rms_r = solve_intrinsics (orr, ir, size, "right", o.verbose, f.K2, f.D2, rv2, tv2);
    f.K1m = f.K1.clone (); f.D1m = f.D1.clone ();
    f.K2m = f.K2.clone (); f.D2m = f.D2.clone ();

    const int flags = DIST_MODEL_FLAG | (o.refine_intrinsics ? cv::CALIB_USE_INTRINSIC_GUESS
                                                             : cv::CALIB_FIX_INTRINSIC);
    cv::Mat E, F;
    f.rms_s = cv::stereoCalibrate (os, sl, sr, f.K1, f.D1, f.K2, f.D2, size,
                                   f.R, f.T, E, F, flags,
                                   cv::TermCriteria (cv::TermCriteria::EPS +
                                                     cv::TermCriteria::MAX_ITER, 100, 1e-6));
    return !f.R.empty () && !f.T.empty ();
}

/*
 * Per-view reprojection RMS through the SINGLE fitted stereo model.
 *
 * Solve the board pose from the LEFT eye, carry it to the right through the common
 * (R, T), and reproject into both. A view that does not conform to the one rigid
 * baseline shows up here and nowhere else -- per-eye reprojection cannot see it,
 * because each eye absorbs a relative rotation exactly into its own board pose.
 *
 * Same construction as the agrippa-stereocam calibration notebook's per-pair check.
 */
std::vector<double> per_view_stereo_rms (const std::vector<View> &vs, const Fit &f)
{
    std::vector<double> out;
    cv::Matx33d R (f.R.at<double> (0,0), f.R.at<double> (0,1), f.R.at<double> (0,2),
                   f.R.at<double> (1,0), f.R.at<double> (1,1), f.R.at<double> (1,2),
                   f.R.at<double> (2,0), f.R.at<double> (2,1), f.R.at<double> (2,2));
    cv::Vec3d T (f.T.at<double> (0), f.T.at<double> (1), f.T.at<double> (2));
    for (size_t i = 0; i < vs.size (); i++) {
        cv::Vec3d rl, tl;
        if (!cv::solvePnP (vs[i].obj, vs[i].left, f.K1, f.D1, rl, tl)) {
            out.push_back (std::numeric_limits<double>::infinity ());
            continue;
        }
        cv::Matx33d Rl;
        cv::Rodrigues (rl, Rl);
        cv::Vec3d rr;
        cv::Rodrigues (cv::Matx33d (R * Rl), rr);
        const cv::Vec3d tr = R * tl + T;
        std::vector<cv::Point2f> pl, pr;
        cv::projectPoints (vs[i].obj, rl, tl, f.K1, f.D1, pl);
        cv::projectPoints (vs[i].obj, rr, tr, f.K2, f.D2, pr);
        double el = 0, er = 0;
        for (size_t j = 0; j < pl.size (); j++) {
            const double a = cv::norm (pl[j] - vs[i].left[j]);
            const double b = cv::norm (pr[j] - vs[i].right[j]);
            el += a * a; er += b * b;
        }
        el /= (double) pl.size (); er /= (double) pr.size ();
        out.push_back (std::sqrt (0.5 * (el + er)));
    }
    return out;
}

/*
 * How far apart the two eyes place the board, in mm.
 *
 * This is the number that says whether a hand-eye residual is the CAMERA's fault. The
 * eyes are independent observers of the same rigid target, so their disagreement bounds
 * the vision error. If the hand-eye spread is far larger than this, the residual lives
 * in the robot's reported poses and no amount of extra views, extra distortion terms or
 * fusing the second eye will touch it.
 */
void eye_agreement (const std::vector<View> &vs, const Fit &f, double *mean, double *worst)
{
    cv::Vec3d rvecR;
    cv::Rodrigues (f.R, rvecR);
    const cv::Matx44d left_T_right =
        rt (rvecR, cv::Vec3d (f.T.at<double> (0), f.T.at<double> (1), f.T.at<double> (2))).inv ();
    double sum = 0, mx = 0;
    for (size_t i = 0; i < vs.size (); i++) {
        cv::Vec3d rl, tl, rr, tr;
        cv::solvePnP (vs[i].obj, vs[i].left,  f.K1, f.D1, rl, tl);
        cv::solvePnP (vs[i].obj, vs[i].right, f.K2, f.D2, rr, tr);
        const cv::Vec3d a = translation_of (rt (rl, tl));
        const cv::Vec3d b = translation_of (left_T_right * rt (rr, tr));
        const double gap = cv::norm (a - b) * 1000.0;
        sum += gap;
        mx = std::max (mx, gap);
    }
    *mean = sum / (double) vs.size ();
    *worst = mx;
}


struct HandEye {
    std::string name;
    cv::Matx44d X;
    double spread_mm;
    double spread_deg;
};

/*
 * flange_T_camera, plus the honest quality number: how much the implied board pose
 * moves between views. The board did not move, so any spread is calibration error --
 * and comparing it against the eye agreement says whether the residual is the camera's
 * or the robot's.
 */
std::vector<HandEye> solve_hand_eye (const std::vector<View> &vs, const Fit &f)
{
    std::vector<cv::Mat> Rg, tg, Rc, tc;
    for (size_t i = 0; i < vs.size (); i++) {
        cv::Matx33d Rb;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) Rb(r, c) = vs[i].base_T_flange(r, c);
        Rg.push_back (cv::Mat (Rb));
        tg.push_back (cv::Mat (cv::Vec3d (vs[i].base_T_flange(0,3),
                                          vs[i].base_T_flange(1,3),
                                          vs[i].base_T_flange(2,3))));
        cv::Matx33d Rcam;
        cv::Rodrigues (f.rvecs[i], Rcam);
        Rc.push_back (cv::Mat (Rcam));
        cv::Mat t;
        f.tvecs[i].convertTo (t, CV_64F);
        tc.push_back (t.reshape (1, 3));
    }

    const char *names[4] = {"TSAI", "PARK", "HORAUD", "DANIILIDIS"};
    const cv::HandEyeCalibrationMethod methods[4] = {
        cv::CALIB_HAND_EYE_TSAI, cv::CALIB_HAND_EYE_PARK,
        cv::CALIB_HAND_EYE_HORAUD, cv::CALIB_HAND_EYE_DANIILIDIS};

    std::vector<HandEye> out;
    for (int m = 0; m < 4; m++) {
        cv::Mat R, t;
        cv::calibrateHandEye (Rg, tg, Rc, tc, R, t, methods[m]);
        if (R.empty () || t.empty ())
            continue;
        HandEye he;
        he.name = names[m];
        he.X = cv::Matx44d::eye ();
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) he.X(r, c) = R.at<double> (r, c);
            he.X(r, 3) = t.at<double> (r);
        }
        /* Where each view says the board is. It never moved, so this should not move. */
        std::vector<cv::Matx44d> boards;
        cv::Vec3d mean (0, 0, 0);
        for (size_t i = 0; i < vs.size (); i++) {
            cv::Vec3d rv (f.rvecs[i].at<double> (0), f.rvecs[i].at<double> (1),
                          f.rvecs[i].at<double> (2));
            cv::Vec3d tv (f.tvecs[i].at<double> (0), f.tvecs[i].at<double> (1),
                          f.tvecs[i].at<double> (2));
            boards.push_back (vs[i].base_T_flange * he.X * rt (rv, tv));
            mean += translation_of (boards.back ());
        }
        if (boards.empty ())
            continue;
        mean *= 1.0 / (double) boards.size ();
        double spread = 0, ang = 0;
        cv::Matx33d ref;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) ref(r, c) = boards[0](r, c);
        for (size_t i = 0; i < boards.size (); i++) {
            spread = std::max (spread, cv::norm (translation_of (boards[i]) - mean) * 1000.0);
            cv::Matx33d Rb;
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) Rb(r, c) = boards[i](r, c);
            cv::Vec3d d;
            cv::Rodrigues (cv::Matx33d (Rb * ref.t ()), d);
            ang = std::max (ang, cv::norm (d) * 180.0 / CV_PI);
        }
        he.spread_mm = spread;
        he.spread_deg = ang;
        out.push_back (he);
    }
    return out;
}

}  // namespace

int
ag_calib_solve (const std::string &session_dir, const std::string &out_dir_in,
                const AgCalibSolveOpts &o)
{
    const std::string out_dir = out_dir_in.empty () ? session_dir + "/calib_result" : out_dir_in;
    const cv::Size size (o.eye_w, o.eye_h);

    if (o.verbose) printf ("detecting:\n");
    std::vector<View> views = load_views (session_dir, o);
    if (views.size () < 4) {
        fprintf (stderr, "only %zu usable views -- need more\n", views.size ());
        return -1;
    }
    if (o.verbose) printf ("\n%zu views\n\nintrinsics:\n", views.size ());

    Fit f;
    if (!run_fit (views, size, o, f)) {
        fprintf (stderr, "stereoCalibrate failed\n");
        return -1;
    }

    /*
     * Outlier pass. A view that does not conform to the ONE rigid baseline is invisible
     * to per-eye reprojection (each eye absorbs a relative rotation into its own board
     * pose) but drags the joint solve for everybody. Threshold and construction follow
     * the agrippa-stereocam calibration notebook: max(2 x median, 1.0 px). Never drop
     * below min_views, and always show the table.
     */
    size_t dropped = 0;
    if (!o.keep_outliers) {
        std::vector<double> rv = per_view_stereo_rms (views, f);
        std::vector<double> sorted = rv;
        std::sort (sorted.begin (), sorted.end ());
        const double med = sorted[sorted.size () / 2];
        const double thr = std::max (2.0 * med, 1.0);
        std::vector<View> kept;
        if (o.verbose)
            printf ("\nper-view stereo conformity (median %.3f px, threshold %.3f px):\n",
                    med, thr);
        for (size_t i = 0; i < rv.size (); i++) {
            const bool bad = rv[i] > thr;
            if (o.verbose)
                printf ("  view %2d  %7.3f px%s\n", views[i].index, rv[i],
                        bad ? "   ** dropped **" : "");
            if (!bad) kept.push_back (views[i]);
        }
        dropped = views.size () - kept.size ();
        if (dropped && kept.size () >= (size_t) o.min_views) {
            if (o.verbose)
                printf ("  dropping %zu of %zu views, refitting on %zu\n",
                        dropped, views.size (), kept.size ());
            views.swap (kept);
            if (!run_fit (views, size, o, f))
                return -1;
        } else if (dropped) {
            if (o.verbose)
                printf ("  %zu view(s) over threshold but only %zu would remain (min %d)"
                        " -- keeping all\n", dropped, kept.size (), o.min_views);
            dropped = 0;
        } else if (o.verbose) {
            printf ("  no outliers\n");
        }
    }

    /*
     * alpha is PINNED at 0 by default: the sweep deliberately leaves the
     * gripper-occluded band under-constrained, and letting alpha auto-fit lets that
     * extrapolated distortion drive the global ROI.
     */
    cv::Mat R1, R2, P1, P2, Q;
    cv::stereoRectify (f.K1, f.D1, f.K2, f.D2, size, f.R, f.T, R1, R2, P1, P2, Q,
                       cv::CALIB_ZERO_DISPARITY, o.alpha);

    /* Mean |y_left - y_right| after rectification -- what a stereo matcher feels. */
    std::vector<double> ep;
    for (size_t i = 0; i < views.size (); i++) {
        std::vector<cv::Point2f> ul, ur;
        cv::undistortPoints (views[i].left,  ul, f.K1, f.D1, R1, P1);
        cv::undistortPoints (views[i].right, ur, f.K2, f.D2, R2, P2);
        for (size_t j = 0; j < ul.size (); j++)
            ep.push_back (std::fabs (ul[j].y - ur[j].y));
    }
    std::sort (ep.begin (), ep.end ());
    double ep_mean = 0;
    for (size_t i = 0; i < ep.size (); i++) ep_mean += ep[i];
    ep_mean /= (double) ep.size ();
    const double ep_p95 = ep[(size_t) (0.95 * (double) (ep.size () - 1))];

    double eye_mean = 0, eye_worst = 0;
    eye_agreement (views, f, &eye_mean, &eye_worst);
    const double baseline_mm = cv::norm (f.T) * 1000.0;

    if (o.verbose) {
        printf ("\nstereo:\n  RMS %.3f px, baseline %.2f mm\n", f.rms_s, baseline_mm);
        printf ("  epipolar mean %.3f px (p95 %.3f)\n", ep_mean, ep_p95);
        printf ("  eye agreement mean %.2f mm (worst %.2f)\n", eye_mean, eye_worst);
    }

    /* --- write the archive ------------------------------------------------------- */
    mkdir (out_dir.c_str (), 0755);
    const cv::Mat *arrays[8] = {&f.K1, &f.K2, &f.D1, &f.D2, &R1, &R2, &P1, &P2};
    for (int i = 0; i < 8; i++) {
        cv::Mat a;
        arrays[i]->convertTo (a, CV_64F);
        if (!a.isContinuous ()) a = a.clone ();
        int shape[2] = {a.rows, a.cols};
        char path[512];
        snprintf (path, sizeof path, "%s/%s.npy", out_dir.c_str (), ARCHIVE_FILES[i]);
        if (ag_npy_save (path, a.ptr<double> (), 2, shape) != 0)
            return -1;
    }

    /*
     * Hand-eye. The board never moved, so the spread of where each view PUTS it is the
     * truth test; pick the method that minimises it. Compare against the eye agreement:
     * a spread far above it means the residual is in the robot's reported flange poses,
     * not in this calibration, and no amount of extra views will touch it.
     */
    std::vector<HandEye> he = solve_hand_eye (views, f);
    size_t best = 0;
    for (size_t i = 1; i < he.size (); i++)
        if (he[i].spread_mm < he[best].spread_mm) best = i;
    if (o.verbose) {
        printf ("\nhand-eye (board spread is the truth test: the board never moved):\n");
        for (size_t i = 0; i < he.size (); i++)
            printf ("  %-11s t [%.1f, %.1f, %.1f] mm  board spread %6.2f mm / %5.2f deg%s\n",
                    he[i].name.c_str (), he[i].X(0,3) * 1000, he[i].X(1,3) * 1000,
                    he[i].X(2,3) * 1000, he[i].spread_mm, he[i].spread_deg,
                    i == best ? "   <- best" : "");
        if (!he.empty () && he[best].spread_mm > 5.0 * eye_mean)
            printf ("  NOTE: spread %.2f mm is far above the %.2f mm the cameras disagree "
                    "by -- the residual is in the robot's reported flange poses, not in "
                    "the calibration.\n", he[best].spread_mm, eye_mean);
    }

    /*
     * Key names here are a CONTRACT, not a style choice: publish.py reads hand_eye_best
     * and hand_eye[best].flange_T_camera, and the backend store reads rms_stereo_px,
     * mean_epipolar_error_px, baseline_cm (x10 -> baselineMm) and eye_agreement_mm.
     * Renaming any of them makes the entry import with null metrics rather than fail.
     */
    cJSON *meta = cJSON_CreateObject ();

    /* The controller's clock ran ~84 days behind until it was fixed, and the store
     * needs a date that is actually right; publish.py prefers this field over a mtime,
     * so an unsynced box would date every solve wrongly rather than obviously. */
    time_t now = time (NULL);
    struct tm g;
    char stamp[32] = "";
    if (gmtime_r (&now, &g))
        strftime (stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &g);
    cJSON_AddStringToObject (meta, "solved_at", stamp);

    cJSON *isz = cJSON_AddArrayToObject (meta, "image_size");
    cJSON_AddItemToArray (isz, cJSON_CreateNumber (o.eye_w));
    cJSON_AddItemToArray (isz, cJSON_CreateNumber (o.eye_h));
    cJSON_AddNumberToObject (meta, "num_pairs_used", (double) views.size ());
    cJSON_AddNumberToObject (meta, "rms_stereo_px", f.rms_s);
    cJSON_AddNumberToObject (meta, "mean_epipolar_error_px", ep_mean);
    cJSON_AddNumberToObject (meta, "epipolar_p95_px", ep_p95);
    cJSON_AddNumberToObject (meta, "rms_left_px", f.rms_l);
    cJSON_AddNumberToObject (meta, "rms_right_px", f.rms_r);
    cJSON_AddNumberToObject (meta, "baseline_cm", baseline_mm / 10.0);
    cJSON_AddNumberToObject (meta, "focal_length_px", P1.at<double> (0, 0));
    cJSON_AddNumberToObject (meta, "rectify_alpha", o.alpha);
    cJSON_AddNumberToObject (meta, "views_dropped", (double) dropped);

    cJSON *hej = cJSON_AddObjectToObject (meta, "hand_eye");
    for (size_t i = 0; i < he.size (); i++) {
        cJSON *e = cJSON_AddObjectToObject (hej, he[i].name.c_str ());
        cJSON *ftc = cJSON_AddArrayToObject (e, "flange_T_camera");
        cv::Matx33d R;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) R(r, c) = he[i].X(r, c);
        cv::Vec3d rv;
        cv::Rodrigues (R, rv);
        for (int k = 0; k < 3; k++) cJSON_AddItemToArray (ftc, cJSON_CreateNumber (he[i].X(k, 3)));
        for (int k = 0; k < 3; k++) cJSON_AddItemToArray (ftc, cJSON_CreateNumber (rv[k]));
        cJSON_AddNumberToObject (e, "board_spread_mm", he[i].spread_mm);
        cJSON_AddNumberToObject (e, "board_spread_deg", he[i].spread_deg);
    }
    if (!he.empty ())
        cJSON_AddStringToObject (meta, "hand_eye_best", he[best].name.c_str ());

    cJSON *ea = cJSON_AddObjectToObject (meta, "eye_agreement_mm");
    cJSON_AddNumberToObject (ea, "mean", eye_mean);
    cJSON_AddNumberToObject (ea, "max", eye_worst);

    cJSON *bd = cJSON_AddObjectToObject (meta, "board");
    cJSON_AddNumberToObject (bd, "columns", o.spec.columns);
    cJSON_AddNumberToObject (bd, "rows", o.spec.rows);
    cJSON_AddNumberToObject (bd, "tag_mm", o.spec.tag_mm);
    cJSON_AddNumberToObject (bd, "gap_mm", o.spec.gap_mm);
    cJSON_AddStringToObject (bd, "dictionary", "tag36h11");
    cJSON_AddNumberToObject (bd, "border_cells", 2);

    cJSON_AddStringToObject (meta, "solver", "ag-cam-tools calib-solve");
    /* Which OpenCV solved this. The same session solved on a workstation and on the
     * controller does NOT give bit-identical numbers -- the tag set comes out the same
     * (70/70 on every eye of sweep_am either way, same single 69), but cornerSubPix and
     * the calibrateCamera/stereoCalibrate optimisers changed between releases, and
     * measured across 4.5.4 and 4.13 that moved the baseline by 0.22 mm (0.55%) and the
     * stereo RMS by 0.006 px. Small, but it is a real physical quantity, so record what
     * produced it rather than leaving the discrepancy to be rediscovered. */
    cJSON_AddStringToObject (meta, "opencv_version", CV_VERSION);
    cJSON_AddBoolToObject (meta, "intrinsics_refined_in_stereo", o.refine_intrinsics);

    char *txt = cJSON_Print (meta);
    char mpath[512];
    snprintf (mpath, sizeof mpath, "%s/calibration_meta.json", out_dir.c_str ());
    FILE *mf = fopen (mpath, "w");
    int ok = mf && fputs (txt, mf) >= 0;
    if (mf) ok = (fclose (mf) == 0) && ok;
    free (txt);
    cJSON_Delete (meta);
    if (!ok) {
        fprintf (stderr, "cannot write %s\n", mpath);
        return -1;
    }
    if (o.verbose) printf ("\nwrote %s (8 .npy + calibration_meta.json)\n", out_dir.c_str ());
    return 0;
}

#endif /* HAVE_OPENCV */
