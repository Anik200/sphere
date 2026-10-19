#ifndef AGENT_H
#define AGENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "search.h"

void agent_init(void);

int agent_process_query(const char *query, char *out_response, size_t max_len, int *out_is_happy);

int agent_has_music_intent(const char *query);

int agent_is_music_request(const char *query, MusicTrack *out_track);

#ifdef __cplusplus
}
#endif
#endif
