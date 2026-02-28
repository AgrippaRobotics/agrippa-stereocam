# Testing

`ag-cam-tools` has two testing tiers:

- unit tests that do not require hardware,
- hardware integration tests that require a camera on the network.

## Quick reference

```bash
make test
make test-hw
make test-all
```

## Unit tests

Unit tests are standalone C programs built around `vendor/greatest.h`. They link against only the object files under test rather than the full camera stack.

### Current binaries

| Binary | Source | Coverage |
|--------|--------|----------|
| `bin/test_calib_archive` | `tests/test_calib_archive.c` | Archive pack/unpack and metadata handling |
| `bin/test_remap` | `tests/test_remap.c` | Remap table loading and application |
| `bin/test_binning` | `tests/test_binning.c` | Debayer, software binning, and grayscale processing |

### Conventions

- Each test file defines one or more `SUITE()` blocks.
- `main()` uses `GREATEST_MAIN_BEGIN` and `GREATEST_MAIN_END`.
- Individual suites or tests can be selected from the command line.

## Hardware integration tests

Hardware tests are shell scripts in `tests/`.

### Current scripts

| Script | Coverage |
|--------|----------|
| `tests/test_stash_hw.sh` | Calibration-stash lifecycle |
| `tests/test_binning_hw.sh` | Capture behavior with and without binning |

### Conventions

- accept `-s`, `-a`, and `-i` for device selection,
- return `77` when no camera is available,
- clean up temporary files with `trap`,
- avoid unnecessary external dependencies.

### Fixture helper

`tests/gen_test_calibration.c` generates a small synthetic calibration session for fast archive tests.

## Adding tests

For a new unit test:

1. Create `tests/test_<name>.c`.
2. Link only the objects required by the code under test.
3. Add the new target to the Makefile test targets.

For a new hardware test:

1. Create `tests/test_<name>_hw.sh`.
2. Keep the same device selection and exit-code conventions.
3. Add it to `test-hw` in the Makefile.
