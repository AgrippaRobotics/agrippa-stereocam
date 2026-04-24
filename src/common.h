/*
 * common.h — shared declarations for ag-cam-tools
 */

#ifndef AG_COMMON_H
#define AG_COMMON_H

#include <arv.h>
#include <glib.h>

#include "imgproc.h"

/* Calibration metadata (shared by calib_archive, depth-preview, etc.). */
typedef struct {
    int    min_disparity;
    int    num_disparities;
    double focal_length_px;
    double baseline_cm;
} AgCalibMeta;

/* Default (fallback) sensor geometry for the PDH016S (DualBayerRG8 dual-eye).
 * Used only if the camera's WidthMax/HeightMax cannot be queried.  Real
 * geometry is taken from the device on every open. */
#define AG_SENSOR_WIDTH   2880
#define AG_SENSOR_HEIGHT  1080

/* Acquisition mode for camera_configure(). */
typedef enum {
    AG_MODE_SINGLE_FRAME,
    AG_MODE_CONTINUOUS
} AgAcquisitionMode;

/* Sensor topology detected on camera open.  STEREO means a dual-eye
 * Lucid head delivering DualBayerRG8 (left+right interleaved per row);
 * MONO means a single-sensor camera delivering BayerRG8 or Mono8. */
typedef enum {
    AG_SENSOR_STEREO,
    AG_SENSOR_MONO
} AgSensorMode;

/* Returned by camera_configure(). */
typedef struct {
    ArvStream   *stream;
    guint        frame_w;          /* width after binning  */
    guint        frame_h;          /* height after binning */
    int          software_binning; /* >1 if HW binning unavailable */
    size_t       payload;
    gboolean     data_is_bayer;    /* FALSE when eff. binning > 1 */
    AgSensorMode sensor_mode;      /* stereo dual-eye vs single sensor */
    char         pixel_format[32]; /* actual PixelFormat selected      */
} AgCameraConfig;

/* --- Network helpers --- */

const char *interface_ipv4_address (const char *iface_name);
gboolean    device_on_interface (const char *device_addr_str,
                                 const char *iface_name);
char       *resolve_device_id_by_address (const char *address,
                                          const char *opt_interface);

/*
 * Resolve a camera from --serial, --address, or interactive picker.
 * Pass NULL for serial/address if unused.  When interactive is TRUE and
 * neither serial nor address is given, presents a numbered menu.
 * Returns g_strdup'd device ID; caller must g_free.
 */
char *resolve_device (const char *serial, const char *address,
                      const char *interface_name, gboolean interactive);

/*
 * Set ARV_INTERFACE and return the interface's IPv4 address string.
 * Returns NULL on error (prints its own diagnostic).
 */
const char *setup_interface (const char *interface_name);

/* --- Aravis feature helpers --- */

void     try_set_string_feature  (ArvDevice *dev, const char *name,
                                  const char *value);
void     try_set_integer_feature (ArvDevice *dev, const char *name,
                                  gint64 value);
void     try_set_float_feature   (ArvDevice *dev, const char *name,
                                  double value);
gint64   read_integer_feature_or_default (ArvDevice *dev, const char *name,
                                          gint64 fallback);
gboolean try_get_integer_feature (ArvDevice *dev, const char *name,
                                  gint64 *out_value);
gboolean try_get_float_feature   (ArvDevice *dev, const char *name,
                                  double *out_value);
void     try_execute_optional_command (ArvDevice *dev, const char *name);

/*
 * Probe an opened camera's available pixel formats and decide whether it
 * is a stereo Lucid head (advertises DualBayerRG8) or a monocular Lucid
 * camera (BayerRG8 / Mono8 only).  On success, *out_mode is set and
 * *out_pixel_format is filled with the recommended PixelFormat string
 * (caller-provided buffer, recommend >= 32 bytes).  Returns 0 on
 * success, -1 if no usable pixel format is available.
 */
int detect_sensor_mode (ArvCamera *camera, AgSensorMode *out_mode,
                        char *out_pixel_format, size_t buf_len);

/* --- Unified camera configuration --- */

/*
 * Full camera setup: stop stale acq, configure trigger/binning/geometry/
 * transport/stream, push buffers.  On success fills *out and returns 0.
 * On failure prints an error and returns EXIT_FAILURE.
 *
 * The caller still owns camera; this function does NOT unref it.
 * The caller must g_object_unref(out->stream) when done.
 */
int camera_configure (ArvCamera *camera, AgAcquisitionMode mode,
                      int binning, double exposure_us, double gain_db,
                      gboolean auto_expose, int packet_size,
                      const char *iface_ip, gboolean verbose,
                      AgCameraConfig *out);

/*
 * Run a settle-and-lock loop for auto-exposure.  Fires software triggers,
 * discards frames, and monitors ExposureTime until stable (3 consecutive
 * readings within 2%).  Then locks ExposureAuto and GainAuto to "Off".
 * Returns 0 on success.
 */
int auto_expose_settle (ArvCamera *camera, AgCameraConfig *cfg,
                        double trigger_interval_us);

#endif /* AG_COMMON_H */
