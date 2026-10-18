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

// Evaluates a math query if present. Returns 1 if query was a math calculation.
int brain_evaluate_math(const char *query, char *out_answer, size_t max_len);

// Synthesizes a detailed visual description of an object or discovered web image
void brain_describe_visual(const char *user_query, const char *subject_title, const char *image_caption, const char *web_extract, char *out_desc, size_t max_len);

// Synthesizes an intelligent explanation for general knowledge
void brain_explain_general(const char *query, const char *subject_title, const char *web_extract, char *out_answer, size_t max_len);

// Handles immediate conversational queries (greetings, emotions, jokes, time, identity) in 0ms
int brain_handle_direct(const char *user_query, char *out_response, size_t max_len, int *out_is_happy);

// Evaluates decisions, comparisons, qualitative advice, and domain tradeoffs
int brain_reason_decision(const char *user_query, char *out_response, size_t max_len, int *out_is_happy);

// Main brain reasoning function
int brain_think(const char *user_query, const char *subject_title, const char *web_extract, const char *image_caption, const char *image_url,
                char *out_response, size_t max_len, int *out_is_happy);

#ifdef __cplusplus
}
#endif

#endif // BRAIN_H
