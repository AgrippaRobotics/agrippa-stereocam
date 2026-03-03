# `capture`

Capture a single stereo frame pair and write it to disk. With `--burst N`, captures N pairs in rapid succession from a single trigger.

## Examples

```bash
ag-cam-tools capture -a 192.168.0.201 -e png -o ./frames
ag-cam-tools capture -a 192.168.0.201 -e png -b 2 -x 50000 -v
ag-cam-tools capture -a 192.168.0.201 -A -e png -o ./frames
ag-cam-tools capture -a 192.168.0.201 -A -e png --calibration-local calibration/calibration_20260225_143015_a1b2c3d4
ag-cam-tools capture -a 192.168.0.201 -A -e png --calibration-slot 0
ag-cam-tools capture -a 192.168.0.201 --burst 10 -e png -o ./frames
ag-cam-tools capture -a 192.168.0.201 --burst 5 -A -e png -o ./frames
```

## Options

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-o`, `--output` | Output directory, defaulting to the current working directory |
| `-e`, `--encode` | Output format: `pgm`, `png`, or `jpg` |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB |
| `-A`, `--auto-expose` | Auto-expose and then lock |
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes |
| `--burst` | Burst capture N frames in rapid succession (`2`-`100`) |
| `--calibration-local` | Calibration session directory on disk |
| `--calibration-slot` | On-camera calibration slot: `0`, `1`, or `2` |
| `-v`, `--verbose` | Print diagnostic register readback |

## Burst Mode

The `--burst N` option captures N stereo frame pairs in rapid succession from a single software trigger. Frames are delivered at the camera's maximum acquisition frame rate, using the PDH016S FrameBurstStart trigger selector.

Burst output is written to a timestamped subdirectory under the output directory:

```
frames/burst_20260301_143015/frame_000_left.png
frames/burst_20260301_143015/frame_000_right.png
frames/burst_20260301_143015/frame_001_left.png
frames/burst_20260301_143015/frame_001_right.png
...
```

The burst count must be between 2 and 100.

When `--burst` is combined with `-A` (`--auto-expose`), exposure is settled and locked before the burst begins. All frames in the burst use the same locked exposure and gain values.

Rectification via `--calibration-local` or `--calibration-slot` is supported with burst mode.

## Rectification

Supplying `--calibration-local` or `--calibration-slot` enables stereo rectification on the captured pair. The two options are mutually exclusive.

- `--calibration-local` loads remap tables from a calibration session directory on the local filesystem.
- `--calibration-slot` loads remap tables from a numbered slot (0-2) stored on the camera via `calibration-stash upload`.

## Notes

- `-A` is mutually exclusive with explicit `-x` and `-g`.
- `pgm` is the most direct single-channel output path.
- When the camera reports that post-binning data is no longer Bayer, the tool treats the output as grayscale and skips debayering.
