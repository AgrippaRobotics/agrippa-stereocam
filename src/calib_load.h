/*
 * calib_load.h — unified calibration loader
 *
 * Loads stereo rectification remap tables from either a local filesystem
 * calibration session or a numbered on-camera slot.
 */

#ifndef AG_CALIB_LOAD_H
#define AG_CALIB_LOAD_H

#include "remap.h"
#include "common.h"   /* AgCalibMeta */

/*
 * Calibration source discriminant.
 * Exactly one of local_path / slot should be active.
 */
typedef struct {
    const char *local_path;   /* filesystem session path, or NULL */
    int         slot;         /* 0-2 if slot-based, or -1 if unused */
} AgCalibSource;

/*
 * Load rectification remap tables from either a local filesystem
 * session path or a numbered on-camera slot.
 *
 * - device: the open ArvDevice (needed for slot-based loading).
 *           May be NULL if source->slot < 0.
 * - source: specifies which calibration to load.
 * - out_left, out_right: newly-allocated AgRemapTable pointers
 *           (caller must ag_remap_table_free).
 * - out_meta: optional; filled with calibration metadata if non-NULL.
 *
 * Returns 0 on success, -1 on error (prints its own diagnostics).
 */
int ag_calib_load (ArvDevice *device,
                   const AgCalibSource *source,
                   AgRemapTable **out_left,
                   AgRemapTable **out_right,
                   AgCalibMeta *out_meta);

/*
 * Load only calibration metadata from a local session path.
 * Reads <session_path>/calib_result/calibration_meta.json.
 *
 * Returns 0 on success, -1 on error.
 */
int ag_calib_load_meta (const char *session_path, AgCalibMeta *out);

#endif /* AG_CALIB_LOAD_H */
