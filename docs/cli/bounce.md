# `bounce`

Reset (power-cycle) the camera over GigE without physically unplugging it.

Sends the GenICam `DeviceReset` command, which reboots the camera to its
power-up state. By default the tool waits for the camera to reappear on
the network before exiting.

## Examples

```bash
# Reset by IP, wait for reboot
ag-cam-tools bounce -a 192.168.0.201

# Reset by serial, skip waiting
ag-cam-tools bounce -s PDH016S-001 --no-wait

# Reset with a longer timeout
ag-cam-tools bounce -a 192.168.0.201 --timeout 60
```

## Options

| Flag | Description |
|------|-------------|
| `-s, --serial <serial>` | Match camera by serial number |
| `-a, --address <address>` | Connect by camera IP |
| `-i, --interface <iface>` | Restrict to this NIC |
| `--no-wait` | Exit immediately after issuing the reset |
| `--timeout <seconds>` | Max seconds to wait for the camera to come back (default: 30) |

## Notes

- The camera will be unreachable for several seconds during reboot.
- After reset, the camera returns to its power-up settings (user-set Default).
- This is equivalent to unplugging and re-plugging the Ethernet cable.
- If `--no-wait` is not given, the tool polls the network until the camera
  reappears or the timeout expires.
