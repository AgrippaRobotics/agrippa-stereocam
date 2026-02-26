# agrippa-stereocam

Stereo camera toolkit for the [Lucid Phoenix PHD016S](https://thinklucid.com/product/phoenix-1-6mp-dual-head-model-imx273/) dual-head GigE Vision camera. Provides camera discovery, single-frame capture with PNG/JPEG/PGM output, and real-time stereo preview via SDL2.

## Building

Dependencies: [Aravis 0.8](https://github.com/AravisProject/aravis), [SDL2](https://www.libsdl.org/), pkg-config.
AprilTag detection in `stream` uses apriltag3 via:
- system install (`pkg-config apriltag`) if available, or
- vendored submodule fallback at `vendor/apriltag`.

OpenCV StereoSGBM backend for `depth-preview` uses OpenCV 4 via:
- system install (`pkg-config opencv4`) — optional, enables `--stereo-backend sgbm`.

ONNX Runtime backend for `depth-preview` uses ONNX Runtime via:
- system install (`pkg-config libonnxruntime`) — optional, enables `--stereo-backend onnx`.
- or set `ONNXRUNTIME_HOME=/path/to/onnxruntime` for manual installs (e.g. Jetson).

```bash
git submodule update --init --recursive
make                # builds bin/ag-cam-tools
sudo make install   # installs binary + shell completions to /usr/local
```

## Usage

```
ag-cam-tools <command> [options]
```

### Commands

| Command | Description |
|---------|-------------|
| `list` | Discover and list GigE cameras on the network |
| `connect` | Connect to a camera and print device info |
| `capture` | Capture a single stereo frame pair |
| `stream` | Real-time stereo preview via SDL2 |
| `focus` | Real-time focus scoring for lens adjustment |
| `calibration-capture` | Interactive stereo pair capture for calibration |
| `depth-preview` | Live depth map with selectable stereo backend |

### `list`

```bash
ag-cam-tools list                      # ASCII table of discovered cameras
ag-cam-tools list --machine-readable   # tab-separated output for scripting
ag-cam-tools list -i en0               # restrict discovery to a specific NIC
```

### `connect`

```bash
ag-cam-tools connect -a 192.168.0.201   # connect by IP
ag-cam-tools connect -s PDH016S-001     # connect by serial
ag-cam-tools connect                    # interactive picker
```

### `capture`

```bash
ag-cam-tools capture -a 192.168.0.201 -e png -o ./frames
ag-cam-tools capture -a 192.168.0.201 -e png -b 2 -x 50000 -v
ag-cam-tools capture -a 192.168.0.201 -A -e png -o ./frames   # auto-expose then capture
```

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-o`, `--output` | Output directory (default: current) |
| `-e`, `--encode` | Output format: `pgm`, `png`, or `jpg` |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB (0-48; 0-24 analog, 24-48 digital) |
| `-A`, `--auto-expose` | Auto-expose/gain settle then lock (mutually exclusive with `-x`/`-g`) |
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes (default: auto-negotiate) |
| `-v`, `--verbose` | Print diagnostic register readback |

### `stream`

```bash
ag-cam-tools stream -a 192.168.0.201 -f 10
ag-cam-tools stream -a 192.168.0.201 -f 5 -b 2 -x 30000 -g 12
ag-cam-tools stream -a 192.168.0.201 -A -f 10              # auto-expose then stream
ag-cam-tools stream -a 192.168.0.201 -t 0.05               # AprilTag detection (5cm tags)
ag-cam-tools stream -a 192.168.0.201 -r calibration/calibration_20260225_143015_a1b2c3d4
```

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-f`, `--fps` | Trigger rate in Hz (default: 10) |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB (0-48; 0-24 analog, 24-48 digital) |
| `-A`, `--auto-expose` | Auto-expose/gain settle then lock (mutually exclusive with `-x`/`-g`) |
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes (default: auto-negotiate) |
| `-r`, `--rectify` | Rectify using calibration session folder (loads `calib_result/remap_*.bin`) |
| `-t`, `--tag-size` | AprilTag size in meters (enables tagStandard52h13 detection) |

Press `q` or `Esc` to quit the stream window.
When AprilTag detection is enabled, detections are printed to stdout per frame/eye.

When `--rectify` is given, the stream applies real-time undistortion and rectification using pre-computed remap tables exported by `2.Calibration.ipynb`. The argument is the path to a calibration session folder (e.g. `calibration/calibration_20260225_143015_a1b2c3d4`); the tool loads `calib_result/remap_left.bin` and `calib_result/remap_right.bin` from within it. On ARM64 (Apple Silicon, Jetson) the remap uses NEON SIMD acceleration.

### `focus`

Live focus-scoring tool for adjusting M12 camera lenses. Displays the stereo stream with a Variance-of-Laplacian sharpness score overlaid on each eye. Screw lenses in/out while watching the score — higher values mean sharper focus.

```bash
ag-cam-tools focus -a 192.168.0.201              # default: center 50% ROI, 10 Hz
ag-cam-tools focus -a 192.168.0.201 --roi 200 200 600 400  # custom ROI
ag-cam-tools focus -a 192.168.0.201 -x 30000 -g 6 -b 2     # with exposure, gain and binning
ag-cam-tools focus -a 192.168.0.201 -A -b 2                 # auto-expose then focus
```

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-f`, `--fps` | Trigger rate in Hz (default: 10) |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB (0-48; 0-24 analog, 24-48 digital) |
| `-A`, `--auto-expose` | Auto-expose/gain settle then lock (mutually exclusive with `-x`/`-g`) |
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes (default: auto-negotiate) |
| `--roi` | Region of interest as `x y w h` pixels (default: center 50%) |

The SDL window shows: live stereo preview, green ROI rectangles, and per-eye focus scores with a delta indicator (turns red when left/right diverge). Scores are also printed to stdout once per second for logging.

Press `q` or `Esc` to quit.

### `calibration-capture`

Interactive capture session for building a stereo calibration image set. Shows a live side-by-side preview and saves left/right PNG pairs on keypress, matching the layout expected by `2.Calibration.ipynb`.

Each session creates a unique folder under the output directory: `calibration_<datetime>_<md5>`, where `<datetime>` is a compact timestamp and `<md5>` is an 8-character hash of the capture parameters. Images are saved into `stereoLeft/` and `stereoRight/` within that session folder.

```bash
ag-cam-tools calibration-capture -a 192.168.0.201 -A           # auto-expose, 30 pairs target
ag-cam-tools calibration-capture -a 192.168.0.201 -A -n 40     # 40 pairs target
ag-cam-tools calibration-capture -a 192.168.0.201 -x 30000 -g 6 -o ./my_calib
ag-cam-tools calibration-capture -a 192.168.0.201 -A -b 2      # use 2:1 binning (720×540)
```

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-o`, `--output` | Base output directory (default: `./calibration`) |
| `-n`, `--count` | Target number of pairs (default: 30) |
| `-f`, `--fps` | Preview rate in Hz (default: 10) |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB (0-48; 0-24 analog, 24-48 digital) |
| `-A`, `--auto-expose` | Auto-expose/gain settle then lock (mutually exclusive with `-x`/`-g`) |
| `-b`, `--binning` | Sensor binning factor: `1` (default) or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes (default: auto-negotiate) |

Press `s` to save the current pair, `q` or `Esc` to quit. The window title shows the running count. Images are saved as `imageL{N}.png` / `imageR{N}.png` (0-indexed) within the session folder.

### `depth-preview`

Live stereo depth preview with selectable disparity backend. Displays the rectified left eye alongside a JET-coloured disparity map. Requires a completed calibration session (rectification maps).

```bash
ag-cam-tools depth-preview -a 192.168.0.201 -A -r calibration/calibration_20260225_143015_a1b2c3d4
ag-cam-tools depth-preview -a 192.168.0.201 -A -r <session> --stereo-backend sgbm --block-size 7
ag-cam-tools depth-preview -a 192.168.0.201 -A -r <session> --stereo-backend onnx --model-path model.onnx
```

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-f`, `--fps` | Trigger rate in Hz (default: 10) |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-g`, `--gain` | Sensor gain in dB (0-48; 0-24 analog, 24-48 digital) |
| `-A`, `--auto-expose` | Auto-expose/gain settle then lock (mutually exclusive with `-x`/`-g`) |
| `-b`, `--binning` | Sensor binning factor: `1` (default) or `2` |
| `-p`, `--packet-size` | GigE packet size in bytes (default: auto-negotiate) |
| `-r`, `--rectify` | **Required.** Calibration session folder (loads `calib_result/remap_*.bin`) |
| `--stereo-backend` | Backend: `sgbm` (default), `onnx` (also accepts `igev`, `foundation` as aliases) |
| `--model-path` | Path to ONNX model file (required for `onnx` backend) |
| `--min-disparity` | Override calibration metadata min_disparity |
| `--num-disparities` | Override calibration metadata num_disparities |
| `--block-size` | SGBM block size (default: 5) |

Press `q` or `Esc` to quit. Click on the disparity panel to print the disparity value at that pixel.

#### Stereo backends

| Backend | Engine | Build requirement |
|---------|--------|-------------------|
| `sgbm` | OpenCV StereoSGBM (in-process) | `pkg-config opencv4` |
| `onnx` | ONNX Runtime (in-process) | `pkg-config libonnxruntime` or `ONNXRUNTIME_HOME` |

The `onnx` backend runs any ONNX stereo model in-process via the ONNX Runtime C API. It automatically selects the best execution provider: CUDA > CoreML (macOS) > CPU. The CLI also accepts `igev` and `foundation` as aliases for `onnx`.

Export notebooks for converting PyTorch models to ONNX live in `backends/` — see [backends/IGEV_SETUP.md](backends/IGEV_SETUP.md) for the full setup guide.

### Shell completions

Bash and zsh completions are installed by `make install`. They provide tab-completion for subcommands, flags, and discovered camera IPs/serials.

To source manually:

```bash
source completions/ag-cam-tools.bash   # bash
source completions/ag-cam-tools.zsh    # zsh
```

## Calibration Notebooks

Offline stereo calibration runs in Jupyter notebooks under `calibration/`.
See [bringup_guide.md](bringup_guide.md) for the full bring-up workflow.

| Notebook | Description |
|----------|-------------|
| `1.Acquiring_Calibration_Images.ipynb` | Live capture via Aravis (alternative to `calibration-capture`) |
| `2.Calibration.ipynb` | Corner detection, stereo calibration, rectification map export |
| `3.Depthmap_with_Tuning_Bar.ipynb` | Live depth map with interactive StereoBM tuning |

### Setup

```bash
cd calibration
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python -m ipykernel install --user --name agrippa-calibration
```

### Calibration workflow (`2.Calibration.ipynb`)

1. **Corner detection** — finds checkerboard corners in all stereo pairs with subpixel refinement
2. **Per-camera intrinsic calibration** — runs `calibrateCamera` independently on each camera to seed the stereo solver and flag per-camera issues
3. **Stereo calibration** — joint `stereoCalibrate` with intrinsic guess, rational 8-coefficient distortion model
4. **Per-pair reprojection error analysis** — identifies outlier pairs (bar chart + table); a separate cell removes them so you can re-run calibration
5. **Stereo rectification + export** — `stereoRectify`, undistortion/rectification maps, all `.npy` artifacts
6. **Epipolar error validation** — measures rectified y-difference across all corner pairs (must be < 0.5 px for reliable disparity)
7. **C-ready remap export** — pre-computed `remap_left.bin` / `remap_right.bin` for `ag-cam-tools stream --rectify`
8. **Disparity range estimation** — computes recommended `numDisparities`/`minDisparity` from baseline, focal length, and working distance
9. **Metadata export** — writes `calibration_meta.json` with RMS errors, baseline, focal length, disparity range, and all key parameters

### Output (`calib_result/`)

Calibration results are saved per-session in `<session>/calib_result/`:
- `.npy` files — camera matrices, distortion coefficients, R, T, rectification maps, Q matrix
- `.bin` files — pre-computed pixel-offset remap tables for the C runtime
- `calibration_meta.json` — machine-readable summary of all calibration parameters and quality metrics

## Citation and Attribution

This project is licensed under Apache-2.0 and includes a [NOTICE](NOTICE) file for attribution notices that should be preserved in redistributions and derivative works.

For academic use, citation metadata is provided in [CITATION.cff](CITATION.cff). GitHub and other tooling can automatically generate citations from this file.
The current Zenodo concept DOI in `CITATION.cff` is a placeholder (`10.5281/zenodo.0000000`) until the first archived release is published.

BibTeX:

```bibtex
@software{arnold_agrippa_stereocam_2026,
  author = {Arnold, Adam},
  title = {agrippa-stereocam},
  year = {2026},
  url = {https://github.com/AgrippaRobotics/agrippa-stereocam},
  license = {Apache-2.0}
}
```

## Driver

Camera acquisition uses [Aravis](https://github.com/AravisProject/aravis), an open-source GigE Vision / USB3 Vision library for device discovery, stream configuration, and frame capture.

## Acquisition Notes

### Sensor Binning (2x2, Average)

Frames can be acquired with **2:1 sensor-level binning** on both axes, reducing each IMX273 from 1440x1080 to **720x540** before the data leaves the sensor.

Sensor binning combines photosite charge on the silicon before readout, so the entire bin is digitised in a single ADC conversion. This is fundamentally better than digital (post-readout) binning:

- **Lower read noise** — one readout per bin instead of four.
- **Higher SNR** — up to 2x (6 dB) improvement at a given exposure.
- **Reduced GigE bandwidth** — 4x fewer bytes keeps the dual-head stream within 1000BASE-T budget.
- **Larger effective pixel pitch** — 3.45 um to 6.9 um, improving low-light sensitivity.

Average mode preserves radiometric linearity and keeps pixel values in the same 0-255 range as full-resolution captures.

### Effect on stereo geometry

Binning changes the effective pixel size used in depth computation:

```
depth = (focal_length_mm * baseline_mm) / (disparity_px * pixel_pitch_mm)
```

With 2x2 binning, `pixel_pitch_mm` = 0.0069 mm instead of 0.00345 mm.

## Hardware

**Platform**: [Lucid Phoenix PHD 1.6 MP Dual Extended-Head (IMX273)](https://thinklucid.com/product/phoenix-1-6mp-dual-head-model-imx273/)

### Stereo Geometry

| Parameter | Value |
|-----------|-------|
| Baseline | 40 mm |
| Focal length | 3 mm |

### Lens (Edmund Optics #20-061, per head)

Edmund Optics TECHSPEC Rugged Blue Series 3mm FL f/5.6 IR-Cut M12 Lens.

| Parameter | Value |
|-----------|-------|
| Focal length | 3.00 mm |
| Aperture | f/5.6 (fixed) |
| Mount | S-Mount (M12 x 0.5) |
| IR-cut filter | Yes (400-700 nm) |
| Working distance | 100 mm - infinity |
| FOV (H / V / D) | 91.2 / 68.5 / 113 degrees |

### Sensor (Sony IMX273, per head)

| Parameter | Value |
|-----------|-------|
| Resolution | 1440 x 1080 px (1.6 MP) |
| Pixel size | 3.45 um |
| Shutter | Global |
| ADC | 12-bit |
| Dynamic range | ~71 dB |

### Interface & Power

| Parameter | Value |
|-----------|-------|
| Digital interface | 1000BASE-T (GigE Vision) |
| Power | PoE (IEEE 802.3af) or 12-24 VDC |

## macOS Considerations

### 1. Disable Wi-Fi

Aravis may choose Wi-Fi instead of the Ethernet NIC, causing discovery failures. Turn off Wi-Fi or force the correct interface with `--interface`:

```bash
ag-cam-tools capture -a 192.168.0.201 -i en0 -e png -o ./frames
```

### 2. Enable Jumbo Frames (MTU 9000)

The tool auto-negotiates the largest GigE packet size supported by the link. With the default 1500-byte MTU, each 3.1 MB stereo frame requires ~2200 packets; enabling jumbo frames (MTU 9000) reduces this to ~380 packets, significantly improving throughput and reducing packet loss.

Set your Ethernet adapter to MTU 9000 in System Settings > Network > adapter > Details > Hardware. The negotiated packet size is printed during startup. To override auto-negotiation, use `--packet-size <bytes>`.

### 3. Application Firewall

The macOS firewall may silently block incoming GVSP packets. Symptoms: stream stats all zero, `tcpdump` shows traffic but capture hangs.

Quick fix — run with `sudo`. Permanent fix:

```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add ./bin/ag-cam-tools
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp ./bin/ag-cam-tools
```

Re-allow the binary after each recompile.

### 4. PF_PACKET sockets

macOS lacks PF_PACKET. The tool disables Aravis packet sockets automatically and falls back to standard UDP.
