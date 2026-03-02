# Agrippa Stereo Camera Docs

This site is the canonical documentation for `ag-cam-tools` and the supporting Agrippa stereo camera workflows.

Use it for:

- build and installation guidance,
- CLI reference by command,
- bring-up and calibration workflows,
- backend setup for classical and neural stereo,
- testing and maintenance notes.

## Start here

- [Getting Started / Overview](getting-started/overview.md)
- [Installation](getting-started/installation.md)
- [Quick Start](getting-started/quickstart.md)
- [CLI Overview](cli/overview.md)
- [Bring-Up Workflow](workflows/bring-up.md)
- [Calibration Workflow](workflows/calibration.md)

## Main capabilities

`ag-cam-tools` currently supports:

- **Discovery and connection** — find and connect to Lucid GigE stereo cameras on the network, with serial, address, and NIC selection.
- **Stereo capture** — single-frame stereo pair capture with optional rectification.
- **Live preview** — real-time SDL stereo display with optional AprilTag detection and rectification overlay.
- **Focus tuning** — live per-eye focus scoring with selectable metrics (Laplacian, Tenengrad, Brenner), region-of-interest control, and procedural stereo audio feedback for hands-free lens alignment.
- **Calibration capture** — interactive stereo pair acquisition for calibration datasets, with audio confirmation cues.
- **Depth preview** — rectified disparity visualization with classical (SGBM) and neural (ONNX) stereo backends, plus live parameter tuning.
- **Depth capture** — export RGBA PNGs with camera colour in RGB and normalised depth in the alpha channel, square-cropped and sized for ML training pipelines.
- **Calibration storage** — upload, download, and manage calibration archives directly on the camera.
- **Camera reset** — power-cycle cameras over GigE with `bounce`.
- **Binning-aware pipeline** — automatic grayscale handling when `2x2` sensor binning collapses the Bayer pattern.

## Project links

- Repository: <https://github.com/AgrippaRobotics/agrippa-stereocam>
- Root README: [../README.md](../README.md)
