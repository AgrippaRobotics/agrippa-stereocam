# `depth-capture`

Capture a single stereo frame, compute disparity using StereoSGBM, and write an RGBA PNG where RGB channels carry the camera colour and the alpha channel encodes normalised depth.

The output is center-cropped to a square and sized to a multiple of 32 pixels, ready for direct consumption by ML training pipelines.

## Examples

```bash
ag-cam-tools depth-capture -a 192.168.0.201 --calibration-local calibration/calibration_20260225_143015_a1b2c3d4 --max-depth 500
ag-cam-tools depth-capture -a 192.168.0.201 --calibration-slot 0 --max-depth 300 --size 640
ag-cam-tools depth-capture -a 192.168.0.201 -A --calibration-slot 0 --max-depth 500 -o ./depth_frames
ag-cam-tools depth-capture -a 192.168.0.201 --calibration-local ./calib --max-depth 200 -b 2
```

## Options

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-o`, `--output` | Output directory, defaulting to the current working directory |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB |
| `-A`, `--auto-expose` | Auto-expose and then lock |
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes |
| `--calibration-local` | Calibration session directory on disk (required) |
| `--calibration-slot` | On-camera calibration slot: `0`, `1`, or `2` (required) |
| `--max-depth` | Maximum depth for alpha normalisation, in centimetres (required) |
| `--size` | Output side length in pixels; must be a positive multiple of 32 (default: auto) |
| `-v`, `--verbose` | Print diagnostic register readback |

## Output format

The output is a single RGBA PNG file:

- **R, G, B** — camera colour from the rectified left eye. For grayscale input (e.g. `--binning 2`), the grayscale value is replicated across all three channels.
- **A (alpha)** — depth normalised to `[0, 255]`:
  - `alpha = clamp(depth_cm * 255 / max_depth_cm, 0, 255)`
  - Invalid or occluded pixels (where disparity yields no depth) get `alpha = 0`.

## Square crop and sizing

The raw rectified image (typically 1440 x 1080) is center-cropped to the largest inscribed square, then resized to a multiple of 32 pixels.

- **Default**: the output side is `floor(square_side / 32) * 32`. For a 1080-tall image, this produces a 1056 x 1056 output.
- **`--size N`**: overrides the automatic sizing. `N` must be a positive multiple of 32. The image is resized using nearest-neighbour interpolation.

## Calibration requirement

Calibration is mandatory. Supply either `--calibration-local` or `--calibration-slot` (mutually exclusive). The calibration must include:

- Stereo rectification remap tables (for disparity computation).
- `focal_length_px` and `baseline_cm` in the calibration metadata (for disparity-to-depth conversion).

## Notes

- The disparity backend is SGBM (classical). SGBM parameters are seeded from calibration metadata (`min_disparity`, `num_disparities`).
- `-A` is mutually exclusive with explicit `-x` and `-g`.
- When the camera reports that post-binning data is no longer Bayer, the tool treats the input as grayscale and replicates it into RGB.
