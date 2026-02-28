# `focus`

Live focus-scoring tool for manual lens adjustment. It overlays per-eye focus metrics on the live stream and can generate procedural audio feedback while the two channels converge.

## Examples

```bash
ag-cam-tools focus -a 192.168.0.201
ag-cam-tools focus -a 192.168.0.201 --metric tenengrad
ag-cam-tools focus -a 192.168.0.201 --roi 200 200 600 400
ag-cam-tools focus -a 192.168.0.201 -x 30000 -g 6 -b 2
ag-cam-tools focus -a 192.168.0.201 -A -b 2
ag-cam-tools focus -a 192.168.0.201 -q
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
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes |
| `-q`, `--quiet-audio` | Disable audio feedback |
| `-m`, `--metric` | Focus metric: `laplacian`, `tenengrad`, or `brenner` (default: `laplacian`) |
| `--roi` | Region of interest as `x y w h` pixels |

## Focus metrics

Three metrics are available, selectable via `--metric` or by pressing `M` at runtime:

| Metric | Algorithm | Notes |
|--------|-----------|-------|
| `laplacian` | Variance of 3x3 Laplacian response | Default. Sensitive to fine detail; may fluctuate more with sensor noise. |
| `tenengrad` | Mean squared Sobel gradient magnitude | More robust to noise than Laplacian. Good general-purpose choice. |
| `brenner` | Mean squared two-pixel horizontal difference | Fastest. Less sensitive to noise but also less sensitive to fine texture. |

All metrics return higher values for sharper focus and are normalized by the number of evaluated pixels, so scores are comparable across ROI sizes and binning modes.

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `M` | Cycle through focus metrics (laplacian, tenengrad, brenner) |
| `Q` / `Esc` | Quit |

Switching metrics at runtime resets the moving-average history so the new metric's scores stabilize within a few frames.

## What the operator sees

The SDL window shows:

- the live stereo preview,
- green ROI rectangles,
- the selected metric name,
- smoothed left and right focus scores,
- a left-right mismatch percentage,
- and a lock state line (`LOCKED`, `ALIGNING`, or `LOW DETAIL`).

When both left and right scores are very low (below the low-detail threshold), the tool reports `LOW DETAIL` instead of `LOCKED` or `ALIGNING`. This prevents two equally weak scores from being mistakenly treated as a good focus lock. Point the camera at a more textured target to get a meaningful reading.

## Audio behavior

By default the command also emits procedural stereo audio:

- while the two channels differ, a beating convergence tone is played,
- once focus is considered aligned, alternating confirmation beeps are played.

The detailed audio design is documented in [../workflows/focus-audio.md](../workflows/focus-audio.md).
