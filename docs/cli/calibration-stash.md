# `calibration-stash`

Manage calibration data stored in the camera's persistent UserFile storage.

This allows multiple hosts to use the same on-camera calibration through `--calibration-slot` without keeping local copies synchronized.

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

The upload path reads the following files from the session's `calib_result/` directory:

- `calibration_meta.json`
- `cam_mats_left.npy` / `cam_mats_right.npy` — 3x3 camera matrices (K)
- `dist_coefs_left.npy` / `dist_coefs_right.npy` — distortion coefficients
- `rect_trans_left.npy` / `rect_trans_right.npy` — 3x3 rectification transforms (R)
- `proj_mats_left.npy` / `proj_mats_right.npy` — 3x4 projection matrices (P)

These 9 files (~2 KB total) are packed into an AGCAL archive, compressed with zlib, and wrapped in an AGST envelope with a 4 KB JSON metadata header.

On download or load, remap tables are computed on the fly from the stored matrices (equivalent to `cv::initUndistortRectifyMap` with nearest-neighbor interpolation). Image dimensions are read from `calibration_meta.json`.
