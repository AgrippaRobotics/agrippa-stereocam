# Installation

## Core dependencies

Required for the base build:

- Aravis 0.8
- SDL2
- `pkg-config`
- a C toolchain compatible with the repo Makefile

## Optional dependencies

These enable additional runtime backends and features:

- `apriltag` for AprilTag detection in `stream`
- OpenCV 4 for `--stereo-backend sgbm`
- ONNX Runtime for `--stereo-backend onnx`

If `apriltag` is not installed system-wide, the repo can fall back to the vendored copy in `vendor/apriltag`.

For ONNX Runtime, the build can discover it through either:

- `pkg-config libonnxruntime`
- `ONNXRUNTIME_HOME=/path/to/onnxruntime`

## Build

```bash
git submodule update --init --recursive
make
```

Install the binary and shell completions:

```bash
sudo make install
```

## Shell completions

`make install` installs bash and zsh completions. You can also source them manually during development:

```bash
source completions/ag-cam-tools.bash
source completions/ag-cam-tools.zsh
```

## Notes for macOS

### Interface selection

Aravis may prefer Wi-Fi instead of the Ethernet NIC. If discovery or capture fails, force the correct interface:

```bash
ag-cam-tools capture -a 192.168.0.201 -i en0 -e png -o ./frames
```

### Jumbo frames

The tool auto-negotiates packet size, but MTU 9000 substantially reduces packet count and helps throughput.

### Firewall

The macOS application firewall can block incoming GVSP packets. If capture hangs while traffic is visible on the wire, add the binary explicitly:

```bash
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add ./bin/ag-cam-tools
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp ./bin/ag-cam-tools
```

### PF_PACKET

macOS does not provide PF_PACKET sockets. The tool falls back to standard UDP automatically.
