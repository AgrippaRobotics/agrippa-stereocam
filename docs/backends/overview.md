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

## Post-processing

Both backends share a post-processing pipeline that runs after disparity
computation.  Each stage is optional and controlled by CLI flags on
`depth-preview-classical`:

- **Specular masking** — invalidates pixels under saturated highlights.
- **Median filter** — removes salt-and-pepper outliers.
- **Morphological cleanup** — close-then-open to fill holes and remove blobs.
- **WLS filter** — edge-preserving smoothing guided by the left image (OpenCV
  ximgproc, classical backend only).
- **Temporal median filter** — multi-frame de-noising with scene-change
  detection.
- **Confidence map** — per-pixel quality overlay combining texture strength and
  disparity variance.

Pre-processing options include CLAHE histogram equalisation for low-texture
surfaces and dynamic disparity range from real-world depth bounds.

See [depth-preview-classical](../cli/depth-preview-classical.md) for flag
details and pipeline ordering.

## Export notebooks

Export notebooks live in `backends/`:

- `1.Export_IGEV.ipynb`
- `1b.Export_RT-IGEV.ipynb`
- `2.Export_FoundationStereo.ipynb`

See [igev-setup.md](igev-setup.md) for the concrete IGEV++ workflow.
