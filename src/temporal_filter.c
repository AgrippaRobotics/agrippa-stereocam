/*
 * temporal_filter.c — temporal median filter for disparity maps
 *
 * Ring buffer of N disparity frames with per-pixel temporal median.
 * Includes scene-change detection that resets the buffer when a large
 * fraction of pixels change abruptly between consecutive frames.
 */

#include "temporal_filter.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define INVALID_DISP  (-16)

/*
 * Scene-change detection thresholds.
 * If more than SCENE_CHANGE_FRAC of pixels differ by more than
 * SCENE_CHANGE_THRESH (in Q4.4 units), the buffer is reset.
 */
#define SCENE_CHANGE_THRESH   (5 * 16)    /* 5 pixels of disparity */
#define SCENE_CHANGE_FRAC     0.30        /* 30% of valid pixels */

/* ------------------------------------------------------------------ */
/*  Context                                                            */
/* ------------------------------------------------------------------ */

struct AgTemporalFilter {
    uint32_t  width;
    uint32_t  height;
    int       depth;          /* ring buffer capacity (N) */
    int       count;          /* frames pushed so far (0..depth) */
    int       head;           /* next write position in ring */
    int16_t **frames;         /* array of `depth` disparity buffers */
};

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static int
cmp_int16 (const void *a, const void *b)
{
    int16_t va = *(const int16_t *) a;
    int16_t vb = *(const int16_t *) b;
    return (va > vb) - (va < vb);
}

/*
 * Detect scene change between the most recent frame in the buffer
 * and the incoming frame.  Returns non-zero if the buffer should
 * be reset.
 */
static int
detect_scene_change (const AgTemporalFilter *ctx, const int16_t *incoming)
{
    if (ctx->count == 0)
        return 0;

    /* Previous frame is at (head - 1 + depth) % depth. */
    int prev_idx = (ctx->head - 1 + ctx->depth) % ctx->depth;
    const int16_t *prev = ctx->frames[prev_idx];

    size_t npixels = (size_t) ctx->width * ctx->height;
    size_t valid = 0;
    size_t changed = 0;

    for (size_t i = 0; i < npixels; i++) {
        if (prev[i] <= INVALID_DISP || incoming[i] <= INVALID_DISP)
            continue;
        valid++;
        int diff = (int) incoming[i] - (int) prev[i];
        if (diff < 0) diff = -diff;
        if (diff > SCENE_CHANGE_THRESH)
            changed++;
    }

    if (valid < 100)
        return 0;   /* too few valid pixels to judge */

    return ((double) changed / (double) valid) > SCENE_CHANGE_FRAC;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

AgTemporalFilter *
ag_temporal_filter_create (uint32_t width, uint32_t height, int depth)
{
    if (depth < 2 || width == 0 || height == 0)
        return NULL;

    AgTemporalFilter *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        return NULL;

    ctx->width  = width;
    ctx->height = height;
    ctx->depth  = depth;
    ctx->count  = 0;
    ctx->head   = 0;

    size_t frame_bytes = (size_t) width * height * sizeof (int16_t);

    ctx->frames = calloc ((size_t) depth, sizeof (int16_t *));
    if (!ctx->frames) {
        free (ctx);
        return NULL;
    }

    for (int i = 0; i < depth; i++) {
        ctx->frames[i] = malloc (frame_bytes);
        if (!ctx->frames[i]) {
            for (int j = 0; j < i; j++)
                free (ctx->frames[j]);
            free (ctx->frames);
            free (ctx);
            return NULL;
        }
    }

    return ctx;
}

int
ag_temporal_filter_push (AgTemporalFilter *ctx,
                          const int16_t *disparity_in,
                          int16_t *disparity_out)
{
    if (!ctx || !disparity_in || !disparity_out)
        return -1;

    size_t npixels = (size_t) ctx->width * ctx->height;

    /* Scene-change detection: reset if the scene shifted abruptly. */
    if (detect_scene_change (ctx, disparity_in)) {
        ctx->count = 0;
        ctx->head  = 0;
    }

    /* Store incoming frame in the ring buffer. */
    memcpy (ctx->frames[ctx->head], disparity_in, npixels * sizeof (int16_t));
    ctx->head = (ctx->head + 1) % ctx->depth;
    if (ctx->count < ctx->depth)
        ctx->count++;

    /* If only one frame, just copy through. */
    if (ctx->count == 1) {
        if (disparity_out != disparity_in)
            memcpy (disparity_out, disparity_in, npixels * sizeof (int16_t));
        return 0;
    }

    /* Compute per-pixel temporal median. */
    int n = ctx->count;
    int16_t *tmp = malloc ((size_t) n * sizeof (int16_t));
    if (!tmp)
        return -1;

    for (size_t px = 0; px < npixels; px++) {
        int valid = 0;

        for (int f = 0; f < n; f++) {
            int16_t v = ctx->frames[f][px];
            if (v > INVALID_DISP)
                tmp[valid++] = v;
        }

        if (valid == 0) {
            disparity_out[px] = INVALID_DISP;
            continue;
        }

        if (valid == 1) {
            disparity_out[px] = tmp[0];
            continue;
        }

        /* Sort and take the median. */
        qsort (tmp, (size_t) valid, sizeof (int16_t), cmp_int16);

        if (valid & 1) {
            disparity_out[px] = tmp[valid / 2];
        } else {
            /* Even count: average the two middle values. */
            disparity_out[px] = (int16_t) (((int) tmp[valid / 2 - 1] +
                                             (int) tmp[valid / 2]) / 2);
        }
    }

    free (tmp);
    return 0;
}

void
ag_temporal_filter_reset (AgTemporalFilter *ctx)
{
    if (!ctx)
        return;
    ctx->count = 0;
    ctx->head  = 0;
}

void
ag_temporal_filter_destroy (AgTemporalFilter *ctx)
{
    if (!ctx)
        return;
    for (int i = 0; i < ctx->depth; i++)
        free (ctx->frames[i]);
    free (ctx->frames);
    free (ctx);
}
