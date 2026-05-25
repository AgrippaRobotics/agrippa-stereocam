# libagrippa: C library API

`libagrippa.so` exposes the same camera open / configure / capture
pipeline used internally by `ag-cam-tools` as a reusable shared
library, so non-CLI consumers (Python wrappers, robotics frameworks,
test harnesses, etc.) can keep a persistent camera handle and avoid
the per-frame cost of spawning a fresh subprocess and re-running GigE
Vision discovery.

The public header is [`src/agrippa.h`](../../src/agrippa.h). The CLI's
`ag-cam-tools capture` single-frame path is implemented on top of this
library, so the CLI itself acts as a regression check for the API.

## Build and install

`libagrippa.so` is built by the default `make` target alongside
`bin/ag-cam-tools`:

```bash
make
sudo make install
```

`make install` places:

- `libagrippa.so` under `$PREFIX/lib/`
- `agrippa.h` under `$PREFIX/include/agrippa/`

Use `make lib` to build only the shared library when iterating on a
Python wrapper.

## API surface

```c
#include <agrippa/agrippa.h>

AgOpenParams params = {
    .address    = "192.168.9.17",
    .binning    = 1,
    .continuous = 1,
};
AgCamera *cam = ag_camera_open(&params);

AgFrame frame;
while (ag_camera_capture(cam, &frame) == 0) {
    /* frame.left, frame.right are per-eye Bayer planes of
     * frame.width * frame.height bytes each.  The buffers are valid
     * until the next ag_camera_capture() call. */
    process(frame.left, frame.right, frame.width, frame.height);
    ag_camera_release_frame(cam, &frame);
}

ag_camera_close(cam);
```

### `AgOpenParams`

| Field | Meaning |
|-------|---------|
| `address` / `serial` | Mutually exclusive camera selectors. At least one must be non-NULL. |
| `interface_name` | Optional NIC name (forces `ARV_INTERFACE`). |
| `exposure_us` | Manual exposure in microseconds, or `<=0` to keep the camera default. |
| `gain_db` | Manual gain in dB (0-48), or `<0` to keep the camera default. |
| `auto_expose` | When set to 1, runs auto-expose-and-lock at open time. Mutually exclusive with manual exposure / gain. |
| `binning` | Sensor binning (1 or 2). |
| `packet_size` | GigE packet size, or 0 to auto-negotiate. |
| `calibration_local_path` / `calibration_slot` | Optional rectification source. Mutually exclusive; the loaded `AgRemapTable`s are accessible via `ag_camera_get_remap_left/right`. |
| `continuous` | 1 (recommended) starts continuous acquisition once at open time and pays only the per-frame software-trigger cost on each `ag_camera_capture()`; 0 mimics `ag-cam-tools capture`'s SingleFrame behaviour and restarts acquisition every capture. |
| `verbose` | Diagnostic prints during configuration. |

### `AgFrame`

| Field | Meaning |
|-------|---------|
| `left`, `right` | Per-eye Bayer planes. `right` is `NULL` for monocular Lucid cameras (BayerRG8 / Mono8). |
| `width`, `height` | Per-eye dimensions after software binning. |
| `frame_id`, `timestamp_ns` | Camera-reported metadata. |

### Entry points

- `ag_camera_open(const AgOpenParams *)` &mdash; opens the camera, configures it, and starts acquisition when `continuous=1`.
- `ag_camera_capture(AgCamera *, AgFrame *)` &mdash; software-triggers a frame, splits DualBayerRG8 into per-eye planes, and returns 0 on success.
- `ag_camera_release_frame(AgCamera *, AgFrame *)` &mdash; no-op in this revision; reserved so a future zero-copy buffer hand-off can be added without an ABI break.
- `ag_camera_close(AgCamera *)` &mdash; stops acquisition (if running), frees rectification tables, and tears down the camera handle. Does not call `arv_shutdown()` so subsequent opens keep their discovery cache warm.
- `ag_camera_last_error(AgCamera *)` &mdash; last diagnostic string captured on this handle.

Accessors:

- `ag_camera_is_stereo` &mdash; 1 for DualBayerRG8 heads, 0 for monocular cameras.
- `ag_camera_data_is_bayer` &mdash; 1 when the captured planes still carry a valid Bayer CFA, 0 when binning destroyed the mosaic (see [binning-and-bayer](binning-and-bayer.md)).
- `ag_camera_get_remap_left` / `ag_camera_get_remap_right` &mdash; borrowed pointers to the library-owned rectification tables.

## Threading

An `AgCamera` handle is not internally synchronised. Serialise access
externally if multiple threads share a single handle. Different
handles addressing different cameras can be used concurrently.

## Memory model

`AgFrame.left` and `AgFrame.right` point into scratch buffers owned by
the `AgCamera` handle. The buffers are reused on every
`ag_camera_capture()` call, so callers that need to keep frame data
beyond the next capture must copy it. The Python wrapper in
[makeit-vision](https://github.com/) does exactly this via
`np.frombuffer(...).copy()`.

## Out of scope (for the initial revision)

- FrameBurstStart triggering (`ag-cam-tools capture --burst` still
  uses its own raw-Aravis pipeline).
- AprilTag detection through the library (callers run their own
  detector on the per-eye Bayer planes if they need it).
- Zero-copy buffer hand-off; `ag_camera_capture` always copies the
  current Aravis buffer into the handle's scratch space.
- Applying rectification inside the library; tables are loaded and
  validated, but rectification is currently the caller's
  responsibility via `ag_camera_get_remap_*`.
