#ifndef CONFIG_H
#define CONFIG_H

// Terminal display configuration
#define TERM_W 48
#define TERM_H 22
#define PIX_W (TERM_W * 2)
#define PIX_H (TERM_H * 4)
#define NUM_PARTICLES 600

// Target framerate
#define TARGET_FPS 60
#define FRAME_DELAY_MS (1000 / TARGET_FPS)

// Colors (ANSI 256 color codes)
#define COLOR_RESET        "\033[0m"
#define COLOR_PURPLE       "\033[38;5;141m" // Listening state
#define COLOR_PURPLE_BRIGHT "\033[38;5;177m"
#define COLOR_GREEN        "\033[38;5;46m"  // Speaking / Default state
#define COLOR_GREEN_EMERALD "\033[38;5;48m"
#define COLOR_CYAN         "\033[38;5;51m"  // Searching / Thinking state
#define COLOR_GOLD         "\033[38;5;220m" // Happy state
#define COLOR_RED          "\033[38;5;196m" // Crimson / Red
#define COLOR_BLUE         "\033[38;5;39m"  // Deep Sapphire / Electric Blue
#define COLOR_YELLOW       "\033[38;5;226m" // Vivid Yellow
#define COLOR_ORANGE       "\033[38;5;208m" // Sunset Orange
#define COLOR_PINK         "\033[38;5;213m" // Neon Hot Pink
#define COLOR_WHITE        "\033[38;5;231m" // Pure Crystal White
#define COLOR_MAGENTA      "\033[38;5;201m" // Electric Magenta
#define COLOR_LIME         "\033[38;5;118m" // Electric Lime
#define COLOR_DIM          "\033[38;5;244m"
#define COLOR_WHITE_BOLD   "\033[1;37m"

// Agent States
typedef enum {
    STATE_IDLE,       // Green ball, gentle rotation
    STATE_LISTENING,  // Purple ball, revolves & throbs with incoming voice
    STATE_THINKING,   // Cyan ball, rapid pulse & search
    STATE_SPEAKING,   // Green ball, revolves while replying
    STATE_HAPPY,      // Morphs into Braille happy face, then back to sphere
    STATE_MUSIC       // Dynamic high-energy visualizer dancing to music beats
} AgentState;

#endif // CONFIG_H
