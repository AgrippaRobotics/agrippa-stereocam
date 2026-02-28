# Focus Audio

The `focus` command includes an operator-feedback audio path built on SDL2's procedural audio support.

## Purpose

The audio path exists so an operator can align lenses without continuously watching the screen.

## Operating modes

There are two modes:

1. Convergence mode while left and right focus scores differ
2. Locked mode once the mismatch remains below threshold for a stable interval

## Convergence mode

The design uses a stereo beating tone:

- base frequency around 700 Hz,
- left and right channels hard-panned,
- frequency offset proportional to the normalized focus mismatch.

As the channels converge, the beating slows.

## Locked mode

When the normalized mismatch stays within threshold long enough, the command:

- mutes the base tones,
- switches to alternating confirmation beeps,
- keeps repeating that pattern until alignment drifts.

## Implementation constraints

- 48 kHz sample rate
- `AUDIO_F32SYS`
- procedural callback with no allocations
- shared state updated from the main thread
- smoothed frequency offset to avoid zipper noise

## UX goals

The sound should be:

- deterministic,
- low-latency,
- clear,
- non-fatiguing,
- useful as instrumentation rather than decoration.

The original interface spec remains in the repo root as [../../focus_audio_interface.md](../../focus_audio_interface.md).
