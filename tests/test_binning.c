/*
 * test_binning.c — unit tests proving the binning/debayer oversight
 *
 * Uses synthetic Bayer patterns to demonstrate that software_bin_2x2()
 * destroys the Bayer CFA structure, making subsequent debayering invalid.
 *
 * No camera hardware is required.
 *
 * Build:  make test
 * Run:    bin/test_binning [-s suite] [-t test]
 */

#include "../vendor/greatest.h"
#include "imgproc.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/*
 * Fill a WxH buffer with a synthetic BayerRG8 pattern.
 *
 * CFA layout (same as debayer_rg8_to_rgb assumes):
 *   (y%2==0, x%2==0) = R
 *   (y%2==0, x%2==1) = G  (on R row)
 *   (y%2==1, x%2==0) = G  (on B row)
 *   (y%2==1, x%2==1) = B
 */
static void
fill_bayer_rg8 (guint8 *buf, guint w, guint h,
                guint8 r, guint8 g, guint8 b)
{
    for (guint y = 0; y < h; y++) {
        for (guint x = 0; x < w; x++) {
            int ye = ((y & 1) == 0);
            int xe = ((x & 1) == 0);
            guint8 val;
            if (ye && xe)        val = r;   /* R */
            else if (ye && !xe)  val = g;   /* G on R row */
            else if (!ye && xe)  val = g;   /* G on B row */
            else                 val = b;   /* B */
            buf[(size_t) y * w + x] = val;
        }
    }
}

/*
 * Downsample an RGB image by 2x in each dimension (average 2x2 blocks).
 * This is the CORRECT way to halve resolution of a colour image:
 * debayer first, THEN downsample.
 */
static void
downsample_rgb_2x (const guint8 *src, guint src_w, guint src_h,
                   guint8 *dst, guint dst_w, guint dst_h)
{
    (void) src_h;
    for (guint y = 0; y < dst_h; y++) {
        for (guint x = 0; x < dst_w; x++) {
            for (int c = 0; c < 3; c++) {
                guint sy = 2 * y, sx = 2 * x;
                int v = src[((size_t) sy       * src_w + sx)     * 3 + c]
                      + src[((size_t) sy       * src_w + sx + 1) * 3 + c]
                      + src[((size_t)(sy + 1)  * src_w + sx)     * 3 + c]
                      + src[((size_t)(sy + 1)  * src_w + sx + 1) * 3 + c];
                dst[((size_t) y * dst_w + x) * 3 + c] = (guint8)(v / 4);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Suite 1: bayer_baseline — debayer works on valid Bayer             */
/* ------------------------------------------------------------------ */

TEST debayer_pure_red (void)
{
    enum { W = 8, H = 8 };
    guint8 bayer[W * H];
    guint8 rgb[W * H * 3];

    fill_bayer_rg8 (bayer, W, H, 200, 0, 0);
    debayer_rg8_to_rgb (bayer, rgb, W, H);

    /* Check interior pixels (avoid edges where CLAMP affects interpolation). */
    for (guint y = 2; y < H - 2; y++) {
        for (guint x = 2; x < W - 2; x++) {
            size_t idx = ((size_t) y * W + x) * 3;
            guint8 r = rgb[idx + 0];
            guint8 b = rgb[idx + 2];
            /* Red should dominate; blue should be near zero. */
            ASSERT (r > 100);
            ASSERT (b < 50);
        }
    }
    PASS ();
}

TEST debayer_uniform_white (void)
{
    enum { W = 8, H = 8 };
    guint8 bayer[W * H];
    guint8 rgb[W * H * 3];

    fill_bayer_rg8 (bayer, W, H, 200, 200, 200);
    debayer_rg8_to_rgb (bayer, rgb, W, H);

    /* Interior pixels should be roughly (200, 200, 200). */
    for (guint y = 2; y < H - 2; y++) {
        for (guint x = 2; x < W - 2; x++) {
            size_t idx = ((size_t) y * W + x) * 3;
            ASSERT_EQ (200, rgb[idx + 0]);
            ASSERT_EQ (200, rgb[idx + 1]);
            ASSERT_EQ (200, rgb[idx + 2]);
        }
    }
    PASS ();
}

SUITE (bayer_baseline)
{
    RUN_TEST (debayer_pure_red);
    RUN_TEST (debayer_uniform_white);
}

/* ------------------------------------------------------------------ */
/*  Suite 2: software_bin_destroys_bayer — structural proof            */
/* ------------------------------------------------------------------ */

TEST bin2x2_mixes_channels (void)
{
    /* Pure-red scene: R=200, G=0, B=0.
     * Each 2x2 Bayer quad has one R(200), two G(0), one B(0).
     * Average = (200 + 0 + 0 + 0) / 4 = 50.
     */
    enum { SRC_W = 16, SRC_H = 16, DST_W = 8, DST_H = 8 };
    guint8 src[SRC_W * SRC_H];
    guint8 dst[DST_W * DST_H];

    fill_bayer_rg8 (src, SRC_W, SRC_H, 200, 0, 0);
    software_bin_2x2 (src, SRC_W, SRC_H, dst, DST_W, DST_H);

    for (int i = 0; i < DST_W * DST_H; i++)
        ASSERT_EQ (50, dst[i]);

    PASS ();
}

TEST bin2x2_green_scene (void)
{
    /* Pure-green scene: R=0, G=200, B=0.
     * Each 2x2 quad: R(0), G(200), G(200), B(0).
     * Average = (0 + 200 + 200 + 0) / 4 = 100.
     */
    enum { SRC_W = 16, SRC_H = 16, DST_W = 8, DST_H = 8 };
    guint8 src[SRC_W * SRC_H];
    guint8 dst[DST_W * DST_H];

    fill_bayer_rg8 (src, SRC_W, SRC_H, 0, 200, 0);
    software_bin_2x2 (src, SRC_W, SRC_H, dst, DST_W, DST_H);

    for (int i = 0; i < DST_W * DST_H; i++)
        ASSERT_EQ (100, dst[i]);

    PASS ();
}

TEST bin2x2_output_is_uniform (void)
{
    /* Scene with distinct channels: R=200, G=100, B=50.
     * Each 2x2 quad: R(200), G(100), G(100), B(50).
     * Average = (200 + 100 + 100 + 50) / 4 = 112.
     *
     * KEY ASSERTION: every output pixel is identical.
     * In a valid Bayer pattern, even/even positions would differ from
     * even/odd positions.  After binning, they don't.
     */
    enum { SRC_W = 16, SRC_H = 16, DST_W = 8, DST_H = 8 };
    guint8 src[SRC_W * SRC_H];
    guint8 dst[DST_W * DST_H];

    fill_bayer_rg8 (src, SRC_W, SRC_H, 200, 100, 50);
    software_bin_2x2 (src, SRC_W, SRC_H, dst, DST_W, DST_H);

    /* All pixels should be 112. */
    for (int i = 0; i < DST_W * DST_H; i++)
        ASSERT_EQ (112, dst[i]);

    /* Adjacent pixels that would be different Bayer sites are identical. */
    ASSERT_EQ (dst[0], dst[1]);  /* "R" position vs "G" position */
    ASSERT_EQ (dst[0], dst[DST_W]);  /* "R" position vs next-row "G" */
    ASSERT_EQ (dst[0], dst[DST_W + 1]);  /* "R" position vs "B" position */

    PASS ();
}

SUITE (software_bin_destroys_bayer)
{
    RUN_TEST (bin2x2_mixes_channels);
    RUN_TEST (bin2x2_green_scene);
    RUN_TEST (bin2x2_output_is_uniform);
}

/* ------------------------------------------------------------------ */
/*  Suite 3: debayer_after_bin_is_wrong — downstream consequence       */
/* ------------------------------------------------------------------ */

TEST correct_vs_broken_pipeline (void)
{
    /* Scene: R=200, G=50, B=20.
     *
     * CORRECT pipeline (debayer first, then downsample):
     *   16x16 Bayer -> debayer -> 16x16 RGB -> downsample 2x -> 8x8 RGB
     *   Interior pixels should be approximately (200, 50, 20).
     *
     * BROKEN pipeline (current code: bin first, then debayer):
     *   16x16 Bayer -> bin 2x2 -> 8x8 uniform -> debayer -> 8x8 RGB
     *   Each binned pixel = (200 + 50 + 50 + 20) / 4 = 80.
     *   Debayering a uniform image produces (80, 80, 80).
     */
    enum { SRC_W = 16, SRC_H = 16, DST_W = 8, DST_H = 8 };

    guint8 bayer[SRC_W * SRC_H];
    fill_bayer_rg8 (bayer, SRC_W, SRC_H, 200, 50, 20);

    /* -- Correct pipeline -- */
    guint8 rgb_full[SRC_W * SRC_H * 3];
    debayer_rg8_to_rgb (bayer, rgb_full, SRC_W, SRC_H);

    guint8 rgb_correct[DST_W * DST_H * 3];
    downsample_rgb_2x (rgb_full, SRC_W, SRC_H, rgb_correct, DST_W, DST_H);

    /* -- Broken pipeline -- */
    guint8 binned[DST_W * DST_H];
    software_bin_2x2 (bayer, SRC_W, SRC_H, binned, DST_W, DST_H);

    guint8 rgb_broken[DST_W * DST_H * 3];
    debayer_rg8_to_rgb (binned, rgb_broken, DST_W, DST_H);

    /* Compare at interior pixels. */
    int total_r_diff = 0, total_g_diff = 0, total_b_diff = 0;
    int n = 0;
    for (guint y = 2; y < DST_H - 2; y++) {
        for (guint x = 2; x < DST_W - 2; x++) {
            size_t idx = ((size_t) y * DST_W + x) * 3;
            total_r_diff += abs ((int) rgb_correct[idx + 0] - (int) rgb_broken[idx + 0]);
            total_g_diff += abs ((int) rgb_correct[idx + 1] - (int) rgb_broken[idx + 1]);
            total_b_diff += abs ((int) rgb_correct[idx + 2] - (int) rgb_broken[idx + 2]);
            n++;
        }
    }

    /* The broken pipeline should produce substantially different colours.
     * Correct R channel is ~200, broken is ~80: avg diff should be >50. */
    int avg_r_diff = total_r_diff / n;
    int avg_b_diff = total_b_diff / n;
    ASSERT (avg_r_diff > 50);
    /* B channel: correct is ~20, broken is ~80, so diff should be >30. */
    ASSERT (avg_b_diff > 30);
    (void) total_g_diff;

    PASS ();
}

TEST binned_debayer_equals_gray (void)
{
    /* After bin 2x2, the data is uniform (no Bayer variation).
     * Debayering should produce R == G == B at every interior pixel,
     * proving that debayer is a no-op — it just converts gray to gray. */
    enum { SRC_W = 16, SRC_H = 16, DST_W = 8, DST_H = 8 };

    guint8 bayer[SRC_W * SRC_H];
    fill_bayer_rg8 (bayer, SRC_W, SRC_H, 200, 100, 50);

    guint8 binned[DST_W * DST_H];
    software_bin_2x2 (bayer, SRC_W, SRC_H, binned, DST_W, DST_H);

    guint8 rgb[DST_W * DST_H * 3];
    debayer_rg8_to_rgb (binned, rgb, DST_W, DST_H);

    /* Interior pixels: R == G == B (all equal to 112). */
    for (guint y = 2; y < DST_H - 2; y++) {
        for (guint x = 2; x < DST_W - 2; x++) {
            size_t idx = ((size_t) y * DST_W + x) * 3;
            ASSERT_EQ (rgb[idx + 0], rgb[idx + 1]);  /* R == G */
            ASSERT_EQ (rgb[idx + 1], rgb[idx + 2]);  /* G == B */
            ASSERT_EQ (112, rgb[idx + 0]);            /* value is correct */
        }
    }

    PASS ();
}

TEST disparity_path_roundtrip (void)
{
    /* The depth-preview disparity path currently does:
     *   bin -> debayer -> rgb_to_gray
     *
     * The proposed fix does:
     *   bin (use directly as gray)
     *
     * Both should produce the same grayscale values, proving the
     * debayer->gray roundtrip is wasteful. */
    enum { SRC_W = 16, SRC_H = 16, DST_W = 8, DST_H = 8 };

    guint8 bayer[SRC_W * SRC_H];
    fill_bayer_rg8 (bayer, SRC_W, SRC_H, 200, 100, 50);

    guint8 binned[DST_W * DST_H];
    software_bin_2x2 (bayer, SRC_W, SRC_H, binned, DST_W, DST_H);

    /* Path A (current code): bin -> debayer -> rgb_to_gray. */
    guint8 rgb[DST_W * DST_H * 3];
    debayer_rg8_to_rgb (binned, rgb, DST_W, DST_H);

    guint8 gray_roundtrip[DST_W * DST_H];
    rgb_to_gray (rgb, gray_roundtrip, DST_W * DST_H);

    /* Path B (proposed fix): use binned data directly as gray. */

    /* At interior pixels, both paths should produce the same value
     * (or very close, since rgb_to_gray uses BT.601 weighting on
     * what is effectively R=G=B=112, so output = 112). */
    for (guint y = 2; y < DST_H - 2; y++) {
        for (guint x = 2; x < DST_W - 2; x++) {
            size_t idx = (size_t) y * DST_W + x;
            int diff = abs ((int) gray_roundtrip[idx] - (int) binned[idx]);
            /* Allow tolerance of 1 for fixed-point rounding. */
            ASSERT (diff <= 1);
        }
    }

    PASS ();
}

SUITE (debayer_after_bin_is_wrong)
{
    RUN_TEST (correct_vs_broken_pipeline);
    RUN_TEST (binned_debayer_equals_gray);
    RUN_TEST (disparity_path_roundtrip);
}

/* ------------------------------------------------------------------ */
/*  Suite 4: deinterleave_then_bin — end-to-end mini-pipeline          */
/* ------------------------------------------------------------------ */

/*
 * Build a synthetic DualBayerRG8 interleaved frame.
 *
 * In DualBayerRG8, even columns are left eye, odd columns are right eye.
 * Full width = 2 * sub_w.  Each eye's Bayer pattern is independently
 * valid after deinterleaving.
 */
static void
fill_dual_bayer (guint8 *buf, guint full_w, guint h,
                 guint8 lr, guint8 lg, guint8 lb,
                 guint8 rr, guint8 rg, guint8 rb)
{
    guint sub_w = full_w / 2;
    guint8 *left_tmp  = g_malloc (sub_w * h);
    guint8 *right_tmp = g_malloc (sub_w * h);

    fill_bayer_rg8 (left_tmp,  sub_w, h, lr, lg, lb);
    fill_bayer_rg8 (right_tmp, sub_w, h, rr, rg, rb);

    /* Interleave: even columns from left, odd columns from right. */
    for (guint y = 0; y < h; y++) {
        for (guint x = 0; x < sub_w; x++) {
            buf[(size_t) y * full_w + 2 * x]     = left_tmp [(size_t) y * sub_w + x];
            buf[(size_t) y * full_w + 2 * x + 1] = right_tmp[(size_t) y * sub_w + x];
        }
    }

    g_free (left_tmp);
    g_free (right_tmp);
}

TEST dual_bayer_pipeline_loses_color (void)
{
    /* Left eye: pure red scene (R=200, G=0, B=0).
     * Right eye: pure blue scene (R=0, G=0, B=200).
     *
     * After the broken pipeline (deinterleave -> bin -> debayer):
     *   Left:  bin averages RGGB = (200+0+0+0)/4 = 50 everywhere
     *   Right: bin averages RGGB = (0+0+0+200)/4 = 50 everywhere
     *
     * Both eyes produce IDENTICAL output — the red vs blue
     * distinction is completely lost. */
    enum { FULL_W = 16, H = 8, SUB_W = 8, BIN_W = 4, BIN_H = 4 };

    guint8 interleaved[FULL_W * H];
    fill_dual_bayer (interleaved, FULL_W, H,
                     200, 0, 0,     /* left: red */
                     0, 0, 200);    /* right: blue */

    guint8 left[SUB_W * H], right[SUB_W * H];
    deinterleave_dual_bayer (interleaved, FULL_W, H, left, right);

    guint8 left_bin[BIN_W * BIN_H], right_bin[BIN_W * BIN_H];
    software_bin_2x2 (left,  SUB_W, H, left_bin,  BIN_W, BIN_H);
    software_bin_2x2 (right, SUB_W, H, right_bin, BIN_W, BIN_H);

    /* Both binned outputs should be 50 everywhere. */
    for (int i = 0; i < BIN_W * BIN_H; i++) {
        ASSERT_EQ (50, left_bin[i]);
        ASSERT_EQ (50, right_bin[i]);
    }

    /* After debayering, both should produce identical RGB. */
    guint8 rgb_left[BIN_W * BIN_H * 3];
    guint8 rgb_right[BIN_W * BIN_H * 3];
    debayer_rg8_to_rgb (left_bin,  rgb_left,  BIN_W, BIN_H);
    debayer_rg8_to_rgb (right_bin, rgb_right, BIN_W, BIN_H);

    /* The red-vs-blue distinction is lost: identical output. */
    ASSERT_MEM_EQ (rgb_left, rgb_right, BIN_W * BIN_H * 3);

    PASS ();
}

SUITE (deinterleave_then_bin)
{
    RUN_TEST (dual_bayer_pipeline_loses_color);
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

GREATEST_MAIN_DEFS ();

int
main (int argc, char **argv)
{
    GREATEST_MAIN_BEGIN ();

    RUN_SUITE (bayer_baseline);
    RUN_SUITE (software_bin_destroys_bayer);
    RUN_SUITE (debayer_after_bin_is_wrong);
    RUN_SUITE (deinterleave_then_bin);

    GREATEST_MAIN_END ();
}
