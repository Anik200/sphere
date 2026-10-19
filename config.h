#ifndef CONFIG_H
#define CONFIG_H

#define TERM_W 48
#define TERM_H 22
#define PIX_W (TERM_W * 2)
#define PIX_H (TERM_H * 4)
#define NUM_PARTICLES 600

#define TARGET_FPS 60
#define FRAME_DELAY_MS (1000 / TARGET_FPS)

#define COLOR_RESET         "\033[0m"
#define COLOR_PURPLE        "\033[38;5;141m"
#define COLOR_PURPLE_BRIGHT "\033[38;5;177m"
#define COLOR_GREEN         "\033[38;5;46m"
#define COLOR_GREEN_EMERALD "\033[38;5;48m"
#define COLOR_CYAN          "\033[38;5;51m"
#define COLOR_GOLD          "\033[38;5;220m"
#define COLOR_RED           "\033[38;5;196m"
#define COLOR_BLUE          "\033[38;5;39m"
#define COLOR_YELLOW        "\033[38;5;226m"
#define COLOR_ORANGE        "\033[38;5;208m"
#define COLOR_PINK          "\033[38;5;213m"
#define COLOR_WHITE         "\033[38;5;231m"
#define COLOR_MAGENTA       "\033[38;5;201m"
#define COLOR_LIME          "\033[38;5;118m"
#define COLOR_DIM           "\033[38;5;244m"
#define COLOR_WHITE_BOLD    "\033[1;37m"
#define COLOR_BORDER        "\033[38;5;238m"
#define COLOR_PILL_BG       "\033[48;5;236m"
#define COLOR_TRACK_BG      "\033[38;5;238m"
#define COLOR_PILL_TEXT     "\033[38;5;253m"

typedef enum {
    STATE_IDLE,
    STATE_LISTENING,
    STATE_THINKING,
    STATE_SPEAKING,
    STATE_HAPPY,
    STATE_MUSIC
} AgentState;

#endif
