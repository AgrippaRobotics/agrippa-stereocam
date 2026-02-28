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

Unit tests are standalone C programs built around the [Unity](https://github.com/ThrowTheSwitch/Unity) framework (`vendor/unity/`). They link against only the object files under test rather than the full camera stack.

### Current binaries

| Binary | Source | Tests | Coverage |
|--------|--------|-------|----------|
| `bin/test_calib_archive` | `tests/test_calib_archive.c` | 27 | Archive pack/unpack, format handling, multi-slot AGMS |
| `bin/test_remap` | `tests/test_remap.c` | 12 | Remap table loading and application |
| `bin/test_binning` | `tests/test_binning.c` | 9 | Debayer, software binning, and grayscale processing |
| `bin/test_calib_load` | `tests/test_calib_load.c` | 9 | Local-path calibration loading, metadata parsing |
| `bin/test_focus` | `tests/test_focus.c` | 10 | Focus score ordering, ROI clamping, Laplacian precision |
| `bin/test_stereo_common` | `tests/test_stereo_common.c` | 17 | Backend parsing, SGBM defaults, JET colorize, depth conversion |
| `bin/test_imgproc_extra` | `tests/test_imgproc_extra.c` | 18 | Gamma LUT, color conversion, roundtrip proofs |
| `bin/test_image` | `tests/test_image.c` | 17 | Format parsing, PGM/PNG/JPG encoding, DualBayer pair output |
| `bin/test_calib_load_slot` | `tests/test_calib_load_slot.c` | 10 | Slot-based calibration loading via mock device |

### Conventions

- Each test is a `void test_<name>(void)` function.
- `setUp()` and `tearDown()` are called automatically before and after every test.
- `main()` uses `UNITY_BEGIN()` / `UNITY_END()` and calls `RUN_TEST(test_fn)`.
- Individual tests can be selected from the command line; pass `-v` for verbose output.

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

1. Create `tests/test_<name>.c`, include `../vendor/unity/unity.h` and the header under test.
2. Implement `void setUp(void)` and `void tearDown(void)` (can be empty stubs).
3. Write tests as `void test_<descriptive_name>(void)` functions.
4. In `main()`, use `UNITY_BEGIN()` / `return UNITY_END()` with `RUN_TEST()` calls.
5. Add a Makefile rule linking `$(UNITY_OBJ)` and only the needed `.o` files against `TEST_LIBS`.
6. Add the binary to the `test:` target.

For a new hardware test:

1. Create `tests/test_<name>_hw.sh`.
2. Keep the same device selection and exit-code conventions.
3. Add it to `test-hw` in the Makefile.
