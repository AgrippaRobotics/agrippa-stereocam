# Binning and Bayer

## Behavior

When `--binning 2` is used, the camera performs sensor-level `2x2` averaging. Because the averaging collapses a full Bayer quad (R, G, G, B) into a single mixed sample, the output is no longer a valid Bayer mosaic.

The runtime reads back `IspBayerPattern` after configuration. When the camera reports that the Bayer pattern is no longer present, all downstream paths treat the image as single-channel grayscale and skip debayering. This applies to:

- `capture` — writes grayscale PGM, PNG, or JPEG instead of color
- `stream` — displays grayscale preview
- `focus` — computes focus metrics on the grayscale image directly
- `calibration-capture` — saves grayscale calibration pairs
- `depth-preview-classical` — feeds grayscale directly to the disparity pipeline without a redundant debayer-then-convert-to-gray round-trip

At `--binning 1` (the default), the full Bayer mosaic is preserved and color debayering proceeds normally.

## Software binning fallback

When the camera does not support hardware binning, a software fallback averages each `2x2` block. This also destroys the CFA layout, so the same grayscale path applies.
