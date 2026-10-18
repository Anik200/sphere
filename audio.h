#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize voice audio engine (spawns sphere_voice_service.exe)
int audio_init(void);

// Mode control
void audio_set_standby_mode(void);
void audio_set_listening_mode(void);

// Events from voice service
int audio_check_wake_detected(void);
int audio_check_interrupted(int *out_wake_retrigger);
int audio_check_exit_detected(void);
int audio_check_timeout_detected(void);

// Retrieve recognized speech query
int audio_get_final_speech(char *out_text, size_t max_len);

// Normalized microphone amplitude [0.0 - 1.0] for particle deformation
float audio_get_current_amp(void);

// Speech synthesis with word-by-word streaming
void audio_speak(const char *text);
void audio_stop_speech(void);
int audio_is_speaking(void);

// Music streaming and playback
void audio_play_music(const char *url);
void audio_stop_music(void);
int audio_is_music_playing(void);

// Clean up voice service
void audio_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_H
