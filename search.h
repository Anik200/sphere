#ifndef SEARCH_H
#define SEARCH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char title[256];
    char extract[4096];
    char image_url[512];
    char image_caption[512];
    char visual_details[2048];
    int has_image;
} SearchResult;


int search_web_and_images(const char *query, SearchResult *out_result);

typedef struct {
    char track_name[256];
    char artist_name[256];
    char collection_name[256];
    char preview_url[1024];
    int found;
} MusicTrack;

int search_music_track(const char *query, MusicTrack *out_track);

#ifdef __cplusplus
}
#endif

#endif 
