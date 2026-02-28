# `stream`

Show a live SDL stereo preview. This command is also the main entrypoint for real-time rectification and optional AprilTag detection.

## Examples

```bash
ag-cam-tools stream -a 192.168.0.201 -f 10
ag-cam-tools stream -a 192.168.0.201 -f 5 -b 2 -x 30000 -g 12
ag-cam-tools stream -a 192.168.0.201 -A -f 10
ag-cam-tools stream -a 192.168.0.201 -t 0.05
ag-cam-tools stream -a 192.168.0.201 -r calibration/calibration_20260225_143015_a1b2c3d4
ag-cam-tools stream -a 192.168.0.201 -r device://
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
| `-r`, `--rectify` | Calibration session path or `device://` |
| `-t`, `--tag-size` | AprilTag size in meters |

## Rectification sources

When `--rectify` is given, the command loads precomputed remap tables from either:

- a calibration session directory on disk,
- or `device://` to read the calibration archive stored on the camera.

On ARM64 platforms, the remap path uses NEON acceleration.

## Runtime behavior

- Press `q` or `Esc` to quit.
- When AprilTag detection is enabled, detections are printed to stdout per frame and per eye.
