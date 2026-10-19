#define _USE_MATH_DEFINES
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static Particle particles[NUM_PARTICLES];
static unsigned char braille_grid[TERM_H][TERM_W];
static char frame_buffer[65536];

static const unsigned char pixel_map[4][2] = {
    {0x01, 0x08},
    {0x02, 0x10},
    {0x04, 0x20},
    {0x40, 0x80}
};

static volatile AgentState current_state = STATE_IDLE;
static volatile AgentState previous_state = STATE_IDLE;
static volatile float current_voice_amp = 0.0f;
static AudioVisData current_vis = {0};
static volatile float current_angle = 0.0f;
static volatile float current_morph = 0.0f;
static volatile BallExpression current_expression = EXPR_NONE;
static volatile int expression_countdown = 0;
static volatile float music_phase = 0.0f;
static volatile float music_beat_wave = 0.0f;
static volatile float ripple_phase = 0.0f;
static char custom_color[32] = "";

static char prev_color[32] = COLOR_GREEN;
static char active_target_color[32] = COLOR_GREEN;
static float bloom_progress = 1.0f;
static float bloom_speed = 0.05f;

static char current_status[128] = "Ready. Press Space to talk or type a prompt.";
static char current_subtitle[4096] = "";
static char current_input_buffer[256] = "";

#ifdef _WIN32
static CRITICAL_SECTION render_lock;
#else
static pthread_mutex_t render_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void lock_renderer(void) {
#ifdef _WIN32
    EnterCriticalSection(&render_lock);
#else
    pthread_mutex_lock(&render_lock);
#endif
}

static void unlock_renderer(void) {
#ifdef _WIN32
    LeaveCriticalSection(&render_lock);
#else
    pthread_mutex_unlock(&render_lock);
#endif
}

static void setup_expression_targets(BallExpression expr) {
    float pi = (float)M_PI;
    switch (expr) {
        case EXPR_HAPPY:
            for (int i = 0; i < NUM_PARTICLES; i++) {
                if (i < 100) {
                    float a = (i / 100.0f) * 2.0f * pi;
                    particles[i].tx = -0.38f + cosf(a) * 0.14f;
                    particles[i].ty = -0.32f + sinf(a) * 0.14f;
                    particles[i].tz = 0.0f;
                } else if (i < 200) {
                    float a = ((i - 100) / 100.0f) * 2.0f * pi;
                    particles[i].tx = 0.38f + cosf(a) * 0.14f;
                    particles[i].ty = -0.32f + sinf(a) * 0.14f;
                    particles[i].tz = 0.0f;
                } else if (i < 240) {
                    float a = ((i - 200) / 40.0f) * 2.0f * pi;
                    particles[i].tx = -0.60f + cosf(a) * 0.08f;
                    particles[i].ty = 0.02f + sinf(a) * 0.05f;
                    particles[i].tz = 0.0f;
                } else if (i < 280) {
                    float a = ((i - 240) / 40.0f) * 2.0f * pi;
                    particles[i].tx = 0.60f + cosf(a) * 0.08f;
                    particles[i].ty = 0.02f + sinf(a) * 0.05f;
                    particles[i].tz = 0.0f;
                } else {
                    float t = (i - 280) / (float)(NUM_PARTICLES - 280);
                    float a = pi * 0.12f + t * pi * 0.76f;
                    float r = 0.68f + ((i % 2 == 0) ? 0.03f : -0.03f);
                    particles[i].tx = cosf(a) * r;
                    particles[i].ty = sinf(a) * (r * 0.85f);
                    particles[i].tz = 0.0f;
                }
            }
            break;

        case EXPR_SAD:
            for (int i = 0; i < NUM_PARTICLES; i++) {
                if (i < 100) {
                    float a = (i / 100.0f) * 2.0f * pi;
                    particles[i].tx = -0.38f + cosf(a) * 0.14f;
                    particles[i].ty = -0.26f + sinf(a) * 0.08f;
                    particles[i].tz = 0.0f;
                } else if (i < 200) {
                    float a = ((i - 100) / 100.0f) * 2.0f * pi;
                    particles[i].tx = 0.38f + cosf(a) * 0.14f;
                    particles[i].ty = -0.26f + sinf(a) * 0.08f;
                    particles[i].tz = 0.0f;
                } else if (i < 250) {
                    float t = (i - 200) / 50.0f;
                    particles[i].tx = -0.15f - t * 0.40f;
                    particles[i].ty = -0.52f + t * 0.18f;
                    particles[i].tz = 0.0f;
                } else if (i < 300) {
                    float t = (i - 250) / 50.0f;
                    particles[i].tx = 0.15f + t * 0.40f;
                    particles[i].ty = -0.52f + t * 0.18f;
                    particles[i].tz = 0.0f;
                } else {
                    float t = (i - 300) / (float)(NUM_PARTICLES - 300);
                    float a = pi * 0.16f + t * pi * 0.68f;
                    float r = 0.65f + ((i % 2 == 0) ? 0.02f : -0.02f);
                    particles[i].tx = cosf(a) * r;
                    particles[i].ty = 0.48f - sinf(a) * (r * 0.65f);
                    particles[i].tz = 0.0f;
                }
            }
            break;

        case EXPR_SURPRISED:
            for (int i = 0; i < NUM_PARTICLES; i++) {
                if (i < 120) {
                    float a = (i / 120.0f) * 2.0f * pi;
                    particles[i].tx = -0.38f + cosf(a) * 0.22f;
                    particles[i].ty = -0.28f + sinf(a) * 0.22f;
                    particles[i].tz = 0.0f;
                } else if (i < 240) {
                    float a = ((i - 120) / 120.0f) * 2.0f * pi;
                    particles[i].tx = 0.38f + cosf(a) * 0.22f;
                    particles[i].ty = -0.28f + sinf(a) * 0.22f;
                    particles[i].tz = 0.0f;
                } else if (i < 300) {
                    float t = (i - 240) / 60.0f;
                    float x = (t < 0.5f) ? (-0.55f + t * 0.70f) : (0.20f + (t - 0.5f) * 0.70f);
                    particles[i].tx = x;
                    particles[i].ty = -0.68f;
                    particles[i].tz = 0.0f;
                } else {
                    float a = ((i - 300) / (float)(NUM_PARTICLES - 300)) * 2.0f * pi;
                    particles[i].tx = cosf(a) * 0.24f;
                    particles[i].ty = 0.38f + sinf(a) * 0.32f;
                    particles[i].tz = 0.0f;
                }
            }
            break;

        case EXPR_ANGRY:
            for (int i = 0; i < NUM_PARTICLES; i++) {
                if (i < 80) {
                    float t = i / 80.0f;
                    particles[i].tx = -0.58f + t * 0.48f;
                    particles[i].ty = -0.48f + t * 0.26f;
                    particles[i].tz = 0.0f;
                } else if (i < 160) {
                    float t = (i - 80) / 80.0f;
                    particles[i].tx = 0.58f - t * 0.48f;
                    particles[i].ty = -0.48f + t * 0.26f;
                    particles[i].tz = 0.0f;
                } else if (i < 240) {
                    float a = ((i - 160) / 80.0f) * 2.0f * pi;
                    particles[i].tx = -0.38f + cosf(a) * 0.16f;
                    particles[i].ty = -0.18f + sinf(a) * 0.06f;
                    particles[i].tz = 0.0f;
                } else if (i < 320) {
                    float a = ((i - 240) / 80.0f) * 2.0f * pi;
                    particles[i].tx = 0.38f + cosf(a) * 0.16f;
                    particles[i].ty = -0.18f + sinf(a) * 0.06f;
                    particles[i].tz = 0.0f;
                } else {
                    float t = (i - 320) / (float)(NUM_PARTICLES - 320);
                    particles[i].tx = -0.55f + t * 1.10f;
                    particles[i].ty = 0.42f + ((i % 4 == 0) ? -0.06f : ((i % 4 == 2) ? 0.06f : 0.0f));
                    particles[i].tz = 0.0f;
                }
            }
            break;

        case EXPR_WINK:
            for (int i = 0; i < NUM_PARTICLES; i++) {
                if (i < 100) {
                    float t = i / 100.0f;
                    particles[i].tx = -0.52f + t * 0.28f;
                    particles[i].ty = -0.30f - sinf(t * pi) * 0.12f;
                    particles[i].tz = 0.0f;
                } else if (i < 200) {
                    float a = ((i - 100) / 100.0f) * 2.0f * pi;
                    particles[i].tx = 0.38f + cosf(a) * 0.15f;
                    particles[i].ty = -0.32f + sinf(a) * 0.15f;
                    particles[i].tz = 0.0f;
                } else if (i < 260) {
                    float a = ((i - 200) / 60.0f) * 2.0f * pi;
                    particles[i].tx = 0.60f + cosf(a) * 0.08f;
                    particles[i].ty = 0.02f + sinf(a) * 0.05f;
                    particles[i].tz = 0.0f;
                } else {
                    float t = (i - 260) / (float)(NUM_PARTICLES - 260);
                    float a = pi * 0.10f + t * pi * 0.75f;
                    float r = 0.65f + ((i % 2 == 0) ? 0.02f : -0.02f);
                    particles[i].tx = cosf(a) * r;
                    particles[i].ty = sinf(a) * (r * 0.70f) - t * 0.10f;
                    particles[i].tz = 0.0f;
                }
            }
            break;

        case EXPR_COOL:
            for (int i = 0; i < NUM_PARTICLES; i++) {
                if (i < 80) {
                    float t = i / 80.0f;
                    particles[i].tx = -0.68f + t * 1.36f;
                    particles[i].ty = -0.38f;
                    particles[i].tz = 0.0f;
                } else if (i < 200) {
                    float a = ((i - 80) / 120.0f) * 2.0f * pi;
                    particles[i].tx = -0.38f + cosf(a) * 0.24f;
                    particles[i].ty = -0.22f + sinf(a) * 0.16f;
                    particles[i].tz = 0.0f;
                } else if (i < 320) {
                    float a = ((i - 200) / 120.0f) * 2.0f * pi;
                    particles[i].tx = 0.38f + cosf(a) * 0.24f;
                    particles[i].ty = -0.22f + sinf(a) * 0.16f;
                    particles[i].tz = 0.0f;
                } else {
                    float t = (i - 320) / (float)(NUM_PARTICLES - 320);
                    float a = pi * 0.12f + t * pi * 0.65f;
                    particles[i].tx = cosf(a) * 0.58f;
                    particles[i].ty = 0.38f + sinf(a) * 0.28f;
                    particles[i].tz = 0.0f;
                }
            }
            break;

        case EXPR_NONE:
        default:
            break;
    }
}

void renderer_init(void) {
#ifdef _WIN32
    InitializeCriticalSection(&render_lock);
    SetConsoleOutputCP(65001);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif

    printf("\033[?1049h\033[?25l\033[2J\033[H");
    fflush(stdout);

    float golden_angle = (float)M_PI * (3.0f - sqrtf(5.0f));
    for (int i = 0; i < NUM_PARTICLES; i++) {
        float y = 1.0f - (i / (float)(NUM_PARTICLES - 1)) * 2.0f;
        float radius = sqrtf(1.0f - y * y);
        float theta = golden_angle * i;
        particles[i].sx = cosf(theta) * radius;
        particles[i].sy = y;
        particles[i].sz = sinf(theta) * radius;
    }
    setup_expression_targets(EXPR_HAPPY);

    FILE *f_col = fopen("sphere_color.cfg", "r");
    if (f_col) {
        char buf[64];
        if (fgets(buf, sizeof(buf), f_col)) {
            size_t l = strlen(buf);
            while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r' || buf[l - 1] == ' ')) {
                buf[--l] = '\0';
            }
            if (l > 0) {
                strncpy(custom_color, buf, sizeof(custom_color) - 1);
                custom_color[sizeof(custom_color) - 1] = '\0';
            }
        }
        fclose(f_col);
    }

    if (custom_color[0] != '\0') {
        snprintf(prev_color, sizeof(prev_color), "%s", custom_color);
        snprintf(active_target_color, sizeof(active_target_color), "%s", custom_color);
    } else {
        snprintf(prev_color, sizeof(prev_color), "%s", COLOR_GREEN);
        snprintf(active_target_color, sizeof(active_target_color), "%s", COLOR_GREEN);
    }
    bloom_progress = 1.0f;
}

void renderer_set_state(AgentState state) {
    lock_renderer();
    if (state == STATE_HAPPY) {
        if (current_state != STATE_HAPPY) {
            previous_state = current_state;
        }
        setup_expression_targets(EXPR_HAPPY);
        current_expression = EXPR_HAPPY;
        expression_countdown = 180;
    }
    current_state = state;
    unlock_renderer();
}

AgentState renderer_get_state(void) {
    return current_state;
}

void renderer_set_expression(BallExpression expr) {
    lock_renderer();
    current_expression = expr;
    if (expr != EXPR_NONE) {
        setup_expression_targets(expr);
        expression_countdown = 240;
    } else {
        expression_countdown = 0;
    }
    unlock_renderer();
}

BallExpression renderer_get_expression(void) {
    return current_expression;
}

void renderer_set_custom_color(const char *ansi_color) {
    lock_renderer();
    if (ansi_color && strlen(ansi_color) > 0) {
        strncpy(custom_color, ansi_color, sizeof(custom_color) - 1);
        custom_color[sizeof(custom_color) - 1] = '\0';
        FILE *f_col = fopen("sphere_color.cfg", "w");
        if (f_col) {
            fprintf(f_col, "%s\n", custom_color);
            fclose(f_col);
        }
    } else {
        custom_color[0] = '\0';
        remove("sphere_color.cfg");
    }
    unlock_renderer();
}

const char *renderer_get_custom_color(void) {
    return custom_color;
}

void renderer_set_voice_amp(float amp) {
    if (amp < 0.0f) amp = 0.0f;
    if (amp > 1.0f) amp = 1.0f;
    current_voice_amp = amp;
    current_vis.overall_amp = amp;
}

void renderer_set_vis_data(const AudioVisData *vis_data) {
    if (vis_data) {
        current_vis = *vis_data;
        current_voice_amp = vis_data->overall_amp;
    }
}

void renderer_set_status(const char *status_text) {
    lock_renderer();
    if (status_text) {
        strncpy(current_status, status_text, sizeof(current_status) - 1);
        current_status[sizeof(current_status) - 1] = '\0';
    }
    unlock_renderer();
}

void renderer_set_subtitle(const char *subtitle_text) {
    lock_renderer();
    if (subtitle_text) {
        strncpy(current_subtitle, subtitle_text, sizeof(current_subtitle) - 1);
        current_subtitle[sizeof(current_subtitle) - 1] = '\0';
    } else {
        current_subtitle[0] = '\0';
    }
    unlock_renderer();
}

void renderer_set_input_buffer(const char *input_text) {
    lock_renderer();
    if (input_text) {
        strncpy(current_input_buffer, input_text, sizeof(current_input_buffer) - 1);
        current_input_buffer[sizeof(current_input_buffer) - 1] = '\0';
    } else {
        current_input_buffer[0] = '\0';
    }
    unlock_renderer();
}

void renderer_trigger_happy(void) {
    renderer_set_state(STATE_HAPPY);
}

static inline void set_subpixel(int px, int py, int max_w, int max_h) {
    if (px < 0 || px >= max_w || py < 0 || py >= max_h) return;
    braille_grid[py / 4][px / 2] |= pixel_map[py % 4][px % 2];
}

void renderer_render_frame(void) {
    lock_renderer();

    AgentState state = current_state;
    float voice_amp = current_voice_amp;

    if (current_expression != EXPR_NONE || state == STATE_HAPPY) {
        current_morph += (1.0f - current_morph) * 0.12f;
        if (expression_countdown > 0) {
            expression_countdown--;
            if (expression_countdown == 0) {
                current_expression = EXPR_NONE;
                if (state == STATE_HAPPY) current_state = previous_state;
            }
        }
    } else {
        current_morph += (0.0f - current_morph) * 0.12f;
    }

    float sound_energy = current_vis.overall_amp;
    if (sound_energy < voice_amp) sound_energy = voice_amp;

    if (state == STATE_MUSIC) {
        sound_energy = current_vis.overall_amp;
        music_phase += 0.045f + current_vis.bass * 0.12f + current_vis.mid * 0.08f;
        music_beat_wave += 0.16f + current_vis.beat_pulse * 0.28f;
    } else if (state == STATE_SPEAKING) {
        if (sound_energy < 0.10f) {
            float speak_pulse = 0.22f + 0.16f * sinf(current_angle * 3.2f) * cosf(current_angle * 1.6f);
            if (sound_energy < speak_pulse) sound_energy = speak_pulse;
        }
    }

    float rotation_speed = 0.035f;
    if (state == STATE_THINKING) {
        rotation_speed = 0.09f;
    } else if (state == STATE_MUSIC) {
        rotation_speed = 0.022f + current_vis.overall_amp * 0.060f + current_vis.beat_pulse * 0.040f;
    } else if (sound_energy > 0.03f) {
        rotation_speed += sound_energy * 0.22f;
    }
    current_angle += rotation_speed;

    ripple_phase += 0.16f;

    float angle = current_angle;
    float morph = current_morph;

    int term_cols = 80;
    int term_rows = 24;

#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        term_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        term_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        term_cols = ws.ws_col;
        term_rows = ws.ws_row;
    }
#endif

    int draw_h = TERM_H;
    if (term_rows < 27) {
        draw_h = term_rows - 8;
        if (draw_h < 10) draw_h = 10;
        if (draw_h > TERM_H) draw_h = TERM_H;
    }

    int draw_w = (int)(draw_h * 2.15f);
    if (draw_w > term_cols - 4) draw_w = term_cols - 4;
    if (draw_w > TERM_W) draw_w = TERM_W;
    if (draw_w < 20) draw_w = 20;

    int pix_w = draw_w * 2;
    int pix_h = draw_h * 4;

    float base_scale = (pix_h * 0.36f);
    if (state == STATE_MUSIC) {
        float music_pump = current_vis.bass * 0.30f + current_vis.beat_pulse * 0.25f + current_vis.overall_amp * 0.08f;
        base_scale *= (1.0f + music_pump);
    } else if (sound_energy > 0.03f) {
        base_scale *= (1.0f + sound_energy * 0.16f);
    } else if (state == STATE_THINKING) {
        base_scale *= (1.0f + 0.10f * sinf(angle * 3.0f));
    }

    memset(braille_grid, 0, sizeof(braille_grid));

    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    float cos_pitch = cosf(angle * 0.35f);
    float sin_pitch = sinf(angle * 0.35f);

    for (int i = 0; i < NUM_PARTICLES; i++) {
        float rx = particles[i].sx * cos_a - particles[i].sz * sin_a;
        float rz = particles[i].sx * sin_a + particles[i].sz * cos_a;
        float ry = particles[i].sy * cos_pitch - rz * sin_pitch;
        rz       = particles[i].sy * sin_pitch + rz * cos_pitch;

        if (state == STATE_MUSIC) {
            float r_center = sqrtf(particles[i].sx * particles[i].sx + particles[i].sy * particles[i].sy + particles[i].sz * particles[i].sz);

            /* 1. Beat shockwave ring expanding outward from center on drum kicks */
            float shock = sinf(r_center * 8.5f - music_beat_wave) * (current_vis.beat_pulse * 0.40f + current_vis.bass * 0.22f);

            /* 2. Subwoofer vertical expansion / pole bounce on bass kicks */
            float bass_lat = sinf(particles[i].sy * 5.0f - music_phase * 2.2f) * (current_vis.bass * 0.30f + current_vis.beat_pulse * 0.20f);

            /* 3. Midrange melody & vocal equatorial twisting ripples */
            float theta = atan2f(particles[i].sz, particles[i].sx);
            float mid_lon = cosf(theta * 4.0f + music_phase * 2.0f) * (current_vis.mid * 0.26f);

            /* 4. Treble sparkle / percussion vibration on hi-hats and cymbals */
            float treble_twinkle = sinf(particles[i].sx * 14.0f + particles[i].sz * 14.0f + music_phase * 4.2f) * (current_vis.treble * 0.20f);

            /* Combined dynamic ripple */
            float ripple = 1.0f + shock + bass_lat + mid_lon + treble_twinkle;
            rx *= ripple;
            ry *= (1.0f + shock * 0.85f + bass_lat * 1.15f + current_vis.bass * 0.18f);
            rz *= ripple;
        } else if (sound_energy > 0.02f) {
            float wave_lat = sinf(particles[i].sy * 7.0f - ripple_phase * 2.2f) * (sound_energy * 0.18f);
            float theta = atan2f(particles[i].sz, particles[i].sx);
            float wave_lon = cosf(theta * 3.0f + ripple_phase * 1.8f) * (sound_energy * 0.14f);
            float radial_dist = particles[i].sx * particles[i].sx + particles[i].sz * particles[i].sz;
            float wave_shock = sinf(radial_dist * 9.0f - ripple_phase * 2.8f) * (sound_energy * 0.14f);
            float repel_force = sound_energy * 0.14f;
            float repel_band = sinf(particles[i].sy * 5.0f + ripple_phase * 1.5f) * (sound_energy * 0.10f);

            float ripple_repel = 1.0f + wave_lat + wave_lon + wave_shock + repel_force + repel_band;
            rx *= ripple_repel;
            ry *= (1.0f + wave_lat * 0.60f + sound_energy * 0.10f + repel_band);
            rz *= ripple_repel;
        }

        float fx = (1.0f - morph) * rx + morph * particles[i].tx;
        float fy = (1.0f - morph) * ry + morph * particles[i].ty;
        float fz = (1.0f - morph) * rz + morph * particles[i].tz;

        float fov = 2.6f / (2.6f - fz * 0.55f);
        int px = (int)(pix_w / 2.0f + fx * base_scale * fov);
        int py = (int)(pix_h / 2.0f + fy * base_scale * fov);
        set_subpixel(px, py, pix_w, pix_h);
    }

    const char *target_color;
    if (morph > 0.35f) {
        switch (current_expression) {
            case EXPR_HAPPY:     target_color = COLOR_GOLD; break;
            case EXPR_SAD:       target_color = COLOR_BLUE; break;
            case EXPR_SURPRISED: target_color = COLOR_CYAN; break;
            case EXPR_ANGRY:     target_color = COLOR_RED; break;
            case EXPR_WINK:      target_color = COLOR_YELLOW; break;
            case EXPR_COOL:      target_color = COLOR_PURPLE_BRIGHT; break;
            default:             target_color = COLOR_GOLD; break;
        }
    } else {
        switch (state) {
            case STATE_LISTENING:
                target_color = COLOR_PURPLE;
                break;
            case STATE_THINKING:
                target_color = COLOR_CYAN;
                break;
            case STATE_MUSIC:
                target_color = (custom_color[0] != '\0') ? custom_color : COLOR_CYAN;
                break;
            case STATE_SPEAKING:
            case STATE_IDLE:
            default:
                target_color = (custom_color[0] != '\0') ? custom_color : COLOR_GREEN;
                break;
        }
    }

    if (strcmp(target_color, active_target_color) != 0) {
        snprintf(prev_color, sizeof(prev_color), "%s", active_target_color);
        snprintf(active_target_color, sizeof(active_target_color), "%s", target_color);
        bloom_progress = 0.0f;

        if (state == STATE_LISTENING) {
            bloom_speed = 0.10f;
        } else {
            bloom_speed = 0.045f;
        }
    }

    if (bloom_progress < 1.0f) {
        bloom_progress += bloom_speed;
        if (bloom_progress >= 1.0f) {
            bloom_progress = 1.0f;
        }
    }

    const char *badge_name = "STANDBY";
    const char *badge_color = (custom_color[0] != '\0') ? custom_color : COLOR_GREEN;
    const char *badge_bg = COLOR_PILL_BG;
    const char *meter_label = "MIC";

    switch (state) {
        case STATE_LISTENING:
            badge_name = "LISTENING";
            badge_color = COLOR_PURPLE_BRIGHT;
            badge_bg = "\033[48;5;53m";
            meter_label = "MIC";
            break;
        case STATE_THINKING:
            badge_name = "THINKING";
            badge_color = COLOR_CYAN;
            badge_bg = "\033[48;5;24m";
            meter_label = "MIC";
            break;
        case STATE_SPEAKING:
            badge_name = "SPEAKING";
            badge_color = COLOR_GREEN_EMERALD;
            badge_bg = "\033[48;5;28m";
            meter_label = "AUDIO";
            break;
        case STATE_HAPPY:
            badge_name = "JOYFUL";
            badge_color = COLOR_GOLD;
            badge_bg = "\033[48;5;94m";
            meter_label = "AUDIO";
            break;
        case STATE_MUSIC:
            if (current_vis.beat_pulse > 0.38f) {
                badge_name = "BEAT \xE2\x97\x86";
                badge_color = COLOR_GOLD;
                badge_bg = "\033[48;5;88m";
            } else {
                badge_name = "MUSIC";
                badge_color = COLOR_PINK;
                badge_bg = "\033[48;5;53m";
            }
            meter_label = (current_vis.beat_pulse > 0.38f) ? "BEAT \xE2\x97\x8F" : "BEAT";
            break;
        case STATE_IDLE:
        default:
            badge_name = "STANDBY";
            badge_color = (custom_color[0] != '\0') ? custom_color : COLOR_GREEN;
            badge_bg = COLOR_PILL_BG;
            meter_label = "MIC";
            break;
    }

    int meter_len = 10;
    int filled = (int)(sound_energy * (float)meter_len);
    if (filled < 0) filled = 0;
    if (filled > meter_len) filled = meter_len;
    int empty = meter_len - filled;
    int pct = (int)(sound_energy * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 99) pct = 99;

    char meter_str[256];
    int m_pos = snprintf(meter_str, sizeof(meter_str),
                         COLOR_PILL_BG " " COLOR_DIM "%s" COLOR_RESET
                         COLOR_PILL_BG " \xE2\x96\x95%s",
                         meter_label, badge_color);
    for (int i = 0; i < filled; i++) {
        m_pos += snprintf(&meter_str[m_pos], sizeof(meter_str) - m_pos, "\xE2\x96\x88");
    }
    m_pos += snprintf(&meter_str[m_pos], sizeof(meter_str) - m_pos, COLOR_TRACK_BG);
    for (int i = 0; i < empty; i++) {
        m_pos += snprintf(&meter_str[m_pos], sizeof(meter_str) - m_pos, "\xE2\x96\x91");
    }
    m_pos += snprintf(&meter_str[m_pos], sizeof(meter_str) - m_pos,
                      COLOR_RESET COLOR_PILL_BG "\xE2\x96\x8F " COLOR_WHITE_BOLD "%2d%%" COLOR_RESET COLOR_PILL_BG " " COLOR_RESET,
                      pct);

    int vis_left_len = 2 + 1 + 1 + 8 + 2 + 1 + 1 + (int)strlen(badge_name) + 2 + 5;
    int vis_meter_len = 1 + (int)strlen(meter_label) + 1 + 1 + meter_len + 1 + 1 + 3 + 1;
    int max_status_len = term_cols - vis_left_len - vis_meter_len - 1;
    if (max_status_len < 6) max_status_len = 6;

    char status_clipped[256];
    snprintf(status_clipped, sizeof(status_clipped), "%.*s", max_status_len, current_status);
    int status_vis_len = (int)strlen(status_clipped);
    int spaces = term_cols - vis_left_len - status_vis_len - vis_meter_len;
    if (spaces < 1) spaces = 1;

    int pos = sprintf(frame_buffer,
                      "\033[H\033[2K  " COLOR_WHITE_BOLD "\xE2\x97\x86 SphereAI" COLOR_RESET
                      "  %s%s \xE2\x97\x8F %s " COLOR_RESET
                      " " COLOR_BORDER "\xE2\x94\x82" COLOR_RESET
                      " " COLOR_DIM "\xE2\x80\xBA" COLOR_RESET
                      " %s%*s%s\n",
                      badge_bg, badge_color, badge_name,
                      status_clipped, spaces, "", meter_str);

    int rule_len = term_cols > 6 ? term_cols - 4 : 40;
    pos += sprintf(&frame_buffer[pos], "\033[2K  " COLOR_BORDER);
    for (int i = 0; i < rule_len; i++) {
        pos += sprintf(&frame_buffer[pos], "\xE2\x94\x80");
    }
    pos += sprintf(&frame_buffer[pos], COLOR_RESET "\n");

    float center_x = draw_w / 2.0f;
    float center_y = draw_h / 2.0f;
    float max_radius = (draw_h / 2.0f) * 1.55f;
    float current_bloom_radius = bloom_progress * max_radius;
    float wave_edge = 1.8f;

    const char *current_pen = NULL;

    int pad_left = (term_cols - draw_w) / 2;
    if (pad_left < 2) pad_left = 2;

    for (int y = 0; y < draw_h; y++) {
        pos += sprintf(&frame_buffer[pos], "\033[2K%*s", pad_left, "");
        for (int x = 0; x < draw_w; x++) {
            unsigned char mask = braille_grid[y][x];
            if (mask == 0) {
                frame_buffer[pos++] = ' ';
            } else {
                const char *cell_color = active_target_color;

                if (bloom_progress < 1.0f) {
                    float dx = ((float)x + 0.5f - center_x);
                    float dy = ((float)y + 0.5f - center_y) * 2.0f;
                    float dist = sqrtf(dx * dx + dy * dy);

                    if (dist <= current_bloom_radius) {
                        if (dist >= current_bloom_radius - wave_edge && bloom_progress < 0.95f) {
                            cell_color = COLOR_WHITE_BOLD;
                        } else {
                            cell_color = active_target_color;
                        }
                    } else {
                        cell_color = prev_color;
                    }
                }

                if (cell_color != current_pen) {
                    pos += sprintf(&frame_buffer[pos], "%s", cell_color);
                    current_pen = cell_color;
                }

                frame_buffer[pos++] = (char)0xE2;
                frame_buffer[pos++] = (char)(0xA0 | (mask >> 6));
                frame_buffer[pos++] = (char)(0x80 | (mask & 0x3F));
            }
        }
        frame_buffer[pos++] = '\n';
    }

    pos += sprintf(&frame_buffer[pos], "%s\033[2K\n", COLOR_RESET);

    if (current_input_buffer[0] != '\0') {
        pos += sprintf(&frame_buffer[pos], "\033[2K  " COLOR_CYAN "User" COLOR_RESET " " COLOR_DIM "\xE2\x80\xBA" COLOR_RESET " " COLOR_WHITE_BOLD "%s\033[7m \033[0m\n", current_input_buffer);
    } else if (current_subtitle[0] != '\0') {
        const char *tag = "Sphere";
        const char *tag_color = COLOR_PURPLE;
        const char *text = current_subtitle;
        if (strncmp(current_subtitle, "Sphere: ", 8) == 0) {
            tag = "Sphere";
            tag_color = COLOR_PURPLE;
            text = current_subtitle + 8;
        } else if (strncmp(current_subtitle, "> ", 2) == 0) {
            tag = "User";
            tag_color = COLOR_CYAN;
            text = current_subtitle + 2;
        } else if (strncmp(current_subtitle, "Listening: \"", 12) == 0) {
            tag = "User";
            tag_color = COLOR_CYAN;
        }

        int max_w = term_cols > 18 ? term_cols - 16 : 40;
        int max_lines = (term_rows < 27) ? 2 : 4;
        int len = (int)strlen(text);
        int start = 0;
        int line_idx = 0;

        while (start < len && line_idx < max_lines) {
            int end = start + max_w;
            if (end >= len) {
                end = len;
            } else {
                int s = end;
                while (s > start && text[s] != ' ') s--;
                if (s > start) end = s;
            }

            if (line_idx == 0) {
                pos += sprintf(&frame_buffer[pos], "\033[2K  %s%s" COLOR_RESET " " COLOR_DIM "\xE2\x80\xBA" COLOR_RESET " " COLOR_WHITE_BOLD "%.*s" COLOR_RESET "\n",
                               tag_color, tag, end - start, &text[start]);
            } else {
                pos += sprintf(&frame_buffer[pos], "\033[2K          " COLOR_WHITE_BOLD "%.*s" COLOR_RESET "\n",
                               end - start, &text[start]);
            }
            line_idx++;
            start = end;
            while (start < len && text[start] == ' ') start++;
        }
    } else {
        pos += sprintf(&frame_buffer[pos], "\033[2K\n");
    }

    pos += sprintf(&frame_buffer[pos], "\033[2K  "
                   COLOR_PILL_BG COLOR_WHITE_BOLD " Space " COLOR_RESET " " COLOR_DIM "Talk" COLOR_RESET "  "
                   COLOR_PILL_BG COLOR_WHITE_BOLD " Enter " COLOR_RESET " " COLOR_DIM "Submit" COLOR_RESET "  "
                   COLOR_PILL_BG COLOR_PURPLE " \"Hey Sphere\" " COLOR_RESET " " COLOR_DIM "Wake" COLOR_RESET "  "
                   COLOR_PILL_BG COLOR_WHITE_BOLD " Esc " COLOR_RESET " " COLOR_DIM "Exit" COLOR_RESET "\033[J");

    unlock_renderer();

    fwrite(frame_buffer, 1, pos, stdout);
    fflush(stdout);
}

void renderer_cleanup(void) {
    printf("\033[?1049l\033[?25h\033[0m\n");
    fflush(stdout);
#ifdef _WIN32
    DeleteCriticalSection(&render_lock);
#endif
}
