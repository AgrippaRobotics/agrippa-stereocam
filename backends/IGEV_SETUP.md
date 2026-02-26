# IGEV++ Backend Setup

Step-by-step guide to getting the IGEV++ neural stereo backend running with `ag-cam-tools depth-preview`.

## Overview

The pipeline has two phases:

1. **Export** (one-time, on a dev machine with PyTorch): clone IGEV++, download a checkpoint, convert it to ONNX.
2. **Inference** (on target hardware): install `libonnxruntime`, rebuild, and run `depth-preview --stereo-backend onnx` with the exported `.onnx` file.

Export requires PyTorch + CUDA and is done once per calibration session (because `max_disp` is baked into the ONNX graph). Inference uses the ONNX Runtime C API — no Python needed at runtime.

---

## Phase 1: ONNX Export

### 1. Clone the IGEV++ repository

```bash
cd ~/code   # or wherever you keep third-party repos
git clone https://github.com/gangweiX/IGEV-plusplus.git
```

This gives you `~/code/IGEV-plusplus/` with model code in `core/igev_stereo.py`.

### 2. Download a pretrained checkpoint

Pretrained weights are on Google Drive:
<https://drive.google.com/drive/folders/1eubNsu03MlhUfTtrbtN7bfAsl39s2ywJ>

Download the SceneFlow checkpoint — it's the general-purpose one:

```bash
mkdir -p ~/code/IGEV-plusplus/pretrained_models/igev_plusplus
# Download sceneflow.pth into that directory.
# (Google Drive doesn't support wget — use a browser or gdown.)
pip install gdown
gdown --fuzzy "https://drive.google.com/..." \
    -O ~/code/IGEV-plusplus/pretrained_models/igev_plusplus/sceneflow.pth
```

### 3. Create the export Python environment

The export notebook needs PyTorch, the `timm` version IGEV++ was built against, and ONNX tooling.

```bash
cd backends
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements-onnx-export.txt
python -m ipykernel install --user --name agrippa-export --display-name "agrippa-export"
```

`requirements-onnx-export.txt` pins:
- `torch>=1.12` (CPU is fine for export)
- `timm==0.5.4` (must match — IGEV++ uses its MobileNetV2)
- `onnx`, `onnxsim`, `onnxruntime`, `numpy`, `Pillow`

### 4. Run the export notebook

Open `backends/1.Export_IGEV.ipynb` in Jupyter, select the **agrippa-export** kernel, and run all cells. The notebook will:

- Read `calibration_meta.json` from your calibration session to get `num_disparities` → sets `max_disp` for the IGEV++ cost volume.
- Read remap table dimensions to determine the export resolution.
- Patch out `torch.cuda.amp.autocast` so tracing works on CPU.
- Export at ONNX opset 16, simplify with `onnxsim`, validate with `onnxruntime`.

**The export is tied to a specific resolution and max_disp.** If you recalibrate or change binning, re-run the notebook.

Configuration cells at the top let you override `max_disp`, `width`, `height`, and `iters`.

---

## Phase 2: Inference on Target

The ONNX backend runs in-process via the ONNX Runtime C API. No Python is needed at runtime.

### 6. Install ONNX Runtime

On the target machine (Jetson, ARM Linux, macOS, etc.):

| Platform | Install command |
|----------|----------------|
| macOS (Homebrew) | `brew install onnxruntime` |
| Ubuntu/Debian | Install from [ONNX Runtime releases](https://github.com/microsoft/onnxruntime/releases) or build from source |
| Jetson (NVIDIA) | Download the aarch64 GPU package from ONNX Runtime releases and set `ONNXRUNTIME_HOME` |

For Jetson or manual installs, set the environment variable before building:

```bash
export ONNXRUNTIME_HOME=/path/to/onnxruntime
```

The build system auto-detects ONNX Runtime via `pkg-config` or `ONNXRUNTIME_HOME`.

### 7. Rebuild ag-cam-tools

```bash
make clean && make
```

The build output should show `HAVE_ONNXRUNTIME=1`. If not, check that `pkg-config --libs libonnxruntime` works or that `ONNXRUNTIME_HOME` is set.

### 8. Copy the ONNX model to the target

```bash
scp models/igev_plusplus.onnx jetson:~/agrippa-stereocam/models/
```

### 9. Run depth-preview with IGEV++

```bash
ag-cam-tools depth-preview \
    --rectify calibration/calibration_YYYYMMDD_HHMMSS \
    --stereo-backend onnx \
    --model-path models/igev_plusplus.onnx
```

The tool will:
1. Load the ONNX model in-process via the ONNX Runtime C API
2. Auto-select the best execution provider (CUDA > CoreML > CPU)
3. Run a warm-up inference pass
4. Display side-by-side: rectified left eye | JET disparity colourmap

The first frame is slow (ONNX Runtime JIT warm-up). Subsequent frames run at model speed.

You can also use `igev` or `foundation` as aliases for `onnx` on the CLI.

---

## Troubleshooting

### "Cannot import IGEVStereo"

The export notebook needs the IGEV++ source tree on `sys.path`. Make sure `igev_src` in the configuration cell points to the root of the cloned repo (the directory containing `core/`).

### "CUDA out of memory" during export

ONNX export traces on CPU by default. If PyTorch is picking up a GPU, pass `CUDA_VISIBLE_DEVICES=""`:

Set `CUDA_VISIBLE_DEVICES=""` in your shell before launching Jupyter, or add it to the notebook's first code cell.

### "timm version mismatch"

IGEV++ uses `timm==0.5.4` for its MobileNetV2 backbone. Newer timm versions renamed internal modules. Pin exactly `timm==0.5.4`.

### Slow inference on Jetson

- Make sure ONNX Runtime was built with CUDA support
- Check the startup log for `using CUDA execution provider`
- Reduce `--iters` at export time (8 instead of 12)
- Consider the RT-IGEV variant (real-time, lower quality) — requires a separate export script

### Model resolution mismatch

The ONNX model is exported at a fixed resolution. The C backend automatically pads inputs to the nearest multiple of 32, so small mismatches are handled. However, if your camera frame dimensions (after binning) differ significantly from the export resolution, re-export with `--width` and `--height` matching your setup.

### "onnx: model has N inputs (expected >= 2)"

The ONNX backend expects a stereo model with at least two inputs (left and right images). Make sure you're using a stereo disparity model, not a monocular depth model.

---

## File Reference

| File | Purpose |
|------|---------|
| `backends/1.Export_IGEV.ipynb` | Interactive IGEV++ to ONNX export notebook |
| `backends/2.Export_FoundationStereo.ipynb` | Interactive FoundationStereo to ONNX export notebook |
| `backends/requirements-onnx-export.txt` | Export-time Python deps (torch, timm, onnx, etc.) |
| `src/stereo_onnx.c` | In-process ONNX Runtime C backend (built when `HAVE_ONNXRUNTIME=1`) |
| `src/stereo.h` | Stereo API header (defines `AgOnnxParams`, `ag_onnx_*` functions) |
