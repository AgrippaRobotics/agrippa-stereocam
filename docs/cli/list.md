# `list`

Discover and print GigE cameras visible on the network.

## Examples

```bash
ag-cam-tools list
ag-cam-tools list --machine-readable
ag-cam-tools list -i en0
```

## Notes

- The default output is an ASCII table.
- `--machine-readable` emits tab-separated output for scripts.
- Use `-i` when multiple network interfaces are present and discovery picks the wrong one.
