# `calibration-capture`

Interactive capture session for collecting a stereo calibration dataset.

## Examples

```bash
ag-cam-tools calibration-capture -a 192.168.0.201 -A
ag-cam-tools calibration-capture -a 192.168.0.201 -A -n 40
ag-cam-tools calibration-capture -a 192.168.0.201 -x 30000 -g 6 -o ./my_calib
ag-cam-tools calibration-capture -a 192.168.0.201 -A -b 2
ag-cam-tools calibration-capture -a 192.168.0.201 -q
```

## Options

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-o`, `--output` | Base output directory, default `./calibration` |
| `-n`, `--count` | Target number of stereo pairs |
| `-f`, `--fps` | Preview rate in Hz |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB |
| `-A`, `--auto-expose` | Auto-expose and then lock |
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes |
| `-q`, `--quiet-audio` | Disable the save confirmation beep |

## Output layout

Each session creates a unique output directory named like:

```text
calibration_<datetime>_<md5>
```

Within it, images are saved into:

- `stereoLeft/`
- `stereoRight/`

## Runtime behavior

- Press `s` to save the current pair.
- Press `q` or `Esc` to quit.
- The window title shows the running image count.
