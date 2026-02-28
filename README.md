# agrippa-stereocam

Stereo camera toolkit for the [Lucid Phoenix PHD016S](https://thinklucid.com/product/phoenix-1-6mp-dual-head-model-imx273/) dual-head GigE Vision camera. It covers camera discovery, capture, live preview, focus alignment, calibration workflows, and classical or neural depth preview.

Documentation now lives in the dedicated docs site:

- Repo docs: [docs/index.md](docs/index.md)
- GitHub Pages site: <https://agripparobotics.github.io/agrippa-stereocam/>

## What this repo provides

`ag-cam-tools` currently includes:

- `list` for GigE camera discovery
- `connect` for device inspection
- `capture` for stereo frame export
- `stream` for live preview, rectification, and AprilTag detection
- `focus` for manual lens alignment with live focus scoring
- `calibration-capture` for stereo dataset collection
- `depth-preview-classical` for rectified disparity with SGBM controls
- `depth-preview-neural` for ONNX-based stereo inference
- `calibration-stash` for storing calibration sessions on-camera

## Build

Core dependencies:

- [Aravis 0.8](https://github.com/AravisProject/aravis)
- [SDL2](https://www.libsdl.org/)
- `pkg-config`

Optional dependencies:

- `apriltag` for AprilTag detection in `stream` or the vendored fallback in `vendor/apriltag`
- OpenCV 4 for the `sgbm` stereo backend
- ONNX Runtime for the `onnx` stereo backend

```bash
git submodule update --init --recursive
make
sudo make install
```

More detailed setup is in [docs/getting-started/installation.md](docs/getting-started/installation.md).

## Quick start

```bash
ag-cam-tools list
ag-cam-tools connect -a 192.168.0.201
ag-cam-tools capture -a 192.168.0.201 -e png -o ./frames
ag-cam-tools stream -a 192.168.0.201 -A
```

For command-specific examples and options, use the CLI docs:

- [docs/cli/overview.md](docs/cli/overview.md)
- [docs/cli/capture.md](docs/cli/capture.md)
- [docs/cli/stream.md](docs/cli/stream.md)
- [docs/cli/focus.md](docs/cli/focus.md)
- [docs/cli/calibration-stash.md](docs/cli/calibration-stash.md)

## Calibration and backends

Offline calibration notebooks live under `calibration/`. ONNX export notebooks live under `backends/`.

- Calibration workflow: [docs/workflows/calibration.md](docs/workflows/calibration.md)
- Bring-up guide: [docs/workflows/bring-up.md](docs/workflows/bring-up.md)
- IGEV++ backend setup: [docs/backends/igev-setup.md](docs/backends/igev-setup.md)

## Testing

The repo has unit tests and hardware integration tests.

```bash
make test
make test-hw
make test-all
```

Detailed testing guidance is in [docs/workflows/testing.md](docs/workflows/testing.md).

## Shell completions

Bash and zsh completions are installed by `make install`.

```bash
source completions/ag-cam-tools.bash
source completions/ag-cam-tools.zsh
```

## Hardware snapshot

Platform: [Lucid Phoenix PHD 1.6 MP Dual Extended-Head (IMX273)](https://thinklucid.com/product/phoenix-1-6mp-dual-head-model-imx273/)

| Parameter | Value |
|-----------|-------|
| Baseline | 40 mm |
| Lens focal length | 3 mm |
| Sensor resolution | 1440 x 1080 px per head |
| Pixel size | 3.45 um |
| Interface | 1000BASE-T (GigE Vision) |
| Power | PoE or 12-24 VDC |

Additional hardware notes live in [docs/hardware/circuits.md](docs/hardware/circuits.md).

## Attribution

This project is licensed under Apache-2.0. Preserve [NOTICE](NOTICE) in redistributions and derivative works. Citation metadata is available in [CITATION.cff](CITATION.cff).
