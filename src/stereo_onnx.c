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

    /* Input/output metadata from the model */
    char  *input_name_left;
    char  *input_name_right;
    char **output_names;
    size_t num_outputs;
} OnnxHandle;

/* ------------------------------------------------------------------ */
/*  Pad dimension up to nearest multiple of 32                         */
/* ------------------------------------------------------------------ */

static uint32_t
pad32 (uint32_t v)
{
    return ((v + 31u) / 32u) * 32u;
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

static void
warmup_inference (OnnxHandle *h)
{
    const OrtApi *api = h->api;

    int64_t shape[4] = { 1, 3, (int64_t) h->pad_h, (int64_t) h->pad_w };
    size_t data_size = h->buf_elems * sizeof (float);

    /* Fill with mid-grey. */
    for (size_t i = 0; i < h->buf_elems; i++) {
        h->left_buf[i]  = 128.0f;
        h->right_buf[i] = 128.0f;
    }

    OrtValue *inputs[2]  = { NULL, NULL };
    OrtValue *outputs[1] = { NULL };

    if (check_ort (api,
            api->CreateTensorWithDataAsOrtValue (
                h->mem_info, h->left_buf, data_size,
                shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &inputs[0]),
            "warmup: CreateTensor left"))
        return;

    if (check_ort (api,
            api->CreateTensorWithDataAsOrtValue (
                h->mem_info, h->right_buf, data_size,
                shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &inputs[1]),
            "warmup: CreateTensor right")) {
        api->ReleaseValue (inputs[0]);
        return;
    }

    const char *input_names[2]  = { h->input_name_left, h->input_name_right };
    /* Request only the last output for warm-up. */
    const char *output_names[1] = { h->output_names[h->num_outputs - 1] };

    struct timespec t0, t1;
    clock_gettime (CLOCK_MONOTONIC, &t0);

    OrtStatus *s = api->Run (h->session, NULL,
                              input_names, (const OrtValue *const *) inputs, 2,
                              output_names, 1, outputs);

    clock_gettime (CLOCK_MONOTONIC, &t1);
    double dt = (double) (t1.tv_sec - t0.tv_sec)
              + (double) (t1.tv_nsec - t0.tv_nsec) / 1e9;

    if (s != NULL) {
        fprintf (stderr, "onnx: warm-up inference failed: %s\n",
                 api->GetErrorMessage (s));
        api->ReleaseStatus (s);
    } else {
        printf ("  warm-up: %.2f s\n", dt);
        api->ReleaseValue (outputs[0]);
    }

    api->ReleaseValue (inputs[0]);
    api->ReleaseValue (inputs[1]);
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
     * Probe order: CUDA > CoreML > CPU (always available).
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
            const char *empty_keys[]   = { NULL };
            const char *empty_values[] = { NULL };
            s = api->SessionOptionsAppendExecutionProvider (
                h->opts, "CoreML", empty_keys, empty_values, 0);
            if (s == NULL) {
                printf ("ONNX: using CoreML execution provider\n");
            } else {
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

    /* Allocate NCHW buffers. */
    h->buf_elems = (size_t) 3 * h->pad_h * h->pad_w;
    h->left_buf  = g_malloc0 (h->buf_elems * sizeof (float));
    h->right_buf = g_malloc0 (h->buf_elems * sizeof (float));

    /* Memory info for CPU tensors. */
    if (check_ort (api,
            api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault,
                                      &h->mem_info),
            "CreateCpuMemoryInfo"))
        goto fail;

    /* Warm-up inference (first pass triggers JIT, allocation, etc.). */
    warmup_inference (h);

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

    /*
     * 1. Convert uint8 grayscale → float32 [0,255], replicate to 3 channels,
     *    write into pre-allocated NCHW buffers with edge-replication padding.
     */
    size_t plane = (size_t) h->pad_h * h->pad_w;

    /* Fill the valid image region (all 3 channels identical). */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            float lv = (float) left [y * width + x];
            float rv = (float) right[y * width + x];
            size_t off = (size_t) y * h->pad_w + x;
            h->left_buf [off]             = lv;
            h->left_buf [plane + off]     = lv;
            h->left_buf [2 * plane + off] = lv;
            h->right_buf[off]             = rv;
            h->right_buf[plane + off]     = rv;
            h->right_buf[2 * plane + off] = rv;
        }
    }

    /* Edge-replicate right margin. */
    if (h->pad_w > width) {
        for (uint32_t y = 0; y < height; y++) {
            float lv = (float) left [y * width + (width - 1)];
            float rv = (float) right[y * width + (width - 1)];
            for (uint32_t x = width; x < h->pad_w; x++) {
                size_t off = (size_t) y * h->pad_w + x;
                h->left_buf [off]             = lv;
                h->left_buf [plane + off]     = lv;
                h->left_buf [2 * plane + off] = lv;
                h->right_buf[off]             = rv;
                h->right_buf[plane + off]     = rv;
                h->right_buf[2 * plane + off] = rv;
            }
        }
    }

    /* Edge-replicate bottom margin. */
    if (h->pad_h > height) {
        for (uint32_t y = height; y < h->pad_h; y++) {
            for (uint32_t x = 0; x < h->pad_w; x++) {
                uint32_t src_x = x < width ? x : (width - 1);
                float lv = (float) left [(height - 1) * width + src_x];
                float rv = (float) right[(height - 1) * width + src_x];
                size_t off = (size_t) y * h->pad_w + x;
                h->left_buf [off]             = lv;
                h->left_buf [plane + off]     = lv;
                h->left_buf [2 * plane + off] = lv;
                h->right_buf[off]             = rv;
                h->right_buf[plane + off]     = rv;
                h->right_buf[2 * plane + off] = rv;
            }
        }
    }

    /*
     * 2. Create ORT tensors wrapping our buffers (zero-copy).
     */
    int64_t shape[4] = { 1, 3, (int64_t) h->pad_h, (int64_t) h->pad_w };
    size_t data_size = h->buf_elems * sizeof (float);

    OrtValue *inputs[2]  = { NULL, NULL };

    if (check_ort (api,
            api->CreateTensorWithDataAsOrtValue (
                h->mem_info, h->left_buf, data_size,
                shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &inputs[0]),
            "CreateTensor left"))
        return -1;

    if (check_ort (api,
            api->CreateTensorWithDataAsOrtValue (
                h->mem_info, h->right_buf, data_size,
                shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &inputs[1]),
            "CreateTensor right")) {
        api->ReleaseValue (inputs[0]);
        return -1;
    }

    /*
     * 3. Run inference.  Request all outputs — we'll read the last one.
     */
    OrtValue **outputs = g_malloc0 (h->num_outputs * sizeof (OrtValue *));

    const char *input_names[2] = { h->input_name_left, h->input_name_right };

    OrtStatus *s = api->Run (h->session, NULL,
                              input_names,
                              (const OrtValue *const *) inputs, 2,
                              (const char *const *) h->output_names,
                              h->num_outputs, outputs);

    api->ReleaseValue (inputs[0]);
    api->ReleaseValue (inputs[1]);

    if (s != NULL) {
        fprintf (stderr, "onnx: Run failed: %s\n", api->GetErrorMessage (s));
        api->ReleaseStatus (s);
        g_free (outputs);
        return -1;
    }

    /*
     * 4. Read last output tensor, crop to original dims, convert to Q4.4.
     */
    OrtValue *last_output = outputs[h->num_outputs - 1];

    float *out_data = NULL;
    if (check_ort (api,
            api->GetTensorMutableData (last_output, (void **) &out_data),
            "GetTensorMutableData")) {
        for (size_t i = 0; i < h->num_outputs; i++)
            api->ReleaseValue (outputs[i]);
        g_free (outputs);
        return -1;
    }

    /*
     * Output shape may be [1, 1, pad_h, pad_w], [1, pad_h, pad_w],
     * or [pad_h, pad_w].  We need to find where the spatial (pad_h, pad_w)
     * data starts.  Query the shape to determine the offset.
     */
    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    size_t spatial_offset = 0;

    if (api->GetTensorTypeAndShape (last_output, &shape_info) == NULL) {
        size_t ndims = 0;
        (void) api->GetDimensionsCount (shape_info, &ndims);
        if (ndims == 4) {
            /* [1, 1, H, W] or [1, C, H, W] — data starts at beginning. */
            spatial_offset = 0;
        } else if (ndims == 3) {
            /* [1, H, W] — data starts at beginning. */
            spatial_offset = 0;
        }
        /* ndims == 2: [H, W] — also offset 0. */
        api->ReleaseTensorTypeAndShapeInfo (shape_info);
    }

    /* Crop and convert to Q4.4. */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            float d = out_data[spatial_offset + (size_t) y * h->pad_w + x];
            float q4 = d * 16.0f;
            if (q4 > 32767.0f)  q4 = 32767.0f;
            if (q4 < -32768.0f) q4 = -32768.0f;
            disparity_out[y * width + x] = (int16_t) q4;
        }
    }

    /*
     * 5. Release output values.
     */
    for (size_t i = 0; i < h->num_outputs; i++)
        api->ReleaseValue (outputs[i]);
    g_free (outputs);

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
