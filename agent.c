#include "agent.h"
#include "search.h"
#include "brain.h"
#include <string.h>
#include <ctype.h>

void agent_init(void) {
}

int agent_has_music_intent(const char *query) {
    if (!query) return 0;
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

    if (strstr(lower, "play ") ||
        strcmp(lower, "play") == 0 ||
        strstr(lower, "play song") ||
        strstr(lower, "play track") ||
        strstr(lower, "play music") ||
        strstr(lower, "song ") ||
        strstr(lower, "track ") ||
        strstr(lower, "listen to") ||
        strstr(lower, "put on") ||
        strstr(lower, "stream ")) {
        return 1;
    }
    return 0;
}

int agent_is_music_request(const char *query, MusicTrack *out_track) {
    if (!query || !out_track) return 0;
    if (!agent_has_music_intent(query)) return 0;
    return search_music_track(query, out_track);
}

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

static int extract_json_value(const char *json_str, const char *key, char *out_val, size_t max_len) {
    if (!json_str || !key || !out_val || max_len == 0) return 0;
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    const char *pos = strstr(json_str, search_pattern);
    if (!pos) return 0;
    pos += strlen(search_pattern);
    while (*pos && isspace((unsigned char)*pos)) pos++;

    if (*pos == '"') {
        pos++;
        size_t d = 0;
        while (*pos && *pos != '"' && d + 1 < max_len) {
            if (*pos == '\\' && *(pos + 1)) {
                pos++;
                if (*pos == 'n') out_val[d++] = ' ';
                else if (*pos == 't') out_val[d++] = ' ';
                else out_val[d++] = *pos;
            } else {
                out_val[d++] = *pos;
            }
            pos++;
        }
        out_val[d] = '\0';
        return 1;
    } else {
        size_t d = 0;
        while (*pos && *pos != ',' && *pos != '}' && !isspace((unsigned char)*pos) && d + 1 < max_len) {
            out_val[d++] = *pos++;
        }
        out_val[d] = '\0';
        return 1;
    }
}

static int query_sphere_ai_model(const char *query, const char *context, char *out_response, size_t max_len, int *out_is_happy) {
    if (!query || strlen(query) == 0 || !out_response || max_len == 0) return 0;

#ifdef _WIN32
    char temp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_dir);
    char res_file[MAX_PATH];
    snprintf(res_file, sizeof(res_file), "%ssphere_ai_model_%lu.json", temp_dir, GetCurrentProcessId());
    DeleteFileA(res_file);

    char safe_query[512];
    size_t qlen = 0;
    for (size_t i = 0; query[i] && qlen + 2 < sizeof(safe_query); i++) {
        if (query[i] == '"' || query[i] == '\\' || query[i] == '\r' || query[i] == '\n') continue;
        safe_query[qlen++] = query[i];
    }
    safe_query[qlen] = '\0';

    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *last_bs = strrchr(exe_dir, '\\');
    if (last_bs) *last_bs = '\0';
    else strcpy(exe_dir, ".");

    char script_path[MAX_PATH];
    snprintf(script_path, sizeof(script_path), "%s\\sphere_ai_model.py", exe_dir);
    if (_access(script_path, 0) != 0) {
        strcpy(script_path, "sphere_ai_model.py");
    }

    const char *py_bin = "python";
    static char s_agent_py[MAX_PATH] = {0};
    if (s_agent_py[0] == '\0') {
        if (system("python --version >nul 2>&1") == 0) {
            strcpy(s_agent_py, "python");
        } else if (system("py -3 --version >nul 2>&1") == 0) {
            strcpy(s_agent_py, "py -3");
        } else {
            const char *local_app = getenv("LOCALAPPDATA");
            int found = 0;
            if (local_app) {
                char cand[MAX_PATH];
                for (int v = 14; v >= 8; v--) {
                    snprintf(cand, sizeof(cand), "%s\\Programs\\Python\\Python3%d\\python.exe", local_app, v);
                    if (_access(cand, 0) == 0) {
                        snprintf(s_agent_py, sizeof(s_agent_py), "\"%s\"", cand);
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) strcpy(s_agent_py, "python");
        }
    }
    py_bin = s_agent_py;

    char py_cmd[MAX_PATH * 4];
    if (context && strlen(context) > 0) {
        char safe_ctx[1024];
        size_t clen = 0;
        for (size_t i = 0; context[i] && clen + 2 < sizeof(safe_ctx); i++) {
            if (context[i] == '"' || context[i] == '\\' || context[i] == '\r' || context[i] == '\n') continue;
            safe_ctx[clen++] = context[i];
        }
        safe_ctx[clen] = '\0';
        snprintf(py_cmd, sizeof(py_cmd), "%s \"%s\" \"%s\" \"%s\" \"%s\"", py_bin, script_path, safe_query, res_file, safe_ctx);
    } else {
        snprintf(py_cmd, sizeof(py_cmd), "%s \"%s\" \"%s\" \"%s\"", py_bin, script_path, safe_query, res_file);
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessA(NULL, py_cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DWORD wr = WaitForSingleObject(pi.hProcess, 22000);
        if (wr == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#else
    const char *tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    char res_file[512];
    snprintf(res_file, sizeof(res_file), "%s/sphere_ai_model_%d.json", tmp, (int)getpid());
    unlink(res_file);

    char safe_query[512];
    size_t qlen = 0;
    for (size_t i = 0; query[i] && qlen + 2 < sizeof(safe_query); i++) {
        if (query[i] == '"' || query[i] == '\\' || query[i] == '`' || query[i] == '$' || query[i] == '\r' || query[i] == '\n') continue;
        safe_query[qlen++] = query[i];
    }
    safe_query[qlen] = '\0';

    char py_cmd[2048];
    if (context && strlen(context) > 0) {
        char safe_ctx[1024];
        size_t clen = 0;
        for (size_t i = 0; context[i] && clen + 2 < sizeof(safe_ctx); i++) {
            if (context[i] == '"' || context[i] == '\\' || context[i] == '`' || context[i] == '$' || context[i] == '\r' || context[i] == '\n') continue;
            safe_ctx[clen++] = context[i];
        }
        safe_ctx[clen] = '\0';
        snprintf(py_cmd, sizeof(py_cmd), "python3 sphere_ai_model.py \"%s\" \"%s\" \"%s\" 2>/dev/null || python sphere_ai_model.py \"%s\" \"%s\" \"%s\" 2>/dev/null", safe_query, res_file, safe_ctx, safe_query, res_file, safe_ctx);
    } else {
        snprintf(py_cmd, sizeof(py_cmd), "python3 sphere_ai_model.py \"%s\" \"%s\" 2>/dev/null || python sphere_ai_model.py \"%s\" \"%s\" 2>/dev/null", safe_query, res_file, safe_query, res_file);
    }
    int sys_ret = system(py_cmd);
    (void)sys_ret;
#endif

    FILE *f = fopen(res_file, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    int success = 0;
    if (sz > 5 && sz < 32768) {
        char *buf = (char *)malloc(sz + 1);
        if (buf) {
            size_t rd = fread(buf, 1, sz, f);
            buf[rd] = '\0';

            char ans[2048] = {0};
            char found_str[16] = {0};
            char happy_str[16] = {0};

            extract_json_value(buf, "found", found_str, sizeof(found_str));
            extract_json_value(buf, "answer", ans, sizeof(ans));
            extract_json_value(buf, "is_happy", happy_str, sizeof(happy_str));

            if ((strcmp(found_str, "1") == 0 || strcmp(found_str, "true") == 0) && strlen(ans) > 5) {
                strncpy(out_response, ans, max_len - 1);
                out_response[max_len - 1] = '\0';
                if (out_is_happy) {
                    *out_is_happy = (atoi(happy_str) == 1 || strcmp(happy_str, "true") == 0);
                }
                success = 1;
            }
            free(buf);
        }
    }
    fclose(f);
#ifdef _WIN32
    DeleteFileA(res_file);
#else
    unlink(res_file);
#endif
    return success;
}

int agent_process_query(const char *query, char *out_response, size_t max_len, int *out_is_happy) {
    if (!query || !out_response || max_len == 0) return 0;
    out_response[0] = '\0';
    if (out_is_happy) *out_is_happy = 0;

    /* 1. Fast direct math evaluation */
    if (brain_evaluate_math(query, out_response, max_len)) {
        return 1;
    }

    /* 2. Intelligent small AI model reasoning (answers directly or fetches live facts depending on query) */
    if (query_sphere_ai_model(query, "", out_response, max_len, out_is_happy)) {
        return 1;
    }

    /* 3. Robust fallback to native offline brain rules and knowledge */
    if (brain_handle_direct(query, out_response, max_len, out_is_happy)) {
        return 1;
    }

    if (brain_reason_decision(query, out_response, max_len, out_is_happy)) {
        return 1;
    }

    SearchResult res;
    int search_found = search_web_and_images(query, &res);

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
