# Agrippa Stereo Camera Bring-Up Guide

<!-- DOCUMENT: Agrippa Stereo Camera Bring-Up Guide -->
<!-- PURPOSE: High-level and detailed workflow for building and calibrating an Agrippa stereo camera system -->
<!-- AUDIENCE: Engineers performing mechanical, electrical, and calibration bring-up -->
<!-- STATUS: Living engineering document -->

---

## Overview

<!-- SECTION: Overview -->
<!-- DESCRIPTION: High-level workflow for building and commissioning an Agrippa stereo camera -->
<!-- DIAGRAM: Bring-Up Workflow -->

```mermaid
flowchart TD
    A[Mechanical Assembly]
    B[Electrical & Network Setup]
    C[Camera Configuration]
    D[Lens Focus Adjustment]
    E[Stereo Calibration]
    F[Calibration Validation]

    A --> B --> C --> D --> E --> F
```

This document defines the ordered workflow required to bring an Agrippa stereo camera system into operational state.

---

## 1. Mechanical Assembly

<!-- SECTION: Mechanical Assembly -->
<!-- OBJECTIVE: Build rigid stereo baseline with correct alignment -->

### Goals
- Rigidly mount both cameras
- Fix stereo baseline distance
- Ensure no mechanical flex between cameras
- Lock M12 lenses with thread locker (after focus)

### Best Practices
- Use a machined or single-piece mount
- Avoid adjustable mechanisms after calibration
- Ensure no cable strain induces torque

---

## 2. Electrical & Network Setup

<!-- SECTION: Electrical & Network Setup -->
<!-- OBJECTIVE: Ensure deterministic connectivity and power stability -->

### Goals
- Provide stable regulated power
- Configure static IPs (if required)
- Verify GigE throughput
- Confirm hardware trigger wiring

### Validation Checklist
- Cameras visible in Lucid Arena
- No packet loss
- Hardware trigger verified on scope

---

## 3. Camera Configuration

<!-- SECTION: Camera Configuration -->
<!-- OBJECTIVE: Lock imaging parameters before calibration -->

### Lock These Parameters Before Calibration
- Exposure time
- Gain
- Gamma (disable if possible)
- Resolution
- Pixel format
- Frame rate

Calibration is only valid for fixed intrinsics.

---

## 4. Lens Focus Adjustment

<!-- SECTION: Lens Focus Adjustment -->
<!-- OBJECTIVE: Achieve consistent and matched focus between stereo pair -->

### Recommended Method
- Use Laplacian variance metric
- Focus at intended working distance
- Focus both cameras at same distance
- Lock lens mechanically after adjustment

Focus should be finalized **before calibration image capture**.

---

## 5. Stereo Calibration

<!-- SECTION: Stereo Calibration -->
<!-- OBJECTIVE: Estimate intrinsic and extrinsic parameters for stereo reconstruction -->
<!-- TOOLING: OpenCV (offline calibration phase only) -->
<!-- OUTPUT: Calibration artifacts used by runtime pipeline -->

Stereo calibration is performed **offline** using pre-captured checkerboard images.

---

### 5.1 Calibration Image Capture Requirements

<!-- SUBSECTION: Image Capture Best Practices -->
<!-- SOURCE: Calib.io Knowledge Base (industry best practices) -->

The quality of calibration depends far more on image capture than solver choice.

#### Pattern Requirements

- High-quality printed checkerboard
- Flat and rigid mounting
- Known square size (e.g., 7.5 mm)
- Matte surface to avoid reflections

According to Calib.io, pattern flatness and accurate square size are critical to avoiding systematic calibration bias.[^1]

---

#### Number of Images

- Minimum: 15–20 usable stereo pairs
- Recommended: 25–40 diverse views

More views improve robustness and reduce overfitting.[^2]

---

#### Viewpoint Diversity (Critical)

Each stereo pair should vary in:

- Position (move across entire FOV)
- Orientation (tilt around all axes)
- Distance (near, mid, far range)
- Corner coverage (especially near image edges)

Calib.io emphasizes that calibration images must span the **entire sensor area**, especially corners, to properly constrain distortion parameters.[^3]

Avoid:
- All images centered
- All images parallel to camera
- All images at same distance

---

#### Lighting Conditions

- Even, diffuse lighting
- No motion blur
- No saturated pixels
- High contrast between black/white squares

Reflections and blur degrade subpixel corner accuracy.[^4]

---

#### Focus & Exposure Stability

- Lock exposure before capture
- Lock gain before capture
- Disable auto-exposure
- Do not refocus mid-sequence

Intrinsics are only valid for fixed focus and focal length.[^5]

---

### 5.2 OpenCV Calibration Workflow

<!-- SUBSECTION: OpenCV Solver Workflow -->

The OpenCV stereo calibration workflow proceeds as follows:

---

#### Step 1 — Detect Corners (Left & Right Independently)

- `cv::findChessboardCorners`
- `cv::cornerSubPix` refinement
- Validate reprojection residuals

Corner detection happens independently per camera.

---

#### Step 2 — Calibrate Each Camera Intrinsics

- `cv::calibrateCamera`
- Estimate:
  - fx, fy
  - cx, cy
  - distortion coefficients

This produces per-camera intrinsic models.

---

#### Step 3 — Stereo Calibration

- `cv::stereoCalibrate`
- Solve for:
  - Rotation (R)
  - Translation (T)
  - Essential and Fundamental matrices

This defines stereo geometry.

---

#### Step 4 — Stereo Rectification

- `cv::stereoRectify`
- Compute rectification transforms
- Generate undistort/rectify maps

---

#### Step 5 — Determine Valid Stereo ROI

- Extract valid overlapping region
- Store as runtime ROI mask

---

#### Step 6 — Export Artifacts

Export:
- Intrinsic matrices
- Distortion coefficients
- R, T
- Rectification maps
- Valid ROI bounds

Runtime system does not depend on OpenCV.

---

### 5.3 Stereo Calibration Workflow Diagram (Offline)

<!-- DIAGRAM: Stereo Calibration Workflow (Parallelized – Offline) -->

```mermaid
flowchart TD

A[Offline Dataset<br/>Stereo Image Pairs<br/>Checkerboard Views]

A --> B1[Left Image Set]
A --> B2[Right Image Set]

subgraph LEFT_PIPELINE [Left Camera Pipeline]
    direction TB
    B1 --> C1[Detect Checkerboard Corners]
    C1 --> D1[Subpixel Corner Refinement]
    D1 --> E1[Calibrate Left Intrinsics]
end

subgraph RIGHT_PIPELINE [Right Camera Pipeline]
    direction TB
    B2 --> C2[Detect Checkerboard Corners]
    C2 --> D2[Subpixel Corner Refinement]
    D2 --> E2[Calibrate Right Intrinsics]
end

E1 --> F[Stereo Calibration<br/>Solve R and T]
E2 --> F

F --> G[Stereo Rectification]

G --> H[Compute Valid Stereo ROI]

G --> I[Generate Rectification Maps]

I --> J[Export Calibration Artifacts]
```

---

## 6. Calibration Validation

<!-- SECTION: Calibration Validation -->
<!-- OBJECTIVE: Confirm calibration accuracy before deployment -->

### Validation Metrics

- Reprojection error (target < 0.3–0.5 px typical)
- Visual straightness of rectified lines
- Epipolar alignment check
- Depth sanity check on known geometry

### Red Flags

- High distortion at corners
- Depth skew across image
- Systematic reprojection bias
- Large residual error in edge regions

If calibration quality is poor:
- Re-evaluate image diversity
- Re-check pattern flatness
- Confirm focus stability

---

# References

[^1]: Calib.io Knowledge Base – Importance of calibration target flatness and manufacturing precision.
[^2]: Calib.io – Recommended number of calibration images for stable parameter estimation.
[^3]: Calib.io – Full image coverage required for accurate distortion modeling.
[^4]: Calib.io – Impact of blur and lighting on corner detection accuracy.
[^5]: Calib.io – Intrinsics depend on fixed focus and focal settings.
