# `depth-preview-neural`

Live stereo depth preview intended for ONNX-based stereo models. It uses the same rectification and display pipeline as the classical preview, but is aimed at neural backends.

## Examples

```bash
ag-cam-tools depth-preview-neural -a 192.168.0.201 -A -r calibration/calibration_20260225_143015_a1b2c3d4 --stereo-backend onnx --model-path model.onnx
ag-cam-tools depth-preview-neural -a 192.168.0.201 -A -r device:// --stereo-backend igev
```

## Backend summary

| Backend | Engine | Build requirement |
|---------|--------|-------------------|
| `sgbm` | OpenCV StereoSGBM | `pkg-config opencv4` |
| `onnx` | ONNX Runtime C API | `pkg-config libonnxruntime` or `ONNXRUNTIME_HOME` |

The CLI also accepts `igev` and `foundation` as aliases for `onnx`.

## Notes

- `depth-preview-neural` uses the same major CLI options as `depth-preview-classical`.
- Runtime SGBM tuning keys are not enabled in this command.
- The ONNX backend automatically picks the best execution provider available at runtime, preferring CUDA and CoreML over CPU when present.

See [../backends/igev-setup.md](../backends/igev-setup.md) for model export and ONNX runtime setup.
