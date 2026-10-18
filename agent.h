#ifndef AGENT_H
#define AGENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "search.h"

void agent_init(void);

// Process a user query: performs web search, image lookup, detailed visual description,
// and sets is_happy if happy emotion is detected.
int agent_process_query(const char *query, char *out_response, size_t max_len, int *out_is_happy);

// Check if query has intent to play music
int agent_has_music_intent(const char *query);

// Check if user query is a music request and resolve full song track
int agent_is_music_request(const char *query, MusicTrack *out_track);

#ifdef __cplusplus
}
#endif

#endif // AGENT_H
