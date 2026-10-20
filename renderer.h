#ifndef RENDERER_H
#define RENDERER_H

#include "config.h"
#include "audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float sx, sy, sz;
    float tx, ty, tz;
} Particle;

typedef enum {
    EXPR_NONE = 0,
    /* Faces */
    EXPR_HAPPY,
    EXPR_SAD,
    EXPR_SURPRISED,
    EXPR_ANGRY,
    EXPR_WINK,
    EXPR_COOL,
    EXPR_SILLY,
    EXPR_LAUGH,
    EXPR_CONFUSED,
    EXPR_CAT,
    EXPR_SKULL,
    /* Hand Gestures */
    EXPR_THUMBS_UP,
    EXPR_PEACE,
    EXPR_WAVE,
    EXPR_ROCK,
    EXPR_OK,
    /* Figures & Shapes */
    EXPR_HEART,
    EXPR_STAR,
    EXPR_CUBE,
    EXPR_PYRAMID,
    EXPR_DONUT,
    EXPR_SPIRAL,
    EXPR_FLOWER,
    EXPR_INFINITY
} BallExpression;

void renderer_init(void);
void renderer_set_state(AgentState state);
AgentState renderer_get_state(void);
void renderer_set_voice_amp(float amp);
void renderer_set_vis_data(const AudioVisData *vis_data);
void renderer_set_status(const char *status_text);
void renderer_set_subtitle(const char *subtitle_text);
void renderer_set_input_buffer(const char *input_text);
void renderer_trigger_happy(void);
void renderer_set_expression(BallExpression expr);
BallExpression renderer_get_expression(void);
BallExpression renderer_parse_expression_name(const char *name);
const char *renderer_get_expression_name(BallExpression expr);
void renderer_set_expression_by_name(const char *name);
void renderer_set_custom_color(const char *ansi_color);
const char *renderer_get_custom_color(void);
void renderer_render_frame(void);
void renderer_cleanup(void);

#ifdef __cplusplus
}
#endif
#endif
