# `list`

Discover and print GigE cameras visible on the network, annotated with detected stereo / mono sensor mode.

## Examples

```bash
ag-cam-tools list
ag-cam-tools list --machine-readable
ag-cam-tools list -i en0
ag-cam-tools list --no-probe
```

## Sensor mode column

For each enumerated camera the tool briefly opens the device and probes available pixel formats:

| Value | Meaning |
|-------|---------|
| `stereo` | Advertises `DualBayerRG8` (Lucid PDH016S and similar dual-eye heads) |
| `mono` | Advertises `BayerRG8` / `Mono8` only (Lucid Triton TRT016S etc.) |
| `unknown` | Could not open the device, has no usable 8-bit pixel format, or `--no-probe` was set |

Probe failures are non-fatal — the row is still printed with `mode=unknown`.

## Notes

- The default output is an ASCII table.
- `--machine-readable` emits tab-separated output (4 fields: ip, model, serial, mode) for scripts.
- `--no-probe` skips the per-camera open. Useful when cameras are in use elsewhere or when enumeration latency matters; all rows show `mode=unknown`.
- Use `-i` when multiple network interfaces are present and discovery picks the wrong one.
