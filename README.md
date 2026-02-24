# agrippa-stereocam

Stereo camera toolkit for the [Lucid Phoenix PHD016S](https://thinklucid.com/product/phoenix-1-6mp-dual-head-model-imx273/) dual-head GigE Vision camera. Provides camera discovery, single-frame capture with PNG/JPEG/PGM output, and real-time stereo preview via SDL2.

## Building

Dependencies: [Aravis 0.8](https://github.com/AravisProject/aravis), [SDL2](https://www.libsdl.org/), pkg-config.

```bash
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
```

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-o`, `--output` | Output directory (default: current) |
| `-e`, `--encode` | Output format: `pgm`, `png`, or `jpg` |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |
| `-v`, `--verbose` | Print diagnostic register readback |

### `stream`

```bash
ag-cam-tools stream -a 192.168.0.201 -f 10
ag-cam-tools stream -a 192.168.0.201 -f 5 -b 2 -x 30000
```

| Option | Description |
|--------|-------------|
| `-s`, `--serial` | Match camera by serial number |
| `-a`, `--address` | Connect by camera IP address |
| `-i`, `--interface` | Force NIC selection |
| `-f`, `--fps` | Trigger rate in Hz (default: 10) |
| `-x`, `--exposure` | Exposure time in microseconds |
| `-b`, `--binning` | Sensor binning factor: `1` or `2` |

Press `q` or `Esc` to quit the stream window.

### Shell completions

Bash and zsh completions are installed by `make install`. They provide tab-completion for subcommands, flags, and discovered camera IPs/serials.

To source manually:

```bash
source completions/ag-cam-tools.bash   # bash
source completions/ag-cam-tools.zsh    # zsh
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

The default 1500-byte MTU causes packet drops on large frames. Set your Ethernet adapter to MTU 9000 in System Settings > Network > adapter > Details > Hardware.

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
