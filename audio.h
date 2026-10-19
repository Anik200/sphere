#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float overall_amp;   /* 0.0 to 1.0 smoothed audio amplitude */
    float bass;          /* 0.0 to 1.0 low-frequency kick / bass energy */
    float mid;           /* 0.0 to 1.0 midrange vocal / melody energy */
    float treble;        /* 0.0 to 1.0 high-frequency percussion / hi-hat energy */
    float beat_pulse;    /* 0.0 to 1.0 transient beat hit impulse (decays fast) */
    int is_beat;         /* 1 if beat hit on this frame */
} AudioVisData;

int audio_init(void);

void audio_set_standby_mode(void);
void audio_set_listening_mode(void);

int audio_check_wake_detected(void);
int audio_check_interrupted(int *out_wake_retrigger);
int audio_check_exit_detected(void);
int audio_check_timeout_detected(void);

int audio_get_final_speech(char *out_text, size_t max_len);

float audio_get_current_amp(void);
void audio_get_vis_data(AudioVisData *out_vis);

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
