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
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static Particle particles[NUM_PARTICLES];
static unsigned char braille_grid[TERM_H][TERM_W];
static char frame_buffer[TERM_H * (TERM_W * 4 + 4) + 16384];

static const unsigned char pixel_map[4][2] = {
    {0x01, 0x08},
    {0x02, 0x10},
    {0x04, 0x20},
    {0x40, 0x80}
};

static volatile AgentState current_state = STATE_IDLE;
static volatile AgentState previous_state = STATE_IDLE;
static volatile float current_voice_amp = 0.0f;
static volatile float current_angle = 0.0f;
static volatile float current_morph = 0.0f;
static volatile float music_phase = 0.0f;
static volatile int happy_countdown = 0;
static volatile BallExpression current_expression = EXPR_NONE;
static volatile int expression_countdown = 0;
static char custom_color[32] = "";

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
    SetConsoleOutputCP(65001); // UTF-8
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif

    // Hide cursor and clear screen
    printf("\033[?25l\033[2J");
    fflush(stdout);

    // Initialize Fibonacci Sphere base coordinates and default Happy Face target coordinates
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

    // Restore saved custom base color from disk if exists
    FILE *f_col = fopen("sphere_color.cfg", "r");
    if (f_col) {
        if (fgets(custom_color, sizeof(custom_color), f_col)) {
            size_t l = strlen(custom_color);
            while (l > 0 && (custom_color[l - 1] == '\n' || custom_color[l - 1] == '\r')) {
                custom_color[--l] = '\0';
            }
        }
        fclose(f_col);
    }
}

void renderer_set_state(AgentState state) {
    lock_renderer();
    if (state == STATE_HAPPY) {
        if (current_state != STATE_HAPPY) {
            previous_state = current_state;
        }
        setup_expression_targets(EXPR_HAPPY);
        current_expression = EXPR_HAPPY;
        expression_countdown = 180; // ~3 seconds at 60 FPS
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
        expression_countdown = 240; // ~4 seconds morph
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
        FILE *f = fopen("sphere_color.cfg", "w");
        if (f) {
            fprintf(f, "%s", custom_color);
            fclose(f);
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

static inline void set_subpixel(int px, int py) {
    if (px < 0 || px >= PIX_W || py < 0 || py >= PIX_H) return;
    braille_grid[py / 4][px / 2] |= pixel_map[py % 4][px % 2];
}

void renderer_render_frame(void) {
    lock_renderer();

    AgentState state = current_state;
    float voice_amp = current_voice_amp;

    // Morph interpolation
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

    // Dynamic sound energy from voice input, speech synthesis or music
    float sound_energy = voice_amp;
    if (state == STATE_SPEAKING) {
        float speak_pulse = 0.25f + 0.20f * sinf(current_angle * 3.2f) * cosf(current_angle * 1.6f);
        if (sound_energy < speak_pulse) sound_energy = speak_pulse;
    } else if (state == STATE_MUSIC) {
        music_phase += 0.05f;
        float beat = 0.20f + 0.12f * sinf(music_phase * 2.5f);
        if (sound_energy < beat) sound_energy = beat;
    }

    // Dynamic rotation angle: accelerates dynamically with voice input, speech rhythm or thinking
    // Ball MUST NOT spin during music playback!
    float rotation_speed = 0.035f;
    if (state == STATE_MUSIC) {
        rotation_speed = 0.0f; // User explicit: Ball must NOT spin during music playback!
    } else if (sound_energy > 0.03f) {
        rotation_speed += sound_energy * 0.28f; // Dynamic energetic swirl with sound!
    } else if (state == STATE_THINKING) {
        rotation_speed += 0.09f;
    }
    current_angle += rotation_speed;

    float angle = current_angle;
    float morph = current_morph;

    // Dynamic base scale: expands and breathes gently with vocal amplitude or music
    float base_scale = (PIX_H * 0.36f);
    if (state == STATE_MUSIC) {
        base_scale *= (1.0f + sound_energy * 0.06f); // Gentle, subtle breathing pulse (no overreaction)
    } else if (sound_energy > 0.03f) {
        base_scale *= (1.0f + sound_energy * 0.16f); // 16% breathing expansion
    } else if (state == STATE_THINKING) {
        base_scale *= (1.0f + 0.10f * sinf(angle * 3.0f));
    }

    memset(braille_grid, 0, sizeof(braille_grid));

    // Particle transformation and projection
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    float cos_pitch = cosf(angle * 0.35f);
    float sin_pitch = sinf(angle * 0.35f);

    for (int i = 0; i < NUM_PARTICLES; i++) {
        // 3D rotation around Y and X axis
        float rx = particles[i].sx * cos_a - particles[i].sz * sin_a;
        float rz = particles[i].sx * sin_a + particles[i].sz * cos_a;
        float ry = particles[i].sy * cos_pitch - rz * sin_pitch;
        rz       = particles[i].sy * sin_pitch + rz * cos_pitch;

        if (state == STATE_MUSIC) {
            // Elegant, calm musical surface ripples - subtle and smooth, zero chaotic tearing
            float ripple = 1.0f + sinf(particles[i].sy * 3.5f + music_phase * 2.0f) * (sound_energy * 0.045f)
                                + cosf(atan2f(particles[i].sz, particles[i].sx) * 2.0f + music_phase * 1.5f) * (sound_energy * 0.025f);
            rx *= ripple;
            ry *= (1.0f + sinf(particles[i].sy * 2.5f + music_phase * 1.8f) * (sound_energy * 0.025f));
            rz *= ripple;
        } else if (sound_energy > 0.02f) {
            // Normal voice speech ripples and gentle repelling
            float wave_lat = sinf(particles[i].sy * 6.0f - angle * 4.0f) * (sound_energy * 0.14f);
            float theta = atan2f(particles[i].sz, particles[i].sx);
            float wave_lon = cosf(theta * 3.0f + angle * 3.0f) * (sound_energy * 0.10f);
            float repel = 1.0f + wave_lat + wave_lon + (sound_energy * 0.08f);
            rx *= repel;
            ry *= (1.0f + wave_lat * 0.5f);
            rz *= repel;
        }

        // Interpolate between 3D sphere and target expression
        float fx = (1.0f - morph) * rx + morph * particles[i].tx;
        float fy = (1.0f - morph) * ry + morph * particles[i].ty;
        float fz = (1.0f - morph) * rz + morph * particles[i].tz;

        // Perspective projection
        float fov = 2.6f / (2.6f - fz * 0.55f);
        int px = (int)(PIX_W / 2.0f + fx * base_scale * fov);
        int py = (int)(PIX_H / 2.0f + fy * base_scale * fov);
        set_subpixel(px, py);
    }

    // Determine state color: Dynamic state colors take precedence; custom color sets base/idle/speaking color
    const char *state_color;
    if (morph > 0.35f) {
        switch (current_expression) {
            case EXPR_HAPPY:     state_color = COLOR_GOLD; break;
            case EXPR_SAD:       state_color = COLOR_BLUE; break;
            case EXPR_SURPRISED: state_color = COLOR_CYAN; break;
            case EXPR_ANGRY:     state_color = COLOR_RED; break;
            case EXPR_WINK:      state_color = COLOR_YELLOW; break;
            case EXPR_COOL:      state_color = COLOR_PURPLE_BRIGHT; break;
            default:             state_color = COLOR_GOLD; break;
        }
    } else {
        switch (state) {
            case STATE_LISTENING: state_color = COLOR_PURPLE; break;
            case STATE_THINKING:  state_color = COLOR_CYAN; break;
            case STATE_MUSIC:     state_color = (custom_color[0] != '\0' ? custom_color : COLOR_CYAN); break;
            case STATE_SPEAKING:
            case STATE_IDLE:
            default:              state_color = (custom_color[0] != '\0' ? custom_color : COLOR_GREEN); break;
        }
    }

    // Live Audio Visualizer Meter: shows exact sound level reaction in real-time
    char sound_meter[32] = {0};
    int meter_bars = (int)(sound_energy * 10.0f);
    if (meter_bars > 10) meter_bars = 10;
    int b = 0;
    for (; b < meter_bars; b++) sound_meter[b] = '=';
    for (; b < 10; b++) sound_meter[b] = '-';
    sound_meter[10] = '\0';

    // Write entire frame to single buffer
    int pos = sprintf(frame_buffer, "\033[H\033[2K  %sSphereAI%s | %s%s\033[0m | Sound: [%s%s%s]\n",
                      COLOR_WHITE_BOLD, COLOR_RESET, state_color, current_status,
                      state_color, sound_meter, COLOR_RESET);

    pos += sprintf(&frame_buffer[pos], "%s", state_color);

    for (int y = 0; y < TERM_H; y++) {
        for (int x = 0; x < TERM_W; x++) {
            unsigned char mask = braille_grid[y][x];
            if (mask == 0) {
                frame_buffer[pos++] = ' ';
            } else {
                frame_buffer[pos++] = (char)0xE2;
                frame_buffer[pos++] = (char)(0xA0 | (mask >> 6));
                frame_buffer[pos++] = (char)(0x80 | (mask & 0x3F));
            }
        }
        frame_buffer[pos++] = '\n';
    }

    // Subtitle section below sphere (clean full multi-line word wrap - NO CUTOFF!)
    pos += sprintf(&frame_buffer[pos], "%s\033[2K\n", COLOR_RESET);

    if (current_subtitle[0] != '\0') {
        int len = (int)strlen(current_subtitle);
        int start = 0;
        int max_w = 72;
        int lines = 0;
        while (start < len && lines < 12) {
            int end = start + max_w;
            if (end >= len) {
                end = len;
            } else {
                int s = end;
                while (s > start && current_subtitle[s] != ' ') s--;
                if (s > start) end = s;
            }
            pos += sprintf(&frame_buffer[pos], "\033[2K  %s%.*s\033[0m\n", COLOR_WHITE_BOLD, end - start, &current_subtitle[start]);
            lines++;
            start = end;
            while (start < len && current_subtitle[start] == ' ') start++;
        }
    } else {
        pos += sprintf(&frame_buffer[pos], "\033[2K\n");
    }

    pos += sprintf(&frame_buffer[pos], "\033[2K  %s[Voice Control: Say \"Hey Sphere\" to activate | Say \"stop\" to interrupt | Esc to exit]\033[0m\n\033[2K", COLOR_DIM);

    unlock_renderer();

    fwrite(frame_buffer, 1, pos, stdout);
    fflush(stdout);
}

void renderer_cleanup(void) {
    printf("\033[?25h\033[0m\n"); // Restore cursor & colors
    fflush(stdout);
#ifdef _WIN32
    DeleteCriticalSection(&render_lock);
#endif
}
