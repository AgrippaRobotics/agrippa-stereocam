/*
 * focus_audio.h — procedural stereo audio feedback for focus mode
 */

#ifndef AG_FOCUS_AUDIO_H
#define AG_FOCUS_AUDIO_H

#include <glib.h>

gboolean focus_audio_init (void);
void     focus_audio_update_delta (float normalized_delta);
void     focus_audio_shutdown (void);

#endif /* AG_FOCUS_AUDIO_H */
