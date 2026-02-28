# `connect`

Connect to a camera and print device information.

## Examples

```bash
ag-cam-tools connect -a 192.168.0.201
ag-cam-tools connect -s PDH016S-001
ag-cam-tools connect
```

## Notes

- Without a selector, the tool can fall back to an interactive picker.
- Use this command to verify basic connectivity before capture or streaming.
