# `focus`

Live focus-scoring tool for manual lens adjustment. It overlays per-eye focus metrics on the live stream and can generate procedural audio feedback while the two channels converge.

## Examples

```bash
ag-cam-tools focus -a 192.168.0.201
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
| `--roi` | Region of interest as `x y w h` pixels |

## What the operator sees

The SDL window shows:

- the live stereo preview,
- green ROI rectangles,
- smoothed left and right focus scores,
- a left-right mismatch percentage,
- and a lock state line.

The focus score uses a variance-of-Laplacian metric over the configured ROI. Higher values indicate sharper focus.

## Audio behavior

By default the command also emits procedural stereo audio:

- while the two channels differ, a beating convergence tone is played,
- once focus is considered aligned, alternating confirmation beeps are played.

The detailed audio design is documented in [../workflows/focus-audio.md](../workflows/focus-audio.md).
