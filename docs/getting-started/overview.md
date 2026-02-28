# Getting Started Overview

`ag-cam-tools` is a C/C++ toolkit for operating a Lucid Phoenix PHD016S stereo camera from the command line. It focuses on practical acquisition and bring-up tasks rather than a general SDK abstraction.

## Typical workflow

1. Install the build dependencies and compile the tool.
2. Confirm the camera is visible with `ag-cam-tools list`.
3. Connect to a device and capture a frame.
4. Bring the optics into focus with `focus`.
5. Capture a calibration session.
6. Run the offline notebooks to produce rectification artifacts.
7. Use `stream`, `depth-preview-classical`, or `depth-preview-neural` against the calibration output.

## Documentation map

- Build instructions: [installation.md](installation.md)
- First commands to run: [quickstart.md](quickstart.md)
- Full command reference: [../cli/overview.md](../cli/overview.md)
- Mechanical and electrical bring-up: [../workflows/bring-up.md](../workflows/bring-up.md)
- Calibration workflow: [../workflows/calibration.md](../workflows/calibration.md)
