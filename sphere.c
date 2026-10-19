#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define TERM_W 44
#define TERM_H 22
#define PIX_W (TERM_W * 2)
#define PIX_H (TERM_H * 4)
#define NUM_PARTICLES 500

typedef enum { STATE_LISTENING, STATE_SPEAKING, STATE_HAPPY } AgentState;

typedef struct {
    float sx, sy, sz;
    float tx, ty, tz;
} Particle;

Particle particles[NUM_PARTICLES];
unsigned char braille_grid[TERM_H][TERM_W];
char frame_buffer[TERM_H * (TERM_W * 3 + 2) + 64];

static const unsigned char pixel_map[4][2] = {
    {0x01, 0x08},
    {0x02, 0x10},
    {0x04, 0x20},
    {0x40, 0x80}
};

void setup_windows_console(void) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
#endif
}

void init_particles(void) {
    float golden_angle = (float)M_PI * (3.0f - sqrtf(5.0f));
    for (int i = 0; i < NUM_PARTICLES; i++) {
        float y = 1.0f - (i / (float)(NUM_PARTICLES - 1)) * 2.0f;
        float radius = sqrtf(1.0f - y * y);
        float theta = golden_angle * i;
        particles[i].sx = cosf(theta) * radius;
        particles[i].sy = y;
        particles[i].sz = sinf(theta) * radius;

        if (i < 80) {
            float a = (i / 80.0f) * 2.0f * (float)M_PI;
            particles[i].tx = -0.35f + cosf(a) * 0.12f;
            particles[i].ty = -0.3f + sinf(a) * 0.12f;
            particles[i].tz = 0.0f;
        } else if (i < 160) {
            float a = ((i - 80) / 80.0f) * 2.0f * (float)M_PI;
            particles[i].tx = 0.35f + cosf(a) * 0.12f;
            particles[i].ty = -0.3f + sinf(a) * 0.12f;
            particles[i].tz = 0.0f;
        } else {
            float t = (i - 160) / (float)(NUM_PARTICLES - 160);
            float a = (float)M_PI * 0.15f + t * (float)M_PI * 0.7f;
            particles[i].tx = cosf(a) * 0.65f;
            particles[i].ty = sinf(a) * 0.55f;
            particles[i].tz = 0.0f;
        }
    }
}

void set_subpixel(int px, int py) {
    if (px < 0 || px >= PIX_W || py < 0 || py >= PIX_H) return;
    braille_grid[py / 4][px / 2] |= pixel_map[py % 4][px % 2];
}

void render_frame(AgentState state, float angle, float voice_amp, float morph) {
    memset(braille_grid, 0, sizeof(braille_grid));
    float scale = (PIX_H * 0.35f) * (1.0f + voice_amp * 0.25f);

    for (int i = 0; i < NUM_PARTICLES; i++) {
        float rx = particles[i].sx * cosf(angle) - particles[i].sz * sinf(angle);
        float rz = particles[i].sx * sinf(angle) + particles[i].sz * cosf(angle);
        float ry = particles[i].sy * cosf(angle * 0.3f) - rz * sinf(angle * 0.3f);
        rz       = particles[i].sy * sinf(angle * 0.3f) + rz * cosf(angle * 0.3f);

        float fx = (1.0f - morph) * rx + morph * particles[i].tx;
        float fy = (1.0f - morph) * ry + morph * particles[i].ty;
        float fz = (1.0f - morph) * rz + morph * particles[i].tz;

        float fov = 2.5f / (2.5f - fz * 0.6f);
        int px = (int)(PIX_W / 2.0f + fx * scale * fov);
        int py = (int)(PIX_H / 2.0f + fy * scale * fov);
        set_subpixel(px, py);
    }

    int pos = sprintf(frame_buffer, "\033[H%s",
                      (state == STATE_LISTENING) ? "\033[38;5;135m" : "\033[38;5;46m");

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
    pos += sprintf(&frame_buffer[pos], "\033[0m");
    fwrite(frame_buffer, 1, pos, stdout);
    fflush(stdout);
}

int main(void) {
    setup_windows_console();
    init_particles();
    printf("\033[2J\033[?25l");

    float angle = 0.0f, morph = 0.0f;
    for (int frame = 0; frame < 600; frame++) {
        AgentState state = (frame < 200) ? STATE_LISTENING :
                           (frame < 400) ? STATE_SPEAKING : STATE_HAPPY;

        float amp = (state != STATE_HAPPY) ? (0.15f * sinf(frame * 0.3f)) : 0.0f;
        angle += 0.04f + fabsf(amp) * 0.2f;

        if (state == STATE_HAPPY && frame < 520) morph += (1.0f - morph) * 0.08f;
        else morph += (0.0f - morph) * 0.08f;

        render_frame(state, angle, amp, morph);
#ifdef _WIN32
        Sleep(16);
#else
        usleep(16666);
#endif
    }

    printf("\033[?25h");
    return 0;
}
