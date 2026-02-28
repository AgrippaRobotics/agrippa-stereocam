# `capture`

Capture a single stereo frame pair and write it to disk.

## Examples

```bash
ag-cam-tools capture -a 192.168.0.201 -e png -o ./frames
ag-cam-tools capture -a 192.168.0.201 -e png -b 2 -x 50000 -v
ag-cam-tools capture -a 192.168.0.201 -A -e png -o ./frames
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
| `-v`, `--verbose` | Print diagnostic register readback |

## Notes

- `-A` is mutually exclusive with explicit `-x` and `-g`.
- `pgm` is the most direct single-channel output path.
- When the camera reports that post-binning data is no longer Bayer, the tool treats the output as grayscale and skips debayering.
