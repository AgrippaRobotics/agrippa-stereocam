# Testing

ag-cam-tools has two testing tiers: **unit tests** that run without hardware and **hardware integration tests** that require a Lucid camera on the network.

## Quick reference

```bash
make test          # unit tests only (no camera needed)
make test-hw       # hardware integration tests (camera required)
make test-all      # both
```

## Unit tests

Unit tests are C programs that link only against glib and the specific `.o` files under test -- never against Aravis -- so they build and run on any development machine.

All unit tests use the [Unity](https://github.com/ThrowTheSwitch/Unity) framework (`vendor/unity/`).

### Test binaries

| Binary | Source | Tests | What it covers |
|--------|--------|-------|----------------|
| `bin/test_calib_archive` | `tests/test_calib_archive.c` | 27 | `calib_archive.c` pack/unpack/list, AGST/AGCZ/AGCAL format, multi-slot AGMS, backward compat, error handling |
| `bin/test_remap` | `tests/test_remap.c` | 12 | `remap.c` loading `.bin` remap files, from-memory loading, RGB/gray identity and sentinel mapping |
| `bin/test_binning` | `tests/test_binning.c` | 9 | `imgproc.c` debayer, software binning, deinterleave, Bayer CFA destruction proof, pipeline comparison |
| `bin/test_calib_load` | `tests/test_calib_load.c` | 9 | `calib_load.c` local-path loading, metadata parsing, error handling |
| `bin/test_focus` | `tests/test_focus.c` | 24 | `focus.c` per-metric score ordering (laplacian, tenengrad, brenner), blur monotonicity, noise sensitivity, ROI clamping, metric string parsing, known-value Laplacian precision |
| `bin/test_stereo_common` | `tests/test_stereo_common.c` | 17 | `stereo_common.c` backend parsing, SGBM defaults, JET colorize, depth conversion |
| `bin/test_imgproc_extra` | `tests/test_imgproc_extra.c` | 18 | `imgproc.c` gamma_lut_2p5, apply_lut_inplace, rgb_to_gray, gray_to_rgb_replicate, roundtrip proof |
| `bin/test_image` | `tests/test_image.c` | 17 | `image.c` format parsing, PGM write/roundtrip, PNG/JPG magic bytes, DualBayer pair output with binning |
| `bin/test_calib_load_slot` | `tests/test_calib_load_slot.c` | 10 | `calib_load.c` slot path via mock device: legacy AGST, multi-slot AGMS, error handling |

### How unit tests link

Tests need Aravis *headers* (because `common.h` includes `<arv.h>`) but do not link against Aravis *libraries*.  This is why pure image-processing functions live in `imgproc.c` -- they can be linked into test binaries independently.

```
UNITY_CFLAGS = $(TEST_CFLAGS) -I$(UNITY_DIR) -DUNITY_INCLUDE_DOUBLE
TEST_LIBS    = $(shell pkg-config --libs glib-2.0) -lz -lm
```

Each test binary links `$(UNITY_OBJ)` plus only the object files it actually needs:

- `test_calib_archive` links `remap.o`, `calib_archive.o`, `cJSON.o`, `unity.o`
- `test_calib_load` links `remap.o`, `calib_archive.o`, `cJSON.o`, `calib_load.o`, `unity.o`
- `test_remap` links `remap.o`, `unity.o`
- `test_binning` links `imgproc.o`, `unity.o`
- `test_focus` links `focus.o`, `unity.o`
- `test_stereo_common` compiles `stereo_common.c` directly (see note below), links `unity.o`
- `test_imgproc_extra` links `imgproc.o`, `unity.o`
- `test_image` links `image.o`, `imgproc.o`, `remap.o`, `unity.o`
- `test_calib_load_slot` links `calib_load.o`, `remap.o`, `calib_archive.o`, `cJSON.o`, `mock_device_file.o`, `unity.o`

### Testing modules with conditional backends

`stereo_common.c` contains both pure-logic functions (backend parsing, SGBM defaults, JET colorisation) and backend-dispatching functions guarded by `#ifdef HAVE_OPENCV` / `#ifdef HAVE_ONNXRUNTIME`.  The dispatching functions reference symbols like `ag_sgbm_create` and `ag_onnx_create` that only exist when those optional backends are compiled in.

If the test binary linked the pre-built `stereo_common.o` from the main build, it would inherit whichever `HAVE_*` flags were active at build time -- and the linker would demand the backend libraries just to test pure string parsing and colourmap maths.

The solution is to **recompile the source directly into the test binary** with the backend defines explicitly undefined:

```makefile
$(BINDIR)/test_stereo_common: $(TESTDIR)/test_stereo_common.c $(SRCDIR)/stereo_common.c \
                              $(UNITY_OBJ) | $(BINDIR)
	$(CC) $(UNITY_CFLAGS) -UHAVE_OPENCV -UHAVE_ONNXRUNTIME -o $@ \
	      $(TESTDIR)/test_stereo_common.c $(SRCDIR)/stereo_common.c $(UNITY_OBJ) $(TEST_LIBS)
```

The `-U` flags force both backends off regardless of what `CFLAGS` might set globally.  With both guards disabled, the `ag_disparity_create` / `compute` / `destroy` functions compile to their stub branches (print an error and return `NULL` or `-1`) which reference no external symbols.  The test binary then links cleanly against only glib and zlib.

Use this pattern whenever a module mixes testable pure logic with conditionally-compiled backend code that would otherwise drag in heavy external dependencies.

### Mocking hardware dependencies

Modules that call Aravis camera functions (`device_file.c`, `common.c`) cannot be tested without a camera -- unless the hardware-facing symbols are replaced with mock implementations at link time.

`tests/mock_device_file.c` provides configurable stubs for all `ag_device_file_*` functions.  Test code injects data and return codes before each test:

```c
#include "mock_device_file.h"

void setUp (void) { mock_device_file_reset (); }

void test_slot_loading (void) {
    mock_device_file_set_read_data (archive_buf, archive_len);
    // ... call ag_calib_load with slot >= 0 ...
    TEST_ASSERT_EQUAL_INT (1, mock_device_file_read_call_count ());
}
```

The mock object links in place of `device_file.o`, so the test binary resolves all `ag_device_file_*` symbols without pulling in Aravis.  To add mock support for additional hardware modules, follow the same pattern: create a `tests/mock_<module>.c` with controllable stubs and a corresponding header.

### Unity conventions

- Each test is a `void test_<name> (void)` function -- no return value, passes by reaching the end.
- `setUp()` and `tearDown()` are called automatically before and after every test.
- `main()` uses `UNITY_BEGIN()` / `UNITY_END()` and calls `RUN_TEST(test_fn)`.
- Assertions: `TEST_ASSERT_EQUAL_INT`, `TEST_ASSERT_FLOAT_WITHIN`, `TEST_ASSERT_EQUAL_DOUBLE`, `TEST_ASSERT_EQUAL_MEMORY`, `TEST_ASSERT_NOT_NULL`, `TEST_ASSERT_EQUAL_STRING`, etc.
- Verbose output: `bin/test_focus -v`
- Double precision is enabled via `-DUNITY_INCLUDE_DOUBLE` in `UNITY_CFLAGS`.

### Writing a new unit test

1. Create `tests/test_<name>.c`, include `../vendor/unity/unity.h` and the header under test.
2. Implement `void setUp (void)` and `void tearDown (void)` (can be empty stubs).
3. Write tests as `void test_<descriptive_name> (void)` functions.
4. In `main()`, use `UNITY_BEGIN()` / `return UNITY_END()` with `RUN_TEST()` calls.
5. Add a Makefile rule linking `$(UNITY_OBJ)` and only the needed `.o` files against `TEST_LIBS`.
6. Add the binary to the `test:` target's prerequisite list and command list.

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
