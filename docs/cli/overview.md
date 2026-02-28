# CLI Overview

The binary entrypoint is:

```text
ag-cam-tools <command> [options]
```

## Commands

| Command | Purpose |
|---------|---------|
| `list` | Discover GigE cameras on the network |
| `connect` | Connect to a camera and print device information |
| `capture` | Capture a single stereo frame pair |
| `stream` | Show a live stereo preview |
| `focus` | Help align manual-focus lenses with live focus scoring |
| `calibration-capture` | Capture stereo pairs for calibration |
| `depth-preview-classical` | Show rectified disparity with classical stereo |
| `depth-preview-neural` | Show rectified disparity with ONNX stereo |
| `calibration-stash` | Store and retrieve calibration archives on-camera |

## Common device selection options

Most commands accept:

- `-s`, `--serial` to select a camera by serial number
- `-a`, `--address` to select a camera by IP address
- `-i`, `--interface` to force a specific NIC
- `-p`, `--packet-size` to override automatic GigE packet negotiation when supported

## Command pages

- [list](list.md)
- [connect](connect.md)
- [capture](capture.md)
- [stream](stream.md)
- [focus](focus.md)
- [calibration-capture](calibration-capture.md)
- [depth-preview-classical](depth-preview-classical.md)
- [depth-preview-neural](depth-preview-neural.md)
- [calibration-stash](calibration-stash.md)
