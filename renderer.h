#ifndef RENDERER_H
#define RENDERER_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float sx, sy, sz; // Sphere base coordinates
    float tx, ty, tz; // Target coordinates (Happy face)
} Particle;

typedef enum {
    EXPR_NONE = 0,
    EXPR_HAPPY,
    EXPR_SAD,
    EXPR_SURPRISED,
    EXPR_ANGRY,
    EXPR_WINK,
    EXPR_COOL
} BallExpression;

void renderer_init(void);
void renderer_set_state(AgentState state);
AgentState renderer_get_state(void);
void renderer_set_voice_amp(float amp);
void renderer_set_status(const char *status_text);
void renderer_set_subtitle(const char *subtitle_text);
void renderer_set_input_buffer(const char *input_text);
void renderer_trigger_happy(void);
void renderer_set_expression(BallExpression expr);
BallExpression renderer_get_expression(void);
void renderer_set_custom_color(const char *ansi_color);
const char *renderer_get_custom_color(void);
void renderer_render_frame(void);
void renderer_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // RENDERER_H
