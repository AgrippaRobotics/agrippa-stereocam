# `calibration-stash`

Manage calibration data stored in the camera's persistent UserFile storage.

This allows multiple hosts to use the same on-camera calibration through `--rectify device://` without keeping local copies synchronized.

## Examples

```bash
ag-cam-tools calibration-stash list
ag-cam-tools calibration-stash upload --slot 0 calibration/session_a1b2c3d4
ag-cam-tools calibration-stash upload --slot 2 calibration/session_d4e5f6g7
ag-cam-tools calibration-stash download --slot 0 -o /tmp/dl
ag-cam-tools calibration-stash delete --slot 1
ag-cam-tools calibration-stash purge
```

## Actions

| Action | Description |
|--------|-------------|
| `list` | Show storage usage and slot contents |
| `upload` | Pack a calibration session and write it to a slot |
| `download` | Download a slot to a local directory |
| `delete` | Remove a single slot |
| `purge` | Delete the entire calibration file |

## Options

| Option | Description |
|--------|-------------|
| `--slot` | Calibration slot: `0`, `1`, or `2` |
| `-o`, `--output` | Output directory, required for `download` |
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |

## Storage format notes

The upload path reads:

- `remap_left.bin`
- `remap_right.bin`
- `calibration_meta.json`

from the session's `calib_result/` directory.

Remap tables are compacted from 4-byte to 3-byte offsets for storage efficiency and expanded back to the standard 4-byte format on download.
