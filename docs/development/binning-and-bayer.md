# Binning and Bayer Notes

This page summarizes the repo's current reasoning around 2x2 binning and Bayer interpretation.

## Core issue

When the camera performs sensor-level `2x2` binning in average mode, a post-binning sample may no longer represent a valid Bayer site. That matters because several downstream paths historically assumed that binned data could still be demosaiced as if it were a true CFA image.

## Why this matters

If a `2x2` Bayer neighborhood is averaged together:

- the output is a mixed spectral value,
- debayering is no longer mathematically well-founded,
- grayscale or luminance-style handling is more defensible than color reconstruction.

## Current repo stance

The runtime now reads back the effective Bayer state after configuration and treats the image as grayscale when the camera reports that the Bayer pattern is no longer present.

That affects:

- capture output,
- stream preview,
- focus,
- calibration capture,
- depth preview paths.

## Practical consequence

For `binning=2`, expect grayscale-like handling unless the device explicitly reports that a valid Bayer pattern is still preserved.

## Original investigation

The full investigation notes remain in the repo root at [../../binning_bug_fix.md](../../binning_bug_fix.md).
