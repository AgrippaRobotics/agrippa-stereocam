# Backend Overview

Stereo disparity in this repo can be produced through either a classical algorithm or an ONNX-based neural model.

## Classical backend

The classical path uses OpenCV StereoSGBM when OpenCV 4 is available at build time.

Use:

- `depth-preview-classical` for live tuning and inspection,
- `--stereo-backend sgbm` when you want the OpenCV path explicitly.

## Neural backend

The neural path uses ONNX Runtime in-process through the C API.

Use:

- `depth-preview-neural`,
- `--stereo-backend onnx`,
- or the `igev` and `foundation` aliases.

The tool picks the best execution provider available at runtime.

## Export notebooks

Export notebooks live in `backends/`:

- `1.Export_IGEV.ipynb`
- `1b.Export_RT-IGEV.ipynb`
- `2.Export_FoundationStereo.ipynb`

See [igev-setup.md](igev-setup.md) for the concrete IGEV++ workflow.
