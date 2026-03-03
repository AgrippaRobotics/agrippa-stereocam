# Focus Audio

The `focus` command includes an operator-feedback audio path built on SDL2's procedural audio support.

## Purpose

The audio path exists so an operator can align lenses without continuously watching the screen.

## Operating modes

There are two modes:

1. **Convergence mode** while left and right focus scores differ
2. **Locked mode** once the mismatch remains below threshold for a stable interval

---

## Convergence mode

The design uses a stereo beating tone:

- Base frequency: **700 Hz**
- Left channel: 700 Hz
- Right channel: 700 Hz + frequency offset
- Hard stereo pan: left tone to left speaker, right tone to right speaker

This creates audible beating that slows as alignment improves.

### Frequency offset mapping

Map focus mismatch to frequency difference:

```c
float normalized_delta = clamp(delta / delta_scale, -1.0f, 1.0f);
float freq_offset = normalized_delta * 30.0f;  // +/-30 Hz max
```

- `delta_scale` normalizes the expected focus metric range.
- Maximum offset: +/-30 Hz.
- At perfect match: offset = 0 Hz.

### Optional amplitude scaling

```c
float convergence = 1.0f - fabs(normalized_delta);
float amplitude = base_amp * (0.5f + 0.5f * convergence);
```

Keep subtle. Beating clarity is more important than loudness variation.

---

## Lock detection

Lock when:

```c
fabs(normalized_delta) < 0.03f
```

The threshold must hold continuously for >= 1.0 seconds. Reset the timer immediately if the threshold is exceeded.

---

## Locked mode

When locked:

- Immediately mute base tones.
- Play repeating spatial confirmation pattern: left beep, right beep, 1 second pause, repeat.
- Loop while lock condition remains true.
- If alignment drifts, immediately return to convergence mode.

### Beep characteristics

- Frequency: **1000 Hz sine**
- Duration: **120 ms**
- Envelope: linear decay (avoid clicks)
- First beep: left channel only
- Second beep: right channel only
- Pause: ~1 second silence

This produces a clean metrology-style confirmation cue.

---

## SDL2 audio implementation

### Audio format

- Sample rate: **48 kHz**
- Format: `AUDIO_F32SYS`
- Buffer size: 512 samples

### Architecture

Use SDL2 procedural audio via callback:

- Maintain phase accumulators for left base tone, right base tone, and beep oscillator.
- No memory allocation inside the callback.
- No blocking operations.

### Shared state

Main thread:

- Computes focus metrics per frame.
- Updates shared atomic: `normalized_delta`.

Audio thread:

- Reads `normalized_delta`.
- Computes frequency offset.
- Maintains lock timer state machine.
- Generates samples.

### Frequency smoothing

To avoid zipper noise:

```c
current_offset += 0.02f * (target_offset - current_offset);
```

---

## UX goals

The sound should be:

- Deterministic
- Low-latency
- Clear
- Non-fatiguing
- Useful as instrumentation rather than decoration

---

## Implementation

The logic is encapsulated in `focus_audio.c` / `focus_audio.h`, exposing:

```
focus_audio_init()
focus_audio_update_delta(float normalized_delta)
focus_audio_shutdown()
```

This keeps the `focus` command logic clean and readable.
