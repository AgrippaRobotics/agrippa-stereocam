# Testing

ag-cam-tools has two testing tiers: **unit tests** that run without hardware and **hardware integration tests** that require a Lucid camera on the network.

## Quick reference

```bash
make test          # unit tests only (no camera needed)
make test-hw       # hardware integration tests (camera required)
make test-all      # both
```

## Unit tests

Unit tests are C programs that use the [greatest.h](https://github.com/silentbicycle/greatest) single-header test framework (`vendor/greatest.h`).  They link only against glib and the specific `.o` files under test -- never against Aravis -- so they build and run on any development machine.

### Test binaries

| Binary | Source | Tests | What it covers |
|--------|--------|-------|----------------|
| `bin/test_calib_archive` | `tests/test_calib_archive.c` | Pack/unpack/list of calibration archives | `calib_archive.c` round-trip serialisation using sample data in `calibration/sample_calibration/` |
| `bin/test_remap` | `tests/test_remap.c` | Remap table load and apply | `remap.c` loading `.bin` remap files, dimensions, pixel mapping |
| `bin/test_binning` | `tests/test_binning.c` | Bayer CFA vs binning correctness | `imgproc.c` debayer, software binning, deinterleave, grayscale conversion |

### How unit tests link

Tests need Aravis *headers* (because `common.h` includes `<arv.h>`) but do not link against Aravis *libraries*.  This is why pure image-processing functions live in `imgproc.c` -- they can be linked into test binaries independently.

```
TEST_CFLAGS = ... $(shell pkg-config --cflags aravis-0.8) -I$(SRCDIR) -I$(VENDORDIR)
TEST_LIBS   = $(shell pkg-config --libs glib-2.0) -lz -lm
```

Each test binary only links the object files it actually needs:

- `test_calib_archive` links `remap.o`, `calib_archive.o`, `cJSON.o`
- `test_remap` links `remap.o`
- `test_binning` links `imgproc.o`

### greatest.h conventions

- Each test file defines one or more `SUITE()` blocks containing `RUN_TEST()` calls.
- `main()` uses `GREATEST_MAIN_BEGIN / GREATEST_MAIN_END` and calls `RUN_SUITE()`.
- Run a specific suite: `bin/test_binning -s software_bin_destroys_bayer`
- Run a specific test: `bin/test_binning -t bin2x2_mixes_channels`
- Verbose output: `bin/test_binning -v`

### Writing a new unit test

1. Create `tests/test_<name>.c`, include `../vendor/greatest.h` and the header under test.
2. Add a Makefile rule linking only the needed `.o` files against `TEST_LIBS`.
3. Add the binary to the `test:` target's prerequisite list and command list.

## Hardware integration tests

Hardware tests are bash scripts in `tests/` that exercise `bin/ag-cam-tools` subcommands against a live camera.  They are designed to be idempotent and self-cleaning (temp files are removed on exit via `trap`).

### Test scripts

| Script | What it tests |
|--------|---------------|
| `tests/test_stash_hw.sh` | Calibration-stash lifecycle: upload, list, download, integrity check, overwrite, delete, purge |
| `tests/test_binning_hw.sh` | Capture with/without binning: PNG colour type (RGB vs grayscale), PGM validity, dimensions, file sizes, diagnostic messages |

### Conventions

All hardware test scripts follow the same structure:

- **Device selection**: Accept `-s`/`--serial`, `-a`/`--address`, `-i`/`--interface` flags.  Without flags, auto-discover the first camera.
- **Skip on no camera**: If `ag-cam-tools list` fails, exit with code **77** (skip).
- **Exit codes**: 0 = all passed, 1 = failures, 77 = skipped.
- **Colour output**: PASS/FAIL/SKIP in green/red/yellow when stdout is a terminal.
- **Self-cleaning**: `mktemp -d` with a `trap ... EXIT` to clean up.
- **No external dependencies** beyond the tool binary, Python 3 (for PNG/PGM header parsing), and standard unix utilities.

### Test helpers

`tests/gen_test_calibration.c` is a standalone C program (no library dependencies) that generates a minimal 128x128 calibration session with tiny remap files (~64 KB each).  Used by `test_stash_hw.sh` so upload/download cycles are fast.

### Running hardware tests

```bash
# Auto-discover camera
make test-hw

# Target a specific camera
tests/test_stash_hw.sh -a 192.168.1.100
tests/test_binning_hw.sh -s ABC123

# Run everything
make test-all
```

### Writing a new hardware test

1. Create `tests/test_<name>_hw.sh` following the conventions above (device flags, exit codes, trap cleanup).
2. Make it executable: `chmod +x tests/test_<name>_hw.sh`.
3. Add the script to the `test-hw:` target in the Makefile.
4. If the test needs generated fixtures, add a generator and wire it as a `test-hw` prerequisite.
