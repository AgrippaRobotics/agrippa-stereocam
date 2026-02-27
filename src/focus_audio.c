/*
 * focus_audio.c — procedural stereo audio feedback for focus mode
 */

#include "focus_audio.h"

#include <math.h>
#include <stdio.h>

#include <SDL2/SDL.h>

#define AG_FOCUS_AUDIO_RATE             48000
#define AG_FOCUS_AUDIO_BUFFER_SAMPLES   512
#define AG_FOCUS_AUDIO_CHANNELS         2

#define AG_FOCUS_BASE_FREQ              700.0f
#define AG_FOCUS_MAX_OFFSET_HZ           30.0f
#define AG_FOCUS_LOCK_THRESHOLD           0.05f
#define AG_FOCUS_LOCK_HOLD_SECONDS        1.0f

#define AG_FOCUS_BEEP_FREQ            1000.0f
#define AG_FOCUS_BEEP_SECONDS            0.12f
#define AG_FOCUS_BEEP_PAUSE_SECONDS      1.0f

#define AG_FOCUS_BASE_AMP                0.10f
#define AG_FOCUS_BEEP_AMP                0.20f
#define AG_FOCUS_OFFSET_SMOOTHING        0.02f
#define AG_FOCUS_DELTA_SCALE        1000000.0f

typedef struct {
    SDL_AudioDeviceID device;
    SDL_atomic_t      normalized_delta_scaled;
    float             left_phase;
    float             right_phase;
    float             beep_phase;
    float             current_offset_hz;
    float             stable_seconds;
    int               locked;
    int               beep_cycle_pos;
    int               beep_segment;
} FocusAudioState;

static FocusAudioState g_focus_audio;

static float
clampf (float value, float min_value, float max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static float
wrap_phase (float phase)
{
    const float two_pi = 2.0f * (float) M_PI;

    while (phase >= two_pi)
        phase -= two_pi;
    while (phase < 0.0f)
        phase += two_pi;
    return phase;
}

static void
focus_audio_callback (void *userdata, Uint8 *stream, int len)
{
    FocusAudioState *state = (FocusAudioState *) userdata;
    float *samples = (float *) stream;
    int frames = len / (int) sizeof (float) / AG_FOCUS_AUDIO_CHANNELS;
    int beep_samples = (int) (AG_FOCUS_BEEP_SECONDS * AG_FOCUS_AUDIO_RATE);
    int pause_samples = (int) (AG_FOCUS_BEEP_PAUSE_SECONDS * AG_FOCUS_AUDIO_RATE);
    int cycle_samples = beep_samples * 2 + pause_samples;
    const float two_pi = 2.0f * (float) M_PI;

    for (int i = 0; i < frames; i++) {
        float normalized_delta =
            (float) SDL_AtomicGet (&state->normalized_delta_scaled) /
            AG_FOCUS_DELTA_SCALE;
        float target_offset_hz = clampf (normalized_delta, -1.0f, 1.0f) *
                                 AG_FOCUS_MAX_OFFSET_HZ;
        float abs_delta = fabsf (normalized_delta);
        float left_sample = 0.0f;
        float right_sample = 0.0f;

        state->current_offset_hz += AG_FOCUS_OFFSET_SMOOTHING *
                                    (target_offset_hz - state->current_offset_hz);

        if (abs_delta < AG_FOCUS_LOCK_THRESHOLD) {
            state->stable_seconds += 1.0f / AG_FOCUS_AUDIO_RATE;
            if (!state->locked &&
                state->stable_seconds >= AG_FOCUS_LOCK_HOLD_SECONDS) {
                state->locked = 1;
                state->beep_cycle_pos = 0;
                state->beep_segment = -1;
                state->beep_phase = 0.0f;
            }
        } else {
            state->stable_seconds = 0.0f;
            state->locked = 0;
            state->beep_cycle_pos = 0;
            state->beep_segment = -1;
        }

        if (!state->locked) {
            float convergence = 1.0f - fabsf (clampf (normalized_delta, -1.0f, 1.0f));
            float amplitude = AG_FOCUS_BASE_AMP * (0.5f + 0.5f * convergence);
            float left_step = two_pi * AG_FOCUS_BASE_FREQ / AG_FOCUS_AUDIO_RATE;
            float right_freq = AG_FOCUS_BASE_FREQ + state->current_offset_hz;
            float right_step = two_pi * right_freq / AG_FOCUS_AUDIO_RATE;

            left_sample = sinf (state->left_phase) * amplitude;
            right_sample = sinf (state->right_phase) * amplitude;

            state->left_phase = wrap_phase (state->left_phase + left_step);
            state->right_phase = wrap_phase (state->right_phase + right_step);
        } else {
            int segment;
            int segment_pos;

            if (state->beep_cycle_pos < beep_samples) {
                segment = 0;
                segment_pos = state->beep_cycle_pos;
            } else if (state->beep_cycle_pos < beep_samples * 2) {
                segment = 1;
                segment_pos = state->beep_cycle_pos - beep_samples;
            } else {
                segment = 2;
                segment_pos = 0;
            }

            if (segment != state->beep_segment) {
                state->beep_segment = segment;
                if (segment != 2)
                    state->beep_phase = 0.0f;
            }

            if (segment != 2) {
                float envelope = 1.0f;
                float step = two_pi * AG_FOCUS_BEEP_FREQ / AG_FOCUS_AUDIO_RATE;
                float sample = sinf (state->beep_phase) * AG_FOCUS_BEEP_AMP;

                if (beep_samples > 1)
                    envelope = 1.0f - ((float) segment_pos / (float) (beep_samples - 1));

                sample *= envelope;
                state->beep_phase = wrap_phase (state->beep_phase + step);

                if (segment == 0)
                    left_sample = sample;
                else
                    right_sample = sample;
            }

            state->beep_cycle_pos++;
            if (state->beep_cycle_pos >= cycle_samples)
                state->beep_cycle_pos = 0;
        }

        samples[i * 2] = left_sample;
        samples[i * 2 + 1] = right_sample;
    }
}

gboolean
focus_audio_init (void)
{
    SDL_AudioSpec desired;

    SDL_zero (g_focus_audio);
    SDL_AtomicSet (&g_focus_audio.normalized_delta_scaled, 0);

    SDL_zero (desired);
    desired.freq = AG_FOCUS_AUDIO_RATE;
    desired.format = AUDIO_F32SYS;
    desired.channels = AG_FOCUS_AUDIO_CHANNELS;
    desired.samples = AG_FOCUS_AUDIO_BUFFER_SAMPLES;
    desired.callback = focus_audio_callback;
    desired.userdata = &g_focus_audio;

    g_focus_audio.device = SDL_OpenAudioDevice (NULL, 0, &desired, NULL, 0);
    if (!g_focus_audio.device) {
        fprintf (stderr, "warn: focus audio unavailable: %s\n", SDL_GetError ());
        return FALSE;
    }

    SDL_PauseAudioDevice (g_focus_audio.device, 0);
    return TRUE;
}

void
focus_audio_update_delta (float normalized_delta)
{
    float clamped = clampf (normalized_delta, -1.0f, 1.0f);
    int scaled = (int) lroundf (clamped * AG_FOCUS_DELTA_SCALE);

    SDL_AtomicSet (&g_focus_audio.normalized_delta_scaled, scaled);
}

void
focus_audio_shutdown (void)
{
    if (g_focus_audio.device) {
        SDL_CloseAudioDevice (g_focus_audio.device);
        g_focus_audio.device = 0;
    }
}
