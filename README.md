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




## macOS Considerations

macOS has several quirks that affect GigE Vision streaming. All of the items below have been
confirmed to matter for reliable capture with the PDH016S.

### 1. Disable Wi-Fi

Aravis selects the interface to use for camera discovery and streaming.  When Wi-Fi is active,
Aravis may choose it instead of the Ethernet NIC connected to the camera, resulting in discovery
failures or silent stream loss.  **Turn off Wi-Fi** (System Settings → Wi-Fi → off) before
running any capture.

If you need Wi-Fi active for other reasons, force the correct interface with `--interface`:

```bash
./bin/capture -a 192.168.0.201 --interface en0 -e png -o /tmp/caps
```

(`en0` is a typical name for the built-in or first Thunderbolt Ethernet adapter — use `ifconfig`
to confirm yours.)

### 2. Enable Jumbo Frames (MTU 9000) on the camera NIC

The default 1500-byte MTU limits GVSP payload to ~1400 bytes, so a 3.1 MB frame requires over
2200 packets.  At standard MTU, macOS occasionally drops enough of them to prevent Aravis from
assembling a complete frame.

Setting the NIC to **9000-byte jumbo frames** reduces the packet count by roughly 6× and makes
capture reliable.  Set it in System Settings → Network → your Ethernet adapter → Details →
Hardware → MTU (custom) → `9000`.

The camera itself negotiates within the configured packet size; the `capture` binary sends
`GevSCPSPacketSize = 1400` (safe default), and Aravis re-confirms the value after stream
creation.  Jumbo frames give the OS networking stack more headroom even at that payload size.

### 3. Application Firewall may block incoming GVSP packets

The macOS Application Firewall operates between the NIC driver and the BSD socket layer.
`tcpdump` (which taps at the NIC/BPF level) will show GVSP packets arriving, but Aravis's
UDP socket may receive nothing.  Symptoms:

- All stream stats zero: `completed=0 failures=0 underruns=0 resent=0 missing=0`
- `tcpdump -i en0 -n udp src 192.168.0.201` shows traffic
- Capture hangs at "attempt 0: no buffer"

**Quick fix:** run with `sudo`, which bypasses the Application Firewall:

```bash
sudo ./bin/capture -a 192.168.0.201 --interface en0 -e png -o /tmp/caps
```

**Permanent fix:** allow the binary through the firewall:

```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add ./bin/capture
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp ./bin/capture
```

You will need to re-allow the binary any time it is recompiled.

### 4. PF_PACKET sockets are not supported

Linux Aravis uses raw PF_PACKET sockets for higher-throughput streaming.  macOS does not have
this socket type.  The `capture` binary explicitly sets
`ARV_GV_STREAM_OPTION_PACKET_SOCKET_DISABLED` before creating the stream so Aravis falls back
to standard UDP — no action needed, but if you port this code elsewhere make sure that call is
preserved.

### Confirmed working setup

| Setting | Value |
|---------|-------|
| Interface | en0 (direct cable to camera, no switch) |
| Host IP | 192.168.0.149 / 24 |
| Camera IP | 192.168.0.201 |
| MTU | 9000 (jumbo frames) |
| Wi-Fi | Disabled |
| Run as | `sudo` (or firewall exception added) |

```bash
sudo ./bin/capture -a 192.168.0.201 --interface en0 -e png -o /tmp/caps
```
