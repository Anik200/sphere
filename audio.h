#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int audio_init(void);

void audio_set_standby_mode(void);
void audio_set_listening_mode(void);

int audio_check_wake_detected(void);
int audio_check_interrupted(int *out_wake_retrigger);
int audio_check_exit_detected(void);
int audio_check_timeout_detected(void);

int audio_get_final_speech(char *out_text, size_t max_len);

float audio_get_current_amp(void);

void audio_speak(const char *text);
void audio_speak_exit(const char *text);
void audio_stop_speech(void);
int audio_is_speaking(void);

void audio_play_music(const char *url);
void audio_stop_music(void);
int audio_is_music_playing(void);

void audio_cleanup(void);

#ifdef __cplusplus
}
#endif
#endif
