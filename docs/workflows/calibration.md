# Calibration Workflow

Offline stereo calibration is performed in the notebooks under `calibration/`.

## Notebooks

| Notebook | Purpose |
|----------|---------|
| `1.Acquiring_Calibration_Images.ipynb` | Live capture via Aravis as an alternative to `calibration-capture` |
| `2.Calibration.ipynb` | Corner detection, stereo calibration, rectification map export |
| `3.Depthmap_with_Tuning_Bar.ipynb` | Interactive disparity experimentation in notebook form |

## Notebook environment setup

```bash
cd calibration
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python -m ipykernel install --user --name agrippa-calibration
```

## Calibration image capture guidance

The quality of the capture set matters more than small solver tweaks.

Use:

- a flat checkerboard with known square size,
- even, diffuse lighting,
- fixed exposure and gain,
- consistent focus for the entire sequence,
- broad coverage of the sensor, including the corners.

Recommended capture count:

- minimum: 15-20 usable stereo pairs
- preferred: 25-40 diverse pairs

## `2.Calibration.ipynb` workflow

The main notebook performs:

1. checkerboard corner detection with subpixel refinement,
2. independent intrinsic calibration for the left and right cameras,
3. stereo calibration using the intrinsic estimates as the initial guess,
4. per-pair reprojection error analysis and outlier rejection,
5. stereo rectification and export of remap tables,
6. epipolar error validation on the rectified pair set,
7. estimation of recommended disparity search parameters,
8. export of `calibration_meta.json`.

## Output

Each calibration session produces a `calib_result/` directory containing:

- `.npy` calibration artifacts,
- `remap_left.bin` and `remap_right.bin` for the C runtime,
- `calibration_meta.json` with summary metadata and quality metrics.

## Runtime integration

Those outputs are used by:

- `ag-cam-tools stream --rectify <session>`
- `ag-cam-tools depth-preview-classical --rectify <session>`
- `ag-cam-tools depth-preview-neural --rectify <session>`
- `ag-cam-tools calibration-stash upload <session>`

You can also store the result on-camera and load it later with `device://`.
