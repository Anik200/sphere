#include "agent.h"
#include "search.h"
#include "brain.h"
#include "renderer.h"
#include <string.h>
#include <ctype.h>

void agent_init(void) {
}

int agent_is_music_request(const char *query, MusicTrack *out_track) {
    if (!query || !out_track) return 0;
    const unsigned char *u = (const unsigned char *)query;
    if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        query += 3;
    }
    while (*query && (isspace((unsigned char)*query) || *query == '"' || *query == '\'')) {
        query++;
    }

    char lower[512];
    size_t i = 0;
    for (; query[i] && i + 1 < sizeof(lower); i++) {
        lower[i] = (char)tolower((unsigned char)query[i]);
    }
    lower[i] = '\0';
    while (i > 0 && (lower[i-1] == '"' || lower[i-1] == '\'' || isspace((unsigned char)lower[i-1]))) {
        lower[--i] = '\0';
    }

    int has_music_intent = 0;
    if (strncmp(lower, "play ", 5) == 0 ||
        strstr(lower, "play the song") ||
        strstr(lower, "play song") ||
        strstr(lower, "play a song") ||
        strstr(lower, "play track") ||
        strstr(lower, "play music") ||
        strstr(lower, "play some music") ||
        strstr(lower, "play album") ||
        strstr(lower, "play me")) {
        has_music_intent = 1;
    }

    if (!has_music_intent) return 0;

    renderer_set_state(STATE_THINKING);
    renderer_set_status("Searching & fetching full song...");
    renderer_set_subtitle("Retrieving complete track for full start-to-finish playback...");

    return search_music_track(query, out_track);
}

int agent_process_query(const char *query, char *out_response, size_t max_len, int *out_is_happy) {
    if (!query || !out_response || max_len == 0) return 0;
    out_response[0] = '\0';
    if (out_is_happy) *out_is_happy = 0;

    // 1. Direct interactive conversational / math / system check (0ms instant response)
    if (brain_handle_direct(query, out_response, max_len, out_is_happy)) {
        return 1;
    }

    // 2. Cognitive Reasoning & Decision Engine (tradeoffs, comparisons, style, engineering)
    if (brain_reason_decision(query, out_response, max_len, out_is_happy)) {
        return 1;
    }

    // 3. Perform Web & Image Search for real-world knowledge and live imagery
    SearchResult res;
    int search_found = search_web_and_images(query, &res);

    // 3. Process through custom native C Brain
    return brain_think(
        query,
        search_found ? res.title : "",
        search_found ? res.extract : "",
        res.has_image ? res.image_caption : "",
        res.has_image ? res.image_url : "",
        out_response,
        max_len,
        out_is_happy
    );
}
