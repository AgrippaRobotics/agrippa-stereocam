# Quick Start

## Discover a camera

```bash
ag-cam-tools list
ag-cam-tools list --machine-readable
ag-cam-tools list -i en0
```

## Connect and inspect

```bash
ag-cam-tools connect -a 192.168.0.201
ag-cam-tools connect -s PDH016S-001
```

## Capture a frame pair

```bash
ag-cam-tools capture -a 192.168.0.201 -e png -o ./frames
ag-cam-tools capture -a 192.168.0.201 -A -e png -o ./frames
```

## Start live preview

```bash
ag-cam-tools stream -a 192.168.0.201 -A
```

## Focus the lenses

```bash
ag-cam-tools focus -a 192.168.0.201 -A
```

## Collect calibration images

```bash
ag-cam-tools calibration-capture -a 192.168.0.201 -A -n 30
```

## Next steps

- Read the command docs in [../cli/overview.md](../cli/overview.md)
- Follow the workflow docs in [../workflows/bring-up.md](../workflows/bring-up.md)
- Run the offline calibration notebooks described in [../workflows/calibration.md](../workflows/calibration.md)
