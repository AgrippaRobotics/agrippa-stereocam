# IGEV++ Setup

This guide covers the current IGEV++ export and runtime workflow for `ag-cam-tools`.

## Two phases

1. Export the PyTorch checkpoint to ONNX on a development machine
2. Run the exported ONNX model through the in-process ONNX Runtime backend on the target machine

The export is tied to the calibration resolution and disparity range.

## Phase 1: export to ONNX

### Clone the IGEV++ repo

```bash
cd ~/code
git clone https://github.com/gangweiX/IGEV-plusplus.git
```

### Download a checkpoint

Use the SceneFlow checkpoint from the upstream project and place it under:

```text
~/code/IGEV-plusplus/pretrained_models/igev_plusplus/
```

### Create the export environment

```bash
cd backends
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements-onnx-export.txt
python -m ipykernel install --user --name agrippa-export --display-name "agrippa-export"
```

### Run the notebook

Open `backends/1.Export_IGEV.ipynb` and run it with the `agrippa-export` kernel.

The notebook:

- reads calibration metadata,
- determines the export resolution,
- exports ONNX at a fixed shape,
- simplifies and validates the result.

If the calibration changes materially, re-export the model.

## Phase 2: runtime inference

### Install ONNX Runtime

The runtime build can discover ONNX Runtime either through `pkg-config` or `ONNXRUNTIME_HOME`.

```bash
export ONNXRUNTIME_HOME=/path/to/onnxruntime
```

### Rebuild the tool

```bash
make clean && make
```

The build should indicate that ONNX Runtime support is enabled.

### Run the model

```bash
ag-cam-tools depth-preview-neural \
    -a 192.168.0.201 \
    -A \
    -r calibration/calibration_YYYYMMDD_HHMMSS \
    --stereo-backend onnx \
    --model-path models/igev_plusplus.onnx
```

## Troubleshooting highlights

- If the notebook cannot import IGEV modules, check the configured source path.
- If export fails with CUDA memory issues, force CPU export.
- If inference is slow on Jetson, confirm the CUDA execution provider is actually active.
- If the ONNX model resolution differs from the runtime image size, re-export to match the calibrated setup.
