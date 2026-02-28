# Bring-Up Workflow

This guide describes the ordered workflow for commissioning an Agrippa stereo camera system.

## Overview

The high-level order is:

1. Mechanical assembly
2. Electrical and network setup
3. Camera configuration
4. Lens focus adjustment
5. Stereo calibration
6. Calibration validation

## 1. Mechanical assembly

Goals:

- rigidly mount both cameras,
- fix the stereo baseline,
- eliminate mechanical flex,
- avoid cable strain that can twist the optics.

Do not leave adjustable mechanics in the final calibrated configuration unless they can be locked repeatably.

## 2. Electrical and network setup

Before moving on:

- confirm stable power,
- confirm camera discovery through the intended NIC,
- verify there is no obvious packet loss,
- verify any trigger wiring and isolated I/O assumptions.

## 3. Camera configuration

Lock these parameters before calibration:

- exposure,
- gain,
- resolution,
- pixel format,
- frame rate,
- any gamma or ISP settings that affect imaging geometry or corner detection.

Calibration is only valid for fixed intrinsics.

## 4. Focus adjustment

Use the `focus` command at the intended working distance. Aim for:

- high focus scores in both eyes,
- low mismatch between left and right channels,
- stable exposure and gain during adjustment.

Once focus is correct, lock the lenses mechanically.

## 5. Stereo calibration

Calibration is performed offline from captured image pairs. The quality of the final result depends more on dataset quality than solver novelty.

Collect:

- 15-20 usable pairs at minimum,
- 25-40 pairs for a stronger solve,
- diverse board positions, tilts, distances, and edge coverage.

Avoid capture sets where every checkerboard is centered, fronto-parallel, or at the same range.

## 6. Validation

Validate the resulting calibration before depending on it for depth work:

- inspect reprojection error,
- reject obvious outlier pairs,
- confirm epipolar alignment is acceptably small after rectification,
- test real rectified imagery in `stream` or one of the depth-preview commands.

## Related docs

- Calibration details: [calibration.md](calibration.md)
- Focus audio and operator feedback: [focus-audio.md](focus-audio.md)
