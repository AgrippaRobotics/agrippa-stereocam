# agrippa-stereocam
A stereo camera based on the Lucid PDH and associated tooling

## Driver

Camera acquisition is implemented using [Aravis](https://github.com/AravisProject/aravis), an open-source GigE Vision / USB3 Vision library. Aravis handles device discovery, stream configuration, and frame capture via the GigE Vision protocol exposed by the Lucid PHD.

## Acquisition Configuration

### Sensor Binning (2×2, Average)

Frames are acquired with **2:1 sensor-level binning** on both axes, reducing each IMX273 from 1440×1080 to **720×540** before the data leaves the sensor.

**Why sensor-level binning?**

Sensor binning combines photosite charge on the silicon before readout, so the entire bin is digitised in a single ADC conversion. This is fundamentally better than digital (post-readout) binning, where every pixel is read out individually and their noise contributions all accumulate:

- **Lower read noise** — one readout per bin instead of four, so read noise does not scale with bin count.
- **Higher SNR** — averaging four photosites raises signal-to-noise ratio by up to 2× (6 dB) at a given exposure compared to full-resolution capture.
- **Reduced GigE bandwidth** — 720×540 carries 4× fewer bytes than 1440×1080, keeping the dual-head stream (two sensors, one link) comfortably within the 1000BASE-T budget and allowing higher sustainable frame rates.
- **Larger effective pixel pitch** — 2×2 binning raises the effective pitch from 3.45 µm to 6.9 µm, improving sensitivity in lower-light scenes.

**Why "Average" mode?**

Average binning scales the combined charge back to the original bit depth (0–255 for 8-bit output), preserving radiometric linearity and keeping pixel values in the same range as full-resolution captures. Sum mode would halve the available dynamic range and risk saturation in bright scenes.

**Effect on stereo geometry**

Binning does not affect the physical baseline or focal length, but it does change the effective pixel size used when computing depth from disparity:

```
depth = (focal_length_mm × baseline_mm) / (disparity_px × pixel_pitch_mm)
```

With 2×2 binning, `pixel_pitch_mm` = 0.0069 mm (6.9 µm) instead of 0.00345 mm.

## Stereo Camera Parameters

**Platform**: [Lucid Phoenix PHD 1.6 MP Dual Extended-Head (IMX273)](https://thinklucid.com/product/phoenix-1-6mp-dual-head-model-imx273/)

### Stereo Geometry
| Parameter | Value |
|-----------|-------|
| Baseline | 40 mm |
| Focal length | 3 mm |

### Lens (Edmund Optics #20-061, per camera)

**Edmund Optics TECHSPEC Rugged Blue Series 3mm FL f/5.6 IR-Cut M12 Lens** — glued-optic construction for pixel-stable pointing after shock and vibration.

| Parameter | Value |
|-----------|-------|
| Focal length | 3.00 mm |
| Aperture | f/5.6 (fixed) |
| Mount | S-Mount (M12 × 0.5) |
| IR-cut filter | Yes (400–700 nm) |
| Elements / groups | 6 / 5 |
| Max image circle | 6.00 mm |
| Back focal length | 4.8 mm (@ 100 mm WD) |
| Working distance | 100 mm – ∞ |
| Distortion | 34.78% @ full field |
| Coating | λ/4 MgF₂ @ 550 nm |
| Length × diameter | 16.1 × 14 mm |
| Ruggedization | Stabilized (shock & vibration) |

**Field of view** (1/3" sensor format):

| | Angle |
|-|-------|
| Horizontal | 91.2° |
| Vertical | 68.5° |
| Diagonal | 113° |

### Sensor (Sony IMX273, per camera)
| Parameter | Value |
|-----------|-------|
| Sensor size | 1/2.9" (6.3 mm) |
| Resolution | 1440 × 1080 px (1.6 MP) |
| Pixel size | 3.45 µm |
| Shutter | Global |
| ADC | 12-bit |
| Max frame rate | 35.8 FPS (full resolution) |
| Dynamic range | ~71 dB |

### Interface & Power
| Parameter | Value |
|-----------|-------|
| Digital interface | 1000BASE-T (GigE Vision) |
| Power | PoE (IEEE 802.3af) or 12–24 VDC |
| Lens mount | C-mount or S-mount (extended head) |




## macOS Aravis Networking + Permissions Quick Note

### Network adapter requirements
- Camera and Mac must be on the **same subnet (use /24 recommended)**  
  - Example:  
    - Camera: `192.168.2.3`  
    - Mac: `192.168.2.10`  
    - Mask: `255.255.255.0`
- Use **wired Ethernet only** (GigE Vision discovery will not work over Wi-Fi)
- Prefer:
  - Direct cable connection, or
  - Simple unmanaged switch
- Avoid:
  - VLANs
  - Corporate networks
  - Routed subnets
- Disable Wi-Fi while debugging to ensure correct interface selection

### Force correct interface
macOS often has multiple interfaces (Wi-Fi + Ethernet).  
Aravis may choose the wrong one.

Find Ethernet interface:
```bash
ifconfig
```

Run application forcing interface:
```bash
ARV_INTERFACE=en7 python3 app.py
```

Example with sudo:
```bash
sudo ARV_INTERFACE=en7 python3 app.py
```

### Sudo / permissions requirement
On macOS, Aravis requires raw socket access for GigE Vision.

Always run capture applications with:
```bash
sudo python3 app.py
```

Without sudo:
- camera discovery may fail
- streaming may silently fail
- viewer may not see devices

### Typical working invocation
```bash
sudo ARV_INTERFACE=en7 python3 capture.py
```
