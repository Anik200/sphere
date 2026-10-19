#ifndef BRAIN_H
#define BRAIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INTENT_UNKNOWN,
    INTENT_GREETING,
    INTENT_CALCULATION,
    INTENT_DESCRIBE_VISUAL,
    INTENT_EXPLAIN_CONCEPT,
    INTENT_EXIT,
    INTENT_HAPPY
} BrainIntent;

int brain_evaluate_math(const char *query, char *out_answer, size_t max_len);

void brain_describe_visual(const char *user_query, const char *subject_title, const char *image_caption, const char *web_extract, char *out_desc, size_t max_len);

void brain_explain_general(const char *query, const char *subject_title, const char *web_extract, char *out_answer, size_t max_len);

int brain_handle_direct(const char *user_query, char *out_response, size_t max_len, int *out_is_happy);

int brain_reason_decision(const char *user_query, char *out_response, size_t max_len, int *out_is_happy);

int brain_think(const char *user_query, const char *subject_title, const char *web_extract, const char *image_caption, const char *image_url,
                char *out_response, size_t max_len, int *out_is_happy);

#ifdef __cplusplus
}
#endif
#endif
