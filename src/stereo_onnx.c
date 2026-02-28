/*
 * stereo_onnx.c — ONNX Runtime in-process stereo disparity backend
 *
 * Runs any ONNX stereo model (IGEV++, FoundationStereo, etc.) via the
 * ONNX Runtime C API.  Expects two [1, 3, H, W] float32 inputs in
 * [0, 255] range and produces float32 disparity output.
 *
 * Automatically selects the best execution provider:
 *   CUDA > CoreML (macOS) > CPU
 */

#ifdef HAVE_ONNXRUNTIME

#include "stereo.h"

#include <onnxruntime_c_api.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  ORT error helper                                                   */
/* ------------------------------------------------------------------ */

static int
check_ort (const OrtApi *api, OrtStatus *status, const char *context)
{
    if (status != NULL) {
        const char *msg = api->GetErrorMessage (status);
        fprintf (stderr, "onnx: %s: %s\n", context, msg);
        api->ReleaseStatus (status);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Opaque handle                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const OrtApi   *api;
    OrtEnv         *env;
    OrtSession     *session;
    OrtSessionOptions *opts;
    OrtMemoryInfo  *mem_info;

    uint32_t width, height;
    uint32_t pad_w, pad_h;

    /* Pre-allocated NCHW input buffers: [1, 3, pad_h, pad_w] float32 */
    float *left_buf;
    float *right_buf;
    size_t buf_elems;       /* 3 * pad_h * pad_w */
    size_t input_data_size;
    int64_t input_shape[4];
    OrtValue *input_tensors[2];
    const char *input_names[2];

    /* Input/output metadata from the model */
    char  *input_name_left;
    char  *input_name_right;
    char **output_names;
    size_t num_outputs;
    const char *selected_output_name;
    uint32_t output_stride_w;
} OnnxHandle;

/* ------------------------------------------------------------------ */
/*  Pad dimension up to nearest multiple of 32                         */
/* ------------------------------------------------------------------ */

static uint32_t
pad32 (uint32_t v)
{
    return ((v + 31u) / 32u) * 32u;
}

static void
pack_gray_to_nchw3_padded (const uint8_t *src,
                           uint32_t width, uint32_t height,
                           uint32_t pad_w, uint32_t pad_h,
                           float *dst)
{
    size_t plane = (size_t) pad_h * pad_w;
    float *dst0 = dst;
    float *dst1 = dst + plane;
    float *dst2 = dst + 2 * plane;

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *src_row = src + (size_t) y * width;
        float *row0 = dst0 + (size_t) y * pad_w;
        float *row1 = dst1 + (size_t) y * pad_w;
        float *row2 = dst2 + (size_t) y * pad_w;

        for (uint32_t x = 0; x < width; x++) {
            float v = (float) src_row[x];
            row0[x] = v;
            row1[x] = v;
            row2[x] = v;
        }

        if (pad_w > width) {
            float edge = row0[width - 1];
            for (uint32_t x = width; x < pad_w; x++) {
                row0[x] = edge;
                row1[x] = edge;
                row2[x] = edge;
            }
        }
    }

    if (pad_h > height) {
        size_t row_bytes = (size_t) pad_w * sizeof (float);
        float *last0 = dst0 + (size_t) (height - 1) * pad_w;
        float *last1 = dst1 + (size_t) (height - 1) * pad_w;
        float *last2 = dst2 + (size_t) (height - 1) * pad_w;

        for (uint32_t y = height; y < pad_h; y++) {
            memcpy (dst0 + (size_t) y * pad_w, last0, row_bytes);
            memcpy (dst1 + (size_t) y * pad_w, last1, row_bytes);
            memcpy (dst2 + (size_t) y * pad_w, last2, row_bytes);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Query input/output names from the session                          */
/* ------------------------------------------------------------------ */

static int
query_model_names (OnnxHandle *h)
{
    const OrtApi *api = h->api;
    OrtAllocator *alloc = NULL;

    if (check_ort (api, api->GetAllocatorWithDefaultOptions (&alloc),
                   "GetAllocatorWithDefaultOptions"))
        return -1;

    /* Input names. */
    size_t num_inputs = 0;
    if (check_ort (api, api->SessionGetInputCount (h->session, &num_inputs),
                   "SessionGetInputCount"))
        return -1;

    if (num_inputs < 2) {
        fprintf (stderr, "onnx: model has %zu inputs (expected >= 2)\n",
                 num_inputs);
        return -1;
    }

    char *name0 = NULL, *name1 = NULL;
    if (check_ort (api, api->SessionGetInputName (h->session, 0, alloc, &name0),
                   "SessionGetInputName[0]"))
        return -1;
    if (check_ort (api, api->SessionGetInputName (h->session, 1, alloc, &name1),
                   "SessionGetInputName[1]"))
        return -1;

    h->input_name_left  = g_strdup (name0);
    h->input_name_right = g_strdup (name1);
    (void) api->AllocatorFree (alloc, name0);
    (void) api->AllocatorFree (alloc, name1);

    printf ("  inputs: [%s, %s] (%zu total)\n",
            h->input_name_left, h->input_name_right, num_inputs);

    /* Output names. */
    if (check_ort (api, api->SessionGetOutputCount (h->session, &h->num_outputs),
                   "SessionGetOutputCount"))
        return -1;

    if (h->num_outputs == 0) {
        fprintf (stderr, "onnx: model has no outputs\n");
        return -1;
    }

    h->output_names = g_malloc0 (h->num_outputs * sizeof (char *));
    for (size_t i = 0; i < h->num_outputs; i++) {
        char *oname = NULL;
        if (check_ort (api,
                       api->SessionGetOutputName (h->session, i, alloc, &oname),
                       "SessionGetOutputName"))
            return -1;
        h->output_names[i] = g_strdup (oname);
        (void) api->AllocatorFree (alloc, oname);
    }

    printf ("  outputs: %zu (using last: %s)\n",
            h->num_outputs, h->output_names[h->num_outputs - 1]);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Warm-up inference                                                  */
/* ------------------------------------------------------------------ */

static int
warmup_inference (OnnxHandle *h)
{
    const OrtApi *api = h->api;

    /* Fill with mid-grey. */
    for (size_t i = 0; i < h->buf_elems; i++) {
        h->left_buf[i]  = 128.0f;
        h->right_buf[i] = 128.0f;
    }

    OrtValue *output = NULL;
    const char *output_names[1] = { h->selected_output_name };

    struct timespec t0, t1;
    clock_gettime (CLOCK_MONOTONIC, &t0);

    OrtStatus *s = api->Run (h->session, NULL,
                              h->input_names,
                              (const OrtValue *const *) h->input_tensors, 2,
                              output_names, 1, &output);

    clock_gettime (CLOCK_MONOTONIC, &t1);
    double dt = (double) (t1.tv_sec - t0.tv_sec)
              + (double) (t1.tv_nsec - t0.tv_nsec) / 1e9;

    if (s != NULL) {
        fprintf (stderr, "onnx: warm-up inference failed: %s\n",
                 api->GetErrorMessage (s));
        api->ReleaseStatus (s);
        return -1;
    }

    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    if (check_ort (api,
            api->GetTensorTypeAndShape (output, &shape_info),
            "warmup: GetTensorTypeAndShape")) {
        api->ReleaseValue (output);
        return -1;
    }

    size_t ndims = 0;
    int64_t dims[8] = { 0 };

    if (check_ort (api,
            api->GetDimensionsCount (shape_info, &ndims),
            "warmup: GetDimensionsCount")) {
        api->ReleaseTensorTypeAndShapeInfo (shape_info);
        api->ReleaseValue (output);
        return -1;
    }

    if (ndims < 2 || ndims > 4 || ndims > G_N_ELEMENTS (dims)) {
        fprintf (stderr, "onnx: unsupported output rank %zu\n", ndims);
        api->ReleaseTensorTypeAndShapeInfo (shape_info);
        api->ReleaseValue (output);
        return -1;
    }

    if (check_ort (api,
            api->GetDimensions (shape_info, dims, ndims),
            "warmup: GetDimensions")) {
        api->ReleaseTensorTypeAndShapeInfo (shape_info);
        api->ReleaseValue (output);
        return -1;
    }

    int64_t out_h = dims[ndims - 2];
    int64_t out_w = dims[ndims - 1];
    if (out_h < (int64_t) h->height || out_w < (int64_t) h->width) {
        fprintf (stderr, "onnx: output shape too small: %" PRId64 "x%" PRId64 "\n",
                 out_w, out_h);
        api->ReleaseTensorTypeAndShapeInfo (shape_info);
        api->ReleaseValue (output);
        return -1;
    }

    h->output_stride_w = (uint32_t) out_w;
    api->ReleaseTensorTypeAndShapeInfo (shape_info);

    printf ("  warm-up: %.2f s\n", dt);
    api->ReleaseValue (output);
    return 0;
}

static int
create_input_tensors (OnnxHandle *h)
{
    const OrtApi *api = h->api;

    if (check_ort (api,
            api->CreateTensorWithDataAsOrtValue (
                h->mem_info, h->left_buf, h->input_data_size,
                h->input_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &h->input_tensors[0]),
            "CreateTensor left"))
        return -1;

    if (check_ort (api,
            api->CreateTensorWithDataAsOrtValue (
                h->mem_info, h->right_buf, h->input_data_size,
                h->input_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &h->input_tensors[1]),
            "CreateTensor right"))
        return -1;

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Create                                                             */
/* ------------------------------------------------------------------ */

void *
ag_onnx_create (uint32_t width, uint32_t height, const AgOnnxParams *params)
{
    const OrtApiBase *base = OrtGetApiBase ();
    const OrtApi *api = base->GetApi (ORT_API_VERSION);

    if (!api) {
        fprintf (stderr, "onnx: failed to get ORT API v%d\n", ORT_API_VERSION);
        return NULL;
    }

    OnnxHandle *h = g_malloc0 (sizeof (OnnxHandle));
    h->api    = api;
    h->width  = width;
    h->height = height;
    h->pad_w  = pad32 (width);
    h->pad_h  = pad32 (height);

    /* Environment. */
    if (check_ort (api,
            api->CreateEnv (ORT_LOGGING_LEVEL_WARNING, "agstereo", &h->env),
            "CreateEnv"))
        goto fail;

    /* Session options. */
    if (check_ort (api,
            api->CreateSessionOptions (&h->opts),
            "CreateSessionOptions"))
        goto fail;

    /* Thread count — use 0 for ORT default. */
    (void) api->SetIntraOpNumThreads (h->opts, 0);
    (void) api->SetSessionGraphOptimizationLevel (h->opts, ORT_ENABLE_ALL);

    /* ---- Execution provider selection ----
     *
     * Uses the generic SessionOptionsAppendExecutionProvider() for all EPs
     * so the binary links against any ONNX Runtime build (CPU-only, CUDA,
     * CoreML, etc.) without requiring EP-specific symbols at link time.
     * Each call fails gracefully if the EP isn't compiled into the runtime.
     *
     * Probe order: CUDA > CoreML (macOS) > CPU.
     */
    {
        const char *cuda_keys[]   = { "device_id", NULL };
        const char *cuda_values[] = { "0",         NULL };
        OrtStatus *s = api->SessionOptionsAppendExecutionProvider (
            h->opts, "CUDA", cuda_keys, cuda_values, 1);
        if (s == NULL) {
            printf ("ONNX: using CUDA execution provider\n");
        } else {
            api->ReleaseStatus (s);
            const char *coreml_keys[]   = { "MLComputeUnits",
                                              "RequireStaticInputShapes",
                                              NULL };
            const char *coreml_values[] = { "ALL", "1", NULL };
            s = api->SessionOptionsAppendExecutionProvider (
                h->opts, "CoreML", coreml_keys, coreml_values, 2);
            if (s == NULL) {
                printf ("ONNX: using CoreML execution provider\n");
            } else {
                printf ("ONNX: CoreML unavailable: %s\n",
                        api->GetErrorMessage (s));
                api->ReleaseStatus (s);
                printf ("ONNX: using CPU execution provider\n");
            }
        }
    }

    /* Create session from model file. */
    printf ("ONNX: loading %s (%ux%u, padded to %ux%u)\n",
            params->model_path, width, height, h->pad_w, h->pad_h);

    if (check_ort (api,
            api->CreateSession (h->env, params->model_path, h->opts,
                                &h->session),
            "CreateSession"))
        goto fail;

    /* Query model I/O. */
    if (query_model_names (h) != 0)
        goto fail;
    h->selected_output_name = h->output_names[h->num_outputs - 1];
    h->input_names[0] = h->input_name_left;
    h->input_names[1] = h->input_name_right;

    /* Allocate NCHW buffers. */
    h->buf_elems = (size_t) 3 * h->pad_h * h->pad_w;
    h->input_data_size = h->buf_elems * sizeof (float);
    h->input_shape[0] = 1;
    h->input_shape[1] = 3;
    h->input_shape[2] = (int64_t) h->pad_h;
    h->input_shape[3] = (int64_t) h->pad_w;
    h->left_buf  = g_malloc0 (h->input_data_size);
    h->right_buf = g_malloc0 (h->input_data_size);

    /* Memory info for CPU tensors. */
    if (check_ort (api,
            api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault,
                                      &h->mem_info),
            "CreateCpuMemoryInfo"))
        goto fail;

    if (create_input_tensors (h) != 0)
        goto fail;

    /* Warm-up inference (first pass triggers JIT, allocation, etc.). */
    if (warmup_inference (h) != 0)
        goto fail;

    return h;

fail:
    ag_onnx_destroy (h);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Compute                                                            */
/* ------------------------------------------------------------------ */

int
ag_onnx_compute (void *onnx_ptr, uint32_t width, uint32_t height,
                 const uint8_t *left, const uint8_t *right,
                 int16_t *disparity_out)
{
    OnnxHandle *h = (OnnxHandle *) onnx_ptr;
    const OrtApi *api = h->api;

    pack_gray_to_nchw3_padded (left, width, height,
                               h->pad_w, h->pad_h, h->left_buf);
    pack_gray_to_nchw3_padded (right, width, height,
                               h->pad_w, h->pad_h, h->right_buf);

    OrtValue *output = NULL;
    const char *output_names[1] = { h->selected_output_name };

    OrtStatus *s = api->Run (h->session, NULL,
                              h->input_names,
                              (const OrtValue *const *) h->input_tensors, 2,
                              output_names, 1, &output);

    if (s != NULL) {
        fprintf (stderr, "onnx: Run failed: %s\n", api->GetErrorMessage (s));
        api->ReleaseStatus (s);
        return -1;
    }

    float *out_data = NULL;
    if (check_ort (api,
            api->GetTensorMutableData (output, (void **) &out_data),
            "GetTensorMutableData")) {
        api->ReleaseValue (output);
        return -1;
    }

    /* Crop and convert to Q4.4. */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            float d = out_data[(size_t) y * h->output_stride_w + x];
            float q4 = d * 16.0f;
            if (q4 > 32767.0f)  q4 = 32767.0f;
            if (q4 < -32768.0f) q4 = -32768.0f;
            disparity_out[y * width + x] = (int16_t) q4;
        }
    }

    api->ReleaseValue (output);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Destroy                                                            */
/* ------------------------------------------------------------------ */

void
ag_onnx_destroy (void *onnx_ptr)
{
    OnnxHandle *h = (OnnxHandle *) onnx_ptr;
    if (!h)
        return;

    if (h->input_tensors[0])
        h->api->ReleaseValue (h->input_tensors[0]);
    if (h->input_tensors[1])
        h->api->ReleaseValue (h->input_tensors[1]);
    if (h->session)
        h->api->ReleaseSession (h->session);
    if (h->opts)
        h->api->ReleaseSessionOptions (h->opts);
    if (h->mem_info)
        h->api->ReleaseMemoryInfo (h->mem_info);
    if (h->env)
        h->api->ReleaseEnv (h->env);

    g_free (h->left_buf);
    g_free (h->right_buf);
    g_free (h->input_name_left);
    g_free (h->input_name_right);

    if (h->output_names) {
        for (size_t i = 0; i < h->num_outputs; i++)
            g_free (h->output_names[i]);
        g_free (h->output_names);
    }

    g_free (h);
}

#endif /* HAVE_ONNXRUNTIME */
