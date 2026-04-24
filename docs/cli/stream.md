# `stream`

Show a live SDL stereo preview. This command is also the main entrypoint for real-time rectification and optional AprilTag detection.

## Examples

```bash
ag-cam-tools stream -a 192.168.0.201 -f 10
ag-cam-tools stream -a 192.168.0.201 -f 5 -b 2 -x 30000 -g 12
ag-cam-tools stream -a 192.168.0.201 -A -f 10
ag-cam-tools stream -a 192.168.0.201 -t 0.05
ag-cam-tools stream -a 192.168.0.201 --calibration-local calibration/calibration_20260225_143015_a1b2c3d4
ag-cam-tools stream -a 192.168.0.201 --calibration-slot 0
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
| `--calibration-local` | Calibration session directory on disk |
| `--calibration-slot` | On-camera calibration slot: `0`, `1`, or `2` |
| `-t`, `--tag-size` | AprilTag size in meters |

## Rectification

Supplying `--calibration-local` or `--calibration-slot` enables stereo rectification. The two options are mutually exclusive.

- `--calibration-local` loads remap tables from a calibration session directory on the local filesystem.
- `--calibration-slot` loads remap tables from a numbered slot (0-2) stored on the camera via `calibration-stash upload`.

On ARM64 platforms, the remap path uses NEON acceleration.

## Sensor modes

`stream` supports both stereo Lucid heads (e.g. PDH016S, DualBayerRG8) and monocular Lucid GigE cameras (e.g. Triton TRT016S, BayerRG8 / Mono8). The mode is auto-detected at camera open by probing the device's available pixel formats — `DualBayerRG8` advertises stereo, anything else falls back to mono.

| Mode | Window title | Recording | Rectification |
|------|--------------|-----------|---------------|
| stereo | `Stereo Stream` | Side-by-side / top-bottom / separate per `--record-layout` | Supported via `--calibration-*` |
| mono | `Mono Stream` | Single-frame MP4 (mono layout, ignores `--record-layout`) | Not supported — calibration flags rejected |

When `--record-layout` is set on a mono camera, a warning is printed and the recording falls back to mono layout. AprilTag detection runs on the single image instead of left/right eyes.

## Notes

- Press `q` or `Esc` to quit.
- When AprilTag detection is enabled, detections are printed to stdout per frame and per eye.
- When the camera reports that post-binning data is no longer Bayer, the preview switches to grayscale and skips debayering.
