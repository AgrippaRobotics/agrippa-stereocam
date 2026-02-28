# `depth-preview-classical`

Live stereo depth preview using a rectified input pair and a classical disparity backend. The display shows the rectified left image beside a JET-colored disparity map.

## Examples

```bash
ag-cam-tools depth-preview-classical -a 192.168.0.201 -A --calibration-local calibration/calibration_20260225_143015_a1b2c3d4
ag-cam-tools depth-preview-classical -a 192.168.0.201 -A --calibration-slot 0
ag-cam-tools depth-preview-classical -a 192.168.0.201 -A --calibration-local <session> --stereo-backend sgbm --block-size 7
```

## Options

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-f`, `--fps` | Trigger rate in Hz |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB |
| `-A`, `--auto-expose` | Auto-expose and then lock |
| `-b`, `--binning` | Sensor binning factor |
| `-p`, `--packet-size` | GigE packet size in bytes |
| `--calibration-local` | Calibration session directory on disk (at least one calibration source required) |
| `--calibration-slot` | On-camera calibration slot: `0`, `1`, or `2` (at least one calibration source required) |
| `--stereo-backend` | `sgbm` by default, with `onnx` also available |
| `--model-path` | Required when `--stereo-backend onnx` is used |
| `--min-disparity` | Override calibration metadata |
| `--num-disparities` | Override calibration metadata |
| `--block-size` | SGBM block size |
| `--z-near` | Near depth bound in cm; dynamically computes `min_disparity` and `num_disparities` |
| `--z-far` | Far depth bound in cm; used with `--z-near` for dynamic disparity range |
| `--clahe` | Enable CLAHE histogram equalisation on rectified input |
| `--clahe-clip` | CLAHE clip limit (default `2.0`) |
| `--clahe-tile` | CLAHE tile grid size (default `8`) |
| `--mask-specular` | Invalidate disparity under specular highlights |
| `--specular-threshold` | Brightness threshold for specular detection (default `250`) |
| `--median-filter` | Median filter kernel size, `3` or `5` (default off) |
| `--morph-cleanup` | Enable morphological close-then-open cleanup |
| `--wls-filter` | Enable WLS (Weighted Least Squares) edge-preserving disparity filter (requires OpenCV ximgproc) |
| `--wls-lambda` | WLS regularisation strength (default `8000`) |
| `--wls-sigma` | WLS colour sensitivity (default `1.5`) |
| `--temporal-filter` | Temporal median filter depth (number of frames, 2–9; default off) |
| `--confidence-map` | Show per-pixel confidence overlay instead of disparity colormap |

## Post-processing pipeline

When enabled, disparity post-processing stages run in the following order:

1. **Specular masking** (`--mask-specular`) — invalidates pixels under saturated
   highlights that confuse block matching.
2. **Median filter** (`--median-filter 3|5`) — removes salt-and-pepper outliers.
3. **Morphological cleanup** (`--morph-cleanup`) — close then open to fill small
   holes and remove isolated blobs.
4. **WLS filter** (`--wls-filter`) — edge-aware smoothing guided by the left
   rectified image.  Requires a left-right disparity pair (computed automatically).
5. **Temporal filter** (`--temporal-filter N`) — per-pixel temporal median over the
   last N frames.  Includes automatic scene-change detection.
6. **Confidence map** (`--confidence-map`) — per-pixel quality score based on
   texture strength and disparity variance.  Replaces the disparity colormap
   with a JET-coloured confidence overlay.

Pre-processing (applied before disparity computation):

- **CLAHE** (`--clahe`) — contrast-limited adaptive histogram equalisation of
  the rectified grayscale input, useful for low-texture industrial surfaces.
- **Dynamic disparity range** (`--z-near`, `--z-far`) — computes `min_disparity`
  and `num_disparities` from real-world depth bounds in centimetres using the
  calibration metadata.

## Runtime controls

- Press `q` or `Esc` to quit.
- Click the disparity panel to print the disparity value at that pixel.

When `--stereo-backend sgbm` is active, live tuning is available:

| Keys | Parameter |
|------|-----------|
| `[` / `]` | `block_size` |
| `;` / `'` | `min_disparity` |
| `-` / `=` | `num_disparities` |
| `z` / `x` | `p1` |
| `c` / `v` | `p2` |
| `r` | Reset `p1` and `p2` |
| `u` / `i` | `uniqueness_ratio` |
| `j` / `k` | `speckle_window_size` |
| `n` / `m` | `speckle_range` |
| `h` / `l` | `pre_filter_cap` |
| `,` / `.` | `disp12_max_diff` |
| `9` / `0` | `mode` |
| `p` | Print the current parameter set |
