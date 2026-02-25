/*
 * common.h — shared declarations for ag-cam-tools
 */

#ifndef AG_COMMON_H
#define AG_COMMON_H

#include <arv.h>
#include <glib.h>

/* Sensor geometry for the PDH016S (DualBayerRG8). */
#define AG_SENSOR_WIDTH   2880
#define AG_SENSOR_HEIGHT  1080

/* Acquisition mode for camera_configure(). */
typedef enum {
    AG_MODE_SINGLE_FRAME,
    AG_MODE_CONTINUOUS
} AgAcquisitionMode;

/* Returned by camera_configure(). */
typedef struct {
    ArvStream *stream;
    guint      frame_w;          /* width after binning  */
    guint      frame_h;          /* height after binning */
    int        software_binning; /* >1 if HW binning unavailable */
    size_t     payload;
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

/* --- Gamma / LUT --- */

const guint8 *gamma_lut_2p5 (void);
void apply_lut_inplace (guint8 *data, size_t n, const guint8 lut[256]);

/* --- Debayer (BayerRG8 bilinear → interleaved RGB) --- */

void debayer_rg8_to_rgb (const guint8 *bayer, guint8 *rgb,
                         guint width, guint height);

/* --- DualBayer helpers --- */

void deinterleave_dual_bayer (const guint8 *interleaved, guint width,
                              guint height, guint8 *left, guint8 *right);
void software_bin_2x2 (const guint8 *src, guint src_w, guint src_h,
                        guint8 *dst, guint dst_w, guint dst_h);

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
