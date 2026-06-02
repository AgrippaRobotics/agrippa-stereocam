/*
 * agrippa.c — libagrippa implementation
 *
 * Thin wrapper over the existing helpers in common.c / imgproc.c /
 * calib_load.c that exposes a persistent camera handle suitable for
 * driving from Python (or any non-CLI consumer) without spawning a
 * new ag-cam-tools subprocess per frame.
 */

#include "agrippa.h"

#include "common.h"
#include "imgproc.h"
#include "calib_load.h"
#include "remap.h"

#include <arv.h>
#include <glib.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AG_ERROR_BUF_LEN     512
#define AG_POP_TIMEOUT_US    5000000
#define AG_POP_MAX_ATTEMPTS  10
#define AG_TRIGGER_POLL_MAX  100
#define AG_TRIGGER_POLL_US   10000
#define AG_AE_INTERVAL_US    100000.0

struct AgCamera {
    ArvCamera     *camera;
    AgCameraConfig cfg;

    /* Optional rectification tables loaded when calibration is configured. */
    AgRemapTable  *remap_left;
    AgRemapTable  *remap_right;

    /* Intermediate debayer buffers (3 × eye_n_bytes, RGB).  Allocated only
     * when remap tables are present and data_is_bayer is true.  The pipeline
     * is: Bayer → debayer_rg8_to_rgb → left_rgb_buf → ag_remap_rgb →
     * left_rect_buf, which preserves the Bayer CFA pattern alignment. */
    guint8        *left_rgb_buf;
    guint8        *right_rgb_buf;

    /* Rectified output buffers returned via AgFrame.left/right.
     * 3 × eye_n_bytes when data_is_bayer (RGB output), eye_n_bytes otherwise. */
    guint8        *left_rect_buf;
    guint8        *right_rect_buf;

    gboolean       continuous;
    gboolean       acquisition_running;
    gboolean       auto_expose_done;
    double         pending_exposure_us;
    double         pending_gain_db;
    gboolean       auto_expose_requested;

    /* Scratch buffers for split Bayer planes; sized to one eye for
     * stereo, or the full sensor for mono.  Reused across captures so
     * the buffers returned via AgFrame.left/right remain valid until
     * the next capture or close. */
    guint8        *left_buf;
    guint8        *right_buf;
    guint          sub_w;
    guint          sub_h;
    size_t         eye_n_bytes;

    char           last_error[AG_ERROR_BUF_LEN];
};

static void
ag_set_error (AgCamera *cam, const char *fmt, ...)
{
    if (!cam)
        return;
    va_list ap;
    va_start (ap, fmt);
    g_vsnprintf (cam->last_error, sizeof cam->last_error, fmt, ap);
    va_end (ap);
}

static void
ag_clear_error (AgCamera *cam)
{
    if (cam)
        cam->last_error[0] = '\0';
}

static int
ag_start_acquisition (AgCamera *cam)
{
    GError *error = NULL;
    arv_camera_start_acquisition (cam->camera, &error);
    if (error) {
        ag_set_error (cam, "failed to start acquisition: %s", error->message);
        g_clear_error (&error);
        return -1;
    }
    cam->acquisition_running = TRUE;
    return 0;
}

static void
ag_stop_acquisition (AgCamera *cam)
{
    if (!cam->acquisition_running)
        return;
    arv_camera_stop_acquisition (cam->camera, NULL);
    cam->acquisition_running = FALSE;
}

static gboolean
ag_wait_trigger_armed (AgCamera *cam)
{
    ArvDevice *device = arv_camera_get_device (cam->camera);
    gboolean armed = FALSE;
    for (int p = 0; p < AG_TRIGGER_POLL_MAX && !armed; p++) {
        GError *e = NULL;
        armed = arv_device_get_boolean_feature_value (device, "TriggerArmed", &e);
        g_clear_error (&e);
        if (!armed)
            g_usleep (AG_TRIGGER_POLL_US);
    }
    return armed;
}

static int
ag_fire_trigger (AgCamera *cam)
{
    ArvDevice *device = arv_camera_get_device (cam->camera);
    GError *e = NULL;
    arv_device_execute_command (device, "TriggerSoftware", &e);
    if (e) {
        ag_set_error (cam, "TriggerSoftware failed: %s", e->message);
        g_clear_error (&e);
        return -1;
    }
    return 0;
}

static ArvBuffer *
ag_pop_good_buffer (AgCamera *cam)
{
    ArvBuffer *partial = NULL;

    for (int i = 0; i < AG_POP_MAX_ATTEMPTS; i++) {
        ArvBuffer *b = arv_stream_timeout_pop_buffer (cam->cfg.stream,
                                                       AG_POP_TIMEOUT_US);
        if (!b)
            continue;

        ArvBufferStatus st = arv_buffer_get_status (b);
        if (st == ARV_BUFFER_STATUS_SUCCESS) {
            if (partial)
                arv_stream_push_buffer (cam->cfg.stream, partial);
            return b;
        }

        if (partial)
            arv_stream_push_buffer (cam->cfg.stream, partial);
        partial = b;
    }

    if (partial)
        arv_stream_push_buffer (cam->cfg.stream, partial);
    ag_set_error (cam, "timeout waiting for frame after %d attempts",
                  AG_POP_MAX_ATTEMPTS);
    return NULL;
}

static int
ag_load_calibration (AgCamera *cam, const AgOpenParams *params)
{
    if (!params->calibration_local_path && params->calibration_slot < 0)
        return 0;

    if (params->calibration_local_path && params->calibration_slot >= 0) {
        ag_set_error (cam,
                      "calibration_local_path and calibration_slot are "
                      "mutually exclusive");
        return -1;
    }

    if (cam->cfg.sensor_mode == AG_SENSOR_MONO) {
        ag_set_error (cam,
                      "calibration is stereo-only; not applicable to mono cameras");
        return -1;
    }

    AgCalibSource src = {
        .local_path = params->calibration_local_path,
        .slot       = (params->calibration_local_path
                         ? -1 : params->calibration_slot),
    };

    ArvDevice *device = arv_camera_get_device (cam->camera);
    if (ag_calib_load (device, &src,
                       &cam->remap_left, &cam->remap_right, NULL) != 0) {
        ag_set_error (cam, "failed to load calibration");
        return -1;
    }

    guint proc_sub_w = (cam->cfg.frame_w / 2)
                       / (guint) cam->cfg.software_binning;
    guint proc_h     = cam->cfg.frame_h / (guint) cam->cfg.software_binning;
    if (cam->remap_left->width  != proc_sub_w ||
        cam->remap_left->height != proc_h) {
        ag_set_error (cam,
                      "remap dimensions %ux%u do not match frame %ux%u",
                      cam->remap_left->width, cam->remap_left->height,
                      proc_sub_w, proc_h);
        ag_remap_table_free (cam->remap_left);
        ag_remap_table_free (cam->remap_right);
        cam->remap_left  = NULL;
        cam->remap_right = NULL;
        return -1;
    }

    return 0;
}

AgCamera *
ag_camera_open (const AgOpenParams *params)
{
    if (!params) {
        fprintf (stderr, "ag_camera_open: NULL params\n");
        return NULL;
    }

    if (params->binning != 1 && params->binning != 2) {
        fprintf (stderr, "ag_camera_open: binning must be 1 or 2 (got %d)\n",
                 params->binning);
        return NULL;
    }
    if (params->auto_expose &&
        (params->exposure_us > 0.0 || params->gain_db >= 0.0)) {
        fprintf (stderr, "ag_camera_open: auto_expose and exposure/gain are "
                         "mutually exclusive\n");
        return NULL;
    }
    if (params->address && params->serial) {
        fprintf (stderr, "ag_camera_open: address and serial are mutually exclusive\n");
        return NULL;
    }
    if (!params->address && !params->serial) {
        fprintf (stderr, "ag_camera_open: one of address or serial must be set\n");
        return NULL;
    }

    const char *iface_ip = NULL;
    if (params->interface_name) {
        iface_ip = setup_interface (params->interface_name);
        if (!iface_ip)
            return NULL;
    }

    char *device_id = resolve_device (params->serial, params->address,
                                       params->interface_name, FALSE);
    if (!device_id)
        return NULL;

    GError *error = NULL;
    ArvCamera *arv_cam = arv_camera_new (device_id, &error);
    g_free (device_id);
    if (!arv_cam) {
        fprintf (stderr, "ag_camera_open: %s\n",
                 error ? error->message : "failed to open device");
        g_clear_error (&error);
        return NULL;
    }

    AgCamera *cam = g_new0 (AgCamera, 1);
    cam->camera     = arv_cam;
    cam->continuous = params->continuous ? TRUE : FALSE;

    AgAcquisitionMode mode = cam->continuous ? AG_MODE_CONTINUOUS
                                              : AG_MODE_SINGLE_FRAME;

    if (camera_configure (arv_cam, mode,
                          params->binning,
                          params->exposure_us,
                          params->gain_db,
                          params->auto_expose ? TRUE : FALSE,
                          params->packet_size,
                          iface_ip,
                          params->verbose ? TRUE : FALSE,
                          &cam->cfg) != EXIT_SUCCESS) {
        ag_set_error (cam, "camera_configure failed");
        fprintf (stderr, "ag_camera_open: %s\n", cam->last_error);
        g_object_unref (arv_cam);
        g_free (cam);
        return NULL;
    }

    if (ag_load_calibration (cam, params) != 0) {
        fprintf (stderr, "ag_camera_open: %s\n", cam->last_error);
        g_object_unref (cam->cfg.stream);
        g_object_unref (arv_cam);
        g_free (cam);
        return NULL;
    }

    /* Per-eye output dimensions after software binning. */
    if (cam->cfg.sensor_mode == AG_SENSOR_STEREO) {
        cam->sub_w = (cam->cfg.frame_w / 2)
                     / (guint) cam->cfg.software_binning;
        cam->sub_h = cam->cfg.frame_h
                     / (guint) cam->cfg.software_binning;
    } else {
        cam->sub_w = cam->cfg.frame_w / (guint) cam->cfg.software_binning;
        cam->sub_h = cam->cfg.frame_h / (guint) cam->cfg.software_binning;
    }
    cam->eye_n_bytes = (size_t) cam->sub_w * (size_t) cam->sub_h;
    cam->left_buf    = g_malloc (cam->eye_n_bytes);
    if (cam->cfg.sensor_mode == AG_SENSOR_STEREO)
        cam->right_buf = g_malloc (cam->eye_n_bytes);

    if (cam->remap_left) {
        if (cam->cfg.data_is_bayer) {
            cam->left_rgb_buf   = g_malloc (cam->eye_n_bytes * 3);
            cam->right_rgb_buf  = g_malloc (cam->eye_n_bytes * 3);
            cam->left_rect_buf  = g_malloc (cam->eye_n_bytes * 3);
            cam->right_rect_buf = g_malloc (cam->eye_n_bytes * 3);
        } else {
            cam->left_rect_buf  = g_malloc (cam->eye_n_bytes);
            cam->right_rect_buf = g_malloc (cam->eye_n_bytes);
        }
    }

    cam->auto_expose_requested = params->auto_expose ? TRUE : FALSE;
    cam->auto_expose_done      = FALSE;

    if (cam->continuous) {
        if (ag_start_acquisition (cam) != 0) {
            fprintf (stderr, "ag_camera_open: %s\n", cam->last_error);
            ag_camera_close (cam);
            return NULL;
        }
        if (cam->auto_expose_requested) {
            auto_expose_settle (cam->camera, &cam->cfg, AG_AE_INTERVAL_US);
            cam->auto_expose_done = TRUE;
        }
    }

    return cam;
}

int
ag_camera_capture (AgCamera *cam, AgFrame *out)
{
    if (!cam) {
        fprintf (stderr, "ag_camera_capture: NULL handle\n");
        return -1;
    }
    if (!out) {
        ag_set_error (cam, "NULL out parameter");
        return -1;
    }

    ag_clear_error (cam);

    if (!cam->acquisition_running) {
        if (ag_start_acquisition (cam) != 0)
            return -1;
        if (cam->auto_expose_requested && !cam->auto_expose_done) {
            auto_expose_settle (cam->camera, &cam->cfg, AG_AE_INTERVAL_US);
            cam->auto_expose_done = TRUE;
        }
    }

    if (!ag_wait_trigger_armed (cam))
        fprintf (stderr, "warn: TriggerArmed not set, triggering anyway\n");

    if (ag_fire_trigger (cam) != 0)
        goto fail;

    ArvBuffer *buffer = ag_pop_good_buffer (cam);
    if (!buffer)
        goto fail;

    size_t data_size = 0;
    const guint8 *data = arv_buffer_get_data (buffer, &data_size);
    guint width  = arv_buffer_get_image_width  (buffer);
    guint height = arv_buffer_get_image_height (buffer);
    size_t needed = (size_t) width * (size_t) height;

    if (!data || data_size < needed) {
        ag_set_error (cam,
                      "unsupported buffer (%zu bytes for %ux%u, need %zu)",
                      data_size, width, height, needed);
        arv_stream_push_buffer (cam->cfg.stream, buffer);
        goto fail;
    }

    if (cam->cfg.sensor_mode == AG_SENSOR_STEREO) {
        if (width != cam->cfg.frame_w || height != cam->cfg.frame_h) {
            ag_set_error (cam, "unexpected frame %ux%u (expected %ux%u)",
                          width, height, cam->cfg.frame_w, cam->cfg.frame_h);
            arv_stream_push_buffer (cam->cfg.stream, buffer);
            goto fail;
        }
        extract_dual_bayer_eyes (data, width, height,
                                  cam->cfg.software_binning,
                                  cam->left_buf, cam->right_buf);
        if (cam->remap_left) {
            if (cam->cfg.data_is_bayer) {
                apply_lut_inplace (cam->left_buf,  cam->eye_n_bytes, gamma_lut_2p5 ());
                apply_lut_inplace (cam->right_buf, cam->eye_n_bytes, gamma_lut_2p5 ());
                debayer_rg8_to_rgb (cam->left_buf,  cam->left_rgb_buf,
                                    cam->sub_w, cam->sub_h);
                debayer_rg8_to_rgb (cam->right_buf, cam->right_rgb_buf,
                                    cam->sub_w, cam->sub_h);
                ag_remap_rgb (cam->remap_left,  cam->left_rgb_buf,  cam->left_rect_buf);
                ag_remap_rgb (cam->remap_right, cam->right_rgb_buf, cam->right_rect_buf);
                out->channels = 3;
            } else {
                apply_lut_inplace (cam->left_buf,  cam->eye_n_bytes, gamma_lut_2p5 ());
                apply_lut_inplace (cam->right_buf, cam->eye_n_bytes, gamma_lut_2p5 ());
                ag_remap_gray (cam->remap_left,  cam->left_buf,  cam->left_rect_buf);
                ag_remap_gray (cam->remap_right, cam->right_buf, cam->right_rect_buf);
                out->channels = 1;
            }
            out->left  = cam->left_rect_buf;
            out->right = cam->right_rect_buf;
        } else {
            out->left     = cam->left_buf;
            out->right    = cam->right_buf;
            out->channels = 1;
        }
    } else {
        if (width != cam->cfg.frame_w || height != cam->cfg.frame_h) {
            ag_set_error (cam, "unexpected frame %ux%u (expected %ux%u)",
                          width, height, cam->cfg.frame_w, cam->cfg.frame_h);
            arv_stream_push_buffer (cam->cfg.stream, buffer);
            goto fail;
        }
        if (cam->cfg.software_binning > 1) {
            software_bin_2x2 (data, width, height,
                              cam->left_buf, cam->sub_w, cam->sub_h);
        } else {
            memcpy (cam->left_buf, data, cam->eye_n_bytes);
        }
        out->left     = cam->left_buf;
        out->right    = NULL;
        out->channels = 1;
    }

    out->width        = cam->sub_w;
    out->height       = cam->sub_h;
    out->frame_id     = (unsigned long) arv_buffer_get_frame_id  (buffer);
    out->timestamp_ns = (unsigned long) arv_buffer_get_timestamp (buffer);

    arv_stream_push_buffer (cam->cfg.stream, buffer);

    if (!cam->continuous)
        ag_stop_acquisition (cam);

    return 0;

fail:
    if (!cam->continuous)
        ag_stop_acquisition (cam);
    return -1;
}

void
ag_camera_release_frame (AgCamera *cam, AgFrame *frame)
{
    (void) cam;
    if (frame)
        memset (frame, 0, sizeof *frame);
}

void
ag_camera_close (AgCamera *cam)
{
    if (!cam)
        return;

    ag_stop_acquisition (cam);

    if (cam->remap_left)  ag_remap_table_free (cam->remap_left);
    if (cam->remap_right) ag_remap_table_free (cam->remap_right);

    g_free (cam->left_buf);
    g_free (cam->right_buf);
    g_free (cam->left_rgb_buf);
    g_free (cam->right_rgb_buf);
    g_free (cam->left_rect_buf);
    g_free (cam->right_rect_buf);

    if (cam->cfg.stream)
        g_object_unref (cam->cfg.stream);
    if (cam->camera)
        g_object_unref (cam->camera);

    g_free (cam);
}

const char *
ag_camera_last_error (AgCamera *cam)
{
    if (!cam)
        return "";
    return cam->last_error;
}

int
ag_camera_is_stereo (const AgCamera *cam)
{
    if (!cam)
        return 0;
    return cam->cfg.sensor_mode == AG_SENSOR_STEREO ? 1 : 0;
}

int
ag_camera_data_is_bayer (const AgCamera *cam)
{
    if (!cam)
        return 0;
    return cam->cfg.data_is_bayer ? 1 : 0;
}

const AgRemapTable *
ag_camera_get_remap_left (const AgCamera *cam)
{
    return cam ? cam->remap_left : NULL;
}

const AgRemapTable *
ag_camera_get_remap_right (const AgCamera *cam)
{
    return cam ? cam->remap_right : NULL;
}
