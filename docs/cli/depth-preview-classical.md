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
