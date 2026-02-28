/*
 * focus.h — Focus metrics for lens adjustment (pure C99)
 *
 * Provides multiple sharpness metrics (Laplacian, Tenengrad, Brenner)
 * behind a common dispatch interface.  All metrics operate on 8-bit
 * grayscale images and return higher values for sharper focus.
 */

#ifndef AG_FOCUS_H
#define AG_FOCUS_H

#include <stdint.h>

/* Available focus metrics. */
typedef enum {
    AG_FOCUS_METRIC_LAPLACIAN = 0,   /* Variance of 3x3 Laplacian */
    AG_FOCUS_METRIC_TENENGRAD,       /* Sobel gradient energy      */
    AG_FOCUS_METRIC_BRENNER,         /* Brenner gradient            */
    AG_FOCUS_METRIC_COUNT
} AgFocusMetric;

/*
 * Parse a metric name string ("laplacian", "tenengrad", "brenner").
 * Returns the metric enum value, or -1 on unrecognized input.
 */
int ag_focus_metric_from_string (const char *name);

/*
 * Return the display name for a metric enum value.
 */
const char *ag_focus_metric_name (AgFocusMetric metric);

/*
 * Compute a focus sharpness score using the specified metric.
 *
 * The ROI is clamped inward to provide margin for the kernel.
 * If the resulting region is too small, returns 0.0.
 */
double ag_focus_score (AgFocusMetric metric,
                       const uint8_t *image, int width, int height,
                       int roi_x, int roi_y, int roi_w, int roi_h);

/*
 * Legacy API — equivalent to ag_focus_score(AG_FOCUS_METRIC_LAPLACIAN, ...).
 */
double compute_focus_score (const uint8_t *image, int width, int height,
                            int roi_x, int roi_y, int roi_w, int roi_h);

#endif /* AG_FOCUS_H */
