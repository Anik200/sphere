#define _USE_MATH_DEFINES
#include "config.h"
#include "renderer.h"
#include "audio.h"
#include "search.h"
#include "agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <io.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

static volatile int app_running = 1;

#ifdef _WIN32
static void handle_exit(void);

static BOOL WINAPI console_ctrl_handler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            handle_exit();
            return TRUE;
        default:
            return FALSE;
    }
}

static HANDLE hRenderThread = NULL;

static DWORD WINAPI render_loop_thread(LPVOID lpParam) {
    (void)lpParam;
    while (app_running) {
        float amp = audio_get_current_amp();
        renderer_set_voice_amp(amp);
        renderer_render_frame();
        Sleep(FRAME_DELAY_MS);
    }
    return 0;
}
#endif

static void to_lower_str(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    const unsigned char *u = (const unsigned char *)src;
    if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        src += 3;
    }
    while (*src && (isspace((unsigned char)*src) || *src == '"' || *src == '\'')) {
        src++;
    }
    size_t i = 0;
    for (; src[i] && i + 1 < max_len; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
    while (i > 0 && (dst[i-1] == '"' || dst[i-1] == '\'' || isspace((unsigned char)dst[i-1]))) {
        dst[--i] = '\0';
    }
}

static void handle_exit(void) {
    static int already_exiting = 0;
    if (already_exiting) return;
    already_exiting = 1;

    renderer_set_state(STATE_SPEAKING);
    renderer_set_status("Exiting SphereAI...");
    renderer_set_subtitle("Sphere: Good bye, see you.");
    audio_speak("Good bye, see you.");
    int wait_count = 70; // up to 3.5s for complete speech
    while (audio_is_speaking() && --wait_count > 0) {
        Sleep(50);
    }
    audio_stop_speech();
    app_running = 0;
    audio_cleanup();
    renderer_cleanup();
#ifdef _WIN32
    ExitProcess(0);
#else
    exit(0);
#endif
}

static int is_exit_command(const char *lower) {
    if (!lower || strlen(lower) == 0) return 0;
    char clean[512];
    size_t len = strlen(lower);
    if (len >= sizeof(clean)) len = sizeof(clean) - 1;
    memcpy(clean, lower, len);
    clean[len] = '\0';
    while (len > 0 && (clean[len - 1] == '.' || clean[len - 1] == '?' || clean[len - 1] == '!' || clean[len - 1] == ' ' || clean[len - 1] == '"' || clean[len - 1] == '\'')) {
        clean[--len] = '\0';
    }
    return (strcmp(clean, "exit") == 0 ||
            strcmp(clean, "quit") == 0 ||
            strcmp(clean, "close") == 0 ||
            strcmp(clean, "goodbye") == 0 ||
            strcmp(clean, "good bye") == 0 ||
            strcmp(clean, "bye") == 0 ||
            strcmp(clean, "bye bye") == 0 ||
            strcmp(clean, "see you") == 0 ||
            strcmp(clean, "see ya") == 0 ||
            strcmp(clean, "shutdown") == 0 ||
            strcmp(clean, "shut down") == 0 ||
            strcmp(clean, "close app") == 0 ||
            strcmp(clean, "exit app") == 0 ||
            strcmp(clean, "quit app") == 0 ||
            strstr(clean, "exit") != NULL ||
            strstr(clean, "quit") != NULL ||
            strstr(clean, "goodbye") != NULL ||
            strstr(clean, "good bye") != NULL ||
            strstr(clean, "see you") != NULL ||
            strstr(clean, "bye") != NULL ||
            strstr(clean, "shutdown") != NULL ||
            strstr(clean, "shut down") != NULL ||
            strstr(clean, "close app") != NULL ||
            strstr(clean, "turn off") != NULL ||
            strstr(clean, "terminate") != NULL ||
            strstr(clean, "stop listening") != NULL ||
            strstr(clean, "go to sleep") != NULL);
}

static int is_stop_command(const char *lower) {
    return (strcmp(lower, "stop") == 0 ||
            strcmp(lower, "shut up") == 0 ||
            strcmp(lower, "quiet") == 0 ||
            strcmp(lower, "be quiet") == 0 ||
            strcmp(lower, "pause") == 0 ||
            strcmp(lower, "cancel") == 0 ||
            strcmp(lower, "wait") == 0 ||
            strcmp(lower, "hold on") == 0 ||
            strcmp(lower, "stop talking") == 0 ||
            strcmp(lower, "stop it") == 0 ||
            strcmp(lower, "silence") == 0 ||
            strcmp(lower, "enough") == 0);
}

static void handle_user_query(const char *raw_query) {
    if (!raw_query || strlen(raw_query) == 0) return;
    const unsigned char *u = (const unsigned char *)raw_query;
    if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        raw_query += 3;
    }
    while (*raw_query && (isspace((unsigned char)*raw_query) || *raw_query == '"' || *raw_query == '\'')) {
        raw_query++;
    }
    char query[512];
    snprintf(query, sizeof(query), "%s", raw_query);
    size_t qlen = strlen(query);
    while (qlen > 0 && (query[qlen-1] == '"' || query[qlen-1] == '\'' || isspace((unsigned char)query[qlen-1]))) {
        query[--qlen] = '\0';
    }
    if (qlen == 0) return;

    char lower_query[512];
    to_lower_str(query, lower_query, sizeof(lower_query));

    // Exit check
    if (is_exit_command(lower_query)) {
        handle_exit();
        return;
    }

    // Stop check
    if (is_stop_command(lower_query)) {
        audio_stop_speech();
        audio_stop_music();
        audio_set_standby_mode();
        renderer_set_state(STATE_IDLE);
        renderer_set_status("Standby | Say \"Hey Sphere\" to activate");
        renderer_set_subtitle("Stopped. Say \"Hey Sphere\" to wake me up.");
        return;
    }

    // Music playback check: search and stream music preview dynamically
    MusicTrack track;
    if (agent_is_music_request(query, &track)) {
        if (track.found) {
            renderer_set_state(STATE_THINKING);
            renderer_set_status("Loading full song...");
            char sub_load[512];
            snprintf(sub_load, sizeof(sub_load), "Loading: \"%s\" by %s", track.track_name, track.artist_name);
            renderer_set_subtitle(sub_load);

            char announce[512];
            snprintf(announce, sizeof(announce), "Now playing %s by %s. Enjoy the music!", track.track_name, track.artist_name);
            renderer_set_state(STATE_SPEAKING);
            renderer_set_status("Announcing track...");
            audio_speak(announce);

            while (app_running && audio_is_speaking()) {
                if (audio_check_exit_detected()) { handle_exit(); return; }
                int wake_retrigger = 0;
                if (audio_check_interrupted(&wake_retrigger)) {
                    if (wake_retrigger) {
                        renderer_set_state(STATE_LISTENING);
                        renderer_set_status("Awake! Listening to your voice... (Speak now)");
                        renderer_set_subtitle("Listening...");
                        audio_set_listening_mode();
                        return;
                    }
                    audio_set_standby_mode();
                    renderer_set_state(STATE_IDLE);
                    return;
                }
#ifdef _WIN32
                if (_kbhit()) {
                    int c = _getch();
                    if (c == 27) { handle_exit(); return; }
                    audio_stop_speech();
                    break;
                }
                Sleep(20);
#endif
            }

            // Transition to Dynamic Music Visualizer State!
            renderer_set_state(STATE_MUSIC);
            char status_music[256];
            snprintf(status_music, sizeof(status_music), "Visualizing Music | %s - %s", track.track_name, track.artist_name);
            renderer_set_status(status_music);
            char sub_music[512];
            snprintf(sub_music, sizeof(sub_music), "Playing full track: \"%s\" by %s (Say \"stop\" or \"Hey Sphere\" to interrupt)",
                     track.track_name, track.artist_name);
            renderer_set_subtitle(sub_music);

            audio_play_music(track.preview_url);

            // Music visualizer loop: ball pulses, ripples, and repels with the music beats
            while (app_running && audio_is_music_playing()) {
                if (audio_check_exit_detected()) {
                    handle_exit();
                    return;
                }
                int wake_retrigger = 0;
                if (audio_check_interrupted(&wake_retrigger)) {
                    audio_stop_music();
                    if (wake_retrigger) {
                        renderer_set_state(STATE_LISTENING);
                        renderer_set_status("Awake! Listening to your voice... (Speak now)");
                        renderer_set_subtitle("Listening...");
                        audio_set_listening_mode();
                        return;
                    }
                    break;
                }
#ifdef _WIN32
                if (_kbhit()) {
                    int c = _getch();
                    if (c == 27) { handle_exit(); return; }
                    audio_stop_music();
                    break;
                }
                Sleep(25);
#else
                usleep(25000);
#endif
            }

            audio_stop_music();
            audio_set_standby_mode();
            renderer_set_state(STATE_IDLE);
            renderer_set_status("Standby | Say \"Hey Sphere\" to activate");
            renderer_set_subtitle("Music ended. Say \"Hey Sphere\" to wake me up.");
            return;
        } else {
            renderer_set_state(STATE_SPEAKING);
            renderer_set_status("Music track not found");
            char not_found[512];
            snprintf(not_found, sizeof(not_found), "I searched for \"%s\", but couldn't find an available audio stream right now. Feel free to ask for another song or artist!", query);
            renderer_set_subtitle(not_found);
            audio_speak(not_found);
            while (app_running && audio_is_speaking()) {
                if (audio_check_exit_detected()) { handle_exit(); return; }
                int wake_retrigger = 0;
                if (audio_check_interrupted(&wake_retrigger)) {
                    if (wake_retrigger) {
                        renderer_set_state(STATE_LISTENING);
                        audio_set_listening_mode();
                        return;
                    }
                    break;
                }
#ifdef _WIN32
                if (_kbhit()) {
                    int c = _getch();
                    if (c == 27) { handle_exit(); return; }
                    audio_stop_speech();
                    break;
                }
                Sleep(20);
#endif
            }
            audio_set_standby_mode();
            renderer_set_state(STATE_IDLE);
            renderer_set_status("Standby | Say \"Hey Sphere\" to activate");
            return;
        }
    }

    // Happy trigger
    if (strcmp(lower_query, "happy") == 0 || strcmp(lower_query, "smile") == 0) {
        renderer_set_status("Feeling joyful! Morphing happy face...");
        renderer_set_subtitle("Joyful smile! Look at my happy face!");
        renderer_trigger_happy();
        renderer_set_state(STATE_SPEAKING);
        audio_speak("I am feeling joyful! Look at my smiling face!");
        while (app_running) {
            if (audio_check_exit_detected()) {
                handle_exit();
                return;
            }
            int wake_retrigger = 0;
            if (audio_check_interrupted(&wake_retrigger)) {
                if (wake_retrigger) {
                    renderer_set_state(STATE_LISTENING);
                    renderer_set_status("Awake! Listening to your voice... (Speak now)");
                    renderer_set_subtitle("Listening...");
                    audio_set_listening_mode();
                    return;
                }
                break;
            }
            if (!audio_is_speaking()) {
                break;
            }
#ifdef _WIN32
            if (_kbhit()) {
                int c = _getch();
                if (c == 27) { handle_exit(); return; }
                audio_stop_speech();
                break;
            }
            Sleep(20);
#else
            usleep(20000);
#endif
        }
        audio_set_standby_mode();
        renderer_set_state(STATE_IDLE);
        renderer_set_status("Standby | Say \"Hey Sphere\" to activate");
        return;
    }

    // Transition to Thinking / Web Searching state (Cyan ball revolving fast)
    renderer_set_state(STATE_THINKING);
    char think_status[256];
    snprintf(think_status, sizeof(think_status), "Searching & thinking: \"%s\"...", query);
    renderer_set_status(think_status);
    renderer_set_subtitle(query);

    char response[4096] = {0};
    int is_happy = 0;

    agent_process_query(query, response, sizeof(response), &is_happy);

    // Guarantee a rich, non-empty spoken response for every query
    if (strlen(response) == 0) {
        snprintf(response, sizeof(response),
                 "I researched \"%s\", but could not retrieve a clear factual summary. Feel free to ask me more specific questions or explore another topic.", query);
    }

    // If happy sentiment or praise, morph to happy face!
    if (is_happy) {
        renderer_set_status("Feeling joyful! Morphing happy face...");
        renderer_trigger_happy();
#ifdef _WIN32
        Sleep(1200); // Allow happy face morph animation to show
#endif
    }

    // Transition to Speaking state (Green ball revolving while answering)
    renderer_set_state(STATE_SPEAKING);
    renderer_set_status("Speaking response... (Say \"Hey Sphere\" or \"stop\" to interrupt)");
    renderer_set_subtitle(response);

    // Speak aloud with voice synthesis (words stream onto screen one by one in sync)
    audio_speak(response);

    // Wait until speech is finished or interrupted by user voice / key
    while (app_running) {
        if (audio_check_exit_detected()) {
            handle_exit();
            return;
        }

        int wake_retrigger = 0;
        if (audio_check_interrupted(&wake_retrigger)) {
            // Speech was interrupted!
            if (wake_retrigger) {
                // User said "Hey Sphere" while speaking -> Immediately switch to listening!
                renderer_set_state(STATE_LISTENING);
                renderer_set_status("Awake! Listening to your voice... (Speak now)");
                renderer_set_subtitle("Listening...");
                audio_set_listening_mode();
                return;
            }
            break;
        }

        if (!audio_is_speaking()) {
            break;
        }

#ifdef _WIN32
        if (_kbhit()) {
            int c = _getch();
            if (c == 27) { // Esc
                handle_exit();
                return;
            }
            // ANY key immediately interrupts speaking!
            audio_stop_speech();
            break;
        }
        Sleep(20);
#else
        usleep(20000);
#endif
    }

    // After speaking finishes, return to standby mode
    audio_set_standby_mode();
    renderer_set_state(STATE_IDLE);
    renderer_set_status("Standby | Say \"Hey Sphere\" to activate");
}

int main(void) {
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#endif
    renderer_init();
    audio_init();
    agent_init();

#ifdef _WIN32
    hRenderThread = CreateThread(NULL, 0, render_loop_thread, NULL, 0, NULL);
#endif

    renderer_set_state(STATE_IDLE);
    renderer_set_status("Standby | Say \"Hey Sphere\" to wake me up!");
    renderer_set_subtitle("Say \"Hey Sphere\" to activate (or say \"exit\" to quit)");

    int is_pipe = 0;
#ifdef _WIN32
    is_pipe = !_isatty(_fileno(stdin));
#endif

    if (is_pipe) {
        // Piped stdin mode
        char line[512];
        while (fgets(line, sizeof(line), stdin)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
            if (l == 0) continue;
            if (is_exit_command(line)) {
                handle_exit();
                break;
            }
            handle_user_query(line);
            break;
        }
    } else {
        // Welcome voice greeting (spoken once on launch)
        audio_speak("Sphere AI online. Say Hey Sphere to activate.");
        while (app_running) {
            if (audio_check_exit_detected()) { handle_exit(); break; }
            int wake_retrigger = 0;
            if (audio_check_interrupted(&wake_retrigger)) {
                if (wake_retrigger) {
                    renderer_set_state(STATE_LISTENING);
                    renderer_set_status("Awake! Listening to your voice... (Speak now)");
                    renderer_set_subtitle("Listening...");
                    audio_set_listening_mode();
                    break;
                }
                break;
            }
            if (!audio_is_speaking()) break;
#ifdef _WIN32
            if (_kbhit()) {
                int c = _getch();
                if (c == 27) { handle_exit(); break; }
                audio_stop_speech();
                break;
            }
            Sleep(20);
#else
            usleep(20000);
#endif
        }

        audio_set_standby_mode();
        renderer_set_state(STATE_IDLE);
        renderer_set_status("Standby | Say \"Hey Sphere\" to activate");
        renderer_set_subtitle("Say \"Hey Sphere\" to wake me up! (Or say \"exit\")");

        char query_buf[512] = {0};

        while (app_running) {
            // Check for voice exit
            if (audio_check_exit_detected()) {
                handle_exit();
                break;
            }

            // Keyboard check
#ifdef _WIN32
            if (_kbhit()) {
                int c = _getch();
                if (c == 27) { // ESC
                    handle_exit();
                    break;
                }
            }
#endif

            // Check if awake or wake word triggered
            if (audio_check_wake_detected() || renderer_get_state() == STATE_LISTENING) {
                // Wake detected!
                renderer_set_state(STATE_LISTENING);
                renderer_set_status("Awake! Listening to your voice... (Speak now)");
                renderer_set_subtitle("Listening...");
                audio_set_listening_mode();

                // Check if query was already caught in same breath
                if (audio_get_final_speech(query_buf, sizeof(query_buf))) {
                    handle_user_query(query_buf);
                    continue;
                }

                // Active listening wait loop
                while (app_running && renderer_get_state() == STATE_LISTENING) {
#ifdef _WIN32
                    if (_kbhit()) {
                        int c = _getch();
                        if (c == 27) { handle_exit(); break; }
                    }
#endif
                    if (audio_check_exit_detected()) {
                        handle_exit();
                        break;
                    }

                    // Check if speech phrase was finalized
                    if (audio_get_final_speech(query_buf, sizeof(query_buf))) {
                        char heard[256];
                        snprintf(heard, sizeof(heard), "Heard: \"%s\"", query_buf);
                        renderer_set_status(heard);
                        handle_user_query(query_buf);
                        break;
                    }

                    // Check for timeout (silence for >6s)
                    if (audio_check_timeout_detected()) {
                        renderer_set_state(STATE_IDLE);
                        renderer_set_status("Standby | Say \"Hey Sphere\" to activate");
                        renderer_set_subtitle("Listening timed out. Say \"Hey Sphere\" to wake me up.");
                        audio_set_standby_mode();
                        break;
                    }

                    Sleep(30);
                }
            }

            Sleep(30);
        }
    }

    app_running = 0;
#ifdef _WIN32
    if (hRenderThread) {
        WaitForSingleObject(hRenderThread, 1000);
        CloseHandle(hRenderThread);
    }
#endif

    audio_cleanup();
    renderer_cleanup();
    return 0;
}
