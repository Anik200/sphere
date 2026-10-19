#include "audio.h"
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <io.h>
#else
#define _GNU_SOURCE
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#endif

#define SAMPLES_PER_BUFFER 800
#define SAMPLE_RATE 16000
#define MAX_RECORDED_SAMPLES (SAMPLE_RATE * 15)

#ifdef _WIN32
static HWAVEIN hWaveIn = NULL;
static WAVEHDR waveHeaders[2];
static short audio_buffers[2][SAMPLES_PER_BUFFER];

static HANDLE hChildStd_IN_Wr = NULL;
static HANDLE hChildStd_OUT_Rd = NULL;
static HANDLE hProcess = NULL;
static HANDLE hReaderThread = NULL;
static CRITICAL_SECTION audio_cs;

static void audio_lock(void) {
    EnterCriticalSection(&audio_cs);
}
static void audio_unlock(void) {
    LeaveCriticalSection(&audio_cs);
}
#else
static int posix_child_in = -1;
static int posix_child_out = -1;
static pid_t posix_child_pid = -1;
static pthread_t posix_reader_thread;
static pthread_mutex_t audio_cs = PTHREAD_MUTEX_INITIALIZER;

static void audio_lock(void) {
    pthread_mutex_lock(&audio_cs);
}
static void audio_unlock(void) {
    pthread_mutex_unlock(&audio_cs);
}
#endif

static volatile float current_amp = 0.0f;
static volatile float input_amp = 0.0f;
static volatile int wake_detected = 0;
static volatile int interrupted_detected = 0;
static volatile int interrupted_with_wake = 0;
static volatile int exit_detected = 0;
static volatile int timeout_detected = 0;
static volatile int is_speaking = 0;
static volatile int is_music_playing = 0;

static short recorded_samples[MAX_RECORDED_SAMPLES];
static volatile int num_recorded_samples = 0;
static volatile int is_recording_query = 0;

static char final_speech_buf[512] = {0};
static volatile int final_speech_ready = 0;

static char spoken_words_stream[4096] = {0};
static volatile int service_running = 0;

static void write_wav_file(const char *filename, const short *pcm_data, int num_samples) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;

    int byte_rate = SAMPLE_RATE * 1 * 2;
    int data_chunk_size = num_samples * 2;
    int chunk_size = 36 + data_chunk_size;

    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);

    int subchunk1_size = 16;
    short audio_format = 1;
    short num_channels = 1;
    int sample_rate = SAMPLE_RATE;
    short block_align = 2;
    short bits_per_sample = 16;

    fwrite(&subchunk1_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_chunk_size, 4, 1, f);
    fwrite(pcm_data, 2, num_samples, f);
    fclose(f);
}

#ifdef _WIN32
static void audio_send_cmd(const char *cmd);

static void CALLBACK waveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    (void)dwInstance;
    (void)dwParam2;
    if (uMsg == WIM_DATA && service_running) {
        WAVEHDR *pHdr = (WAVEHDR *)dwParam1;
        short *samples = (short *)pHdr->lpData;
        int count = pHdr->dwBytesRecorded / sizeof(short);

        if (count > 0) {
            long long sum = 0;
            for (int i = 0; i < count; i++) {
                sum += (long long)samples[i] * samples[i];
                if (is_recording_query && num_recorded_samples < MAX_RECORDED_SAMPLES) {
                    recorded_samples[num_recorded_samples++] = samples[i];
                }
            }
            float rms = sqrtf((float)sum / count) / 32768.0f;
            float target_amp = (rms - 0.0025f) * 28.0f;
            if (target_amp < 0.0f) target_amp = 0.0f;
            if (target_amp > 1.0f) target_amp = 1.0f;
            if (target_amp > current_amp) {
                current_amp = current_amp * 0.20f + target_amp * 0.80f;
            } else {
                current_amp = current_amp * 0.70f + target_amp * 0.30f;
            }
        }

        waveInAddBuffer(hwi, pHdr, sizeof(WAVEHDR));
    }
}
#endif

static void audio_send_cmd(const char *cmd) {
#ifdef _WIN32
    if (!hChildStd_IN_Wr || !service_running) return;
    char buf[8192];
    int len = snprintf(buf, sizeof(buf), "%s\n", cmd);
    DWORD written = 0;
    WriteFile(hChildStd_IN_Wr, buf, len, &written, NULL);
    FlushFileBuffers(hChildStd_IN_Wr);
#else
    if (posix_child_in < 0 || !service_running) return;
    char buf[8192];
    int len = snprintf(buf, sizeof(buf), "%s\n", cmd);
    ssize_t ret = write(posix_child_in, buf, len);
    (void)ret;
#endif
}

#ifdef _WIN32
static DWORD WINAPI voice_reader_thread(LPVOID lpParam) {
    (void)lpParam;
#else
static void *voice_reader_thread(void *lpParam) {
    (void)lpParam;
#endif
    char chunk[512];
    char line_buf[2048];
    int line_len = 0;

#ifdef _WIN32
    DWORD bytesRead = 0;
    while (service_running && ReadFile(hChildStd_OUT_Rd, chunk, sizeof(chunk) - 1, &bytesRead, NULL)) {
        if (bytesRead == 0) break;
        int count = (int)bytesRead;
#else
    while (service_running) {
        ssize_t n = read(posix_child_out, chunk, sizeof(chunk) - 1);
        if (n <= 0) break;
        int count = (int)n;
#endif
        chunk[count] = '\0';

        for (int i = 0; i < count; i++) {
            char c = chunk[i];
            if (c == '\r') continue;
            if (c == '\n') {
                line_buf[line_len] = '\0';
                if (line_len > 0) {
                    if (strncmp(line_buf, "AMP:", 4) == 0) {
                        float a = (float)atof(line_buf + 4);
                        if (a > input_amp) {
                            input_amp = input_amp * 0.20f + a * 0.80f;
                        } else {
                            input_amp = input_amp * 0.65f + a * 0.35f;
                        }
                    } else if (strncmp(line_buf, "HYP:", 4) == 0) {
                        const char *hyp = line_buf + 4;
                        char display[600];
                        snprintf(display, sizeof(display), "Listening: \"%s...\"", hyp);
                        renderer_set_subtitle(display);
                        renderer_set_input_buffer(hyp);
                    } else if (strcmp(line_buf, "WAKE") == 0) {
                        wake_detected = 1;
                        audio_lock();
                        is_recording_query = 1;
                        num_recorded_samples = 0;
                        audio_unlock();
                    } else if (strncmp(line_buf, "FINAL:", 6) == 0) {
                        audio_lock();
                        is_recording_query = 0;
                        int samples_count = num_recorded_samples;
                        audio_unlock();

                        const char *sapi_text = line_buf + 6;
                        while (*sapi_text && isspace((unsigned char)*sapi_text)) sapi_text++;

                        int sapi_is_music = (strncasecmp(sapi_text, "play", 4) == 0 ||
                                             strncasecmp(sapi_text, "song", 4) == 0 ||
                                             strncasecmp(sapi_text, "listen", 6) == 0 ||
                                             strncasecmp(sapi_text, "put on", 6) == 0);

                        char precise_text[512] = {0};
                        int got_precise = 0;

                        if (!sapi_is_music && strlen(sapi_text) < 16 && samples_count >= (int)(SAMPLE_RATE * 1.0f)) {
#ifdef _WIN32
                            char temp_path[MAX_PATH];
                            GetTempPathA(MAX_PATH, temp_path);
                            char wav_file[MAX_PATH];
                            snprintf(wav_file, sizeof(wav_file), "%ssphere_query.wav", temp_path);
                            char res_file[MAX_PATH];
                            snprintf(res_file, sizeof(res_file), "%ssphere_stt_res.txt", temp_path);
                            DeleteFileA(res_file);

                            write_wav_file(wav_file, recorded_samples, samples_count);

                            char cmd[MAX_PATH * 3];
                            snprintf(cmd, sizeof(cmd), "python transcribe.py \"%s\" \"%s\"", wav_file, res_file);

                            STARTUPINFOA si;
                            PROCESS_INFORMATION pi;
                            memset(&si, 0, sizeof(si));
                            si.cb = sizeof(si);
                            si.dwFlags = STARTF_USESHOWWINDOW;
                            si.wShowWindow = SW_HIDE;

                            if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                                WaitForSingleObject(pi.hProcess, 2500);
                                CloseHandle(pi.hProcess);
                                CloseHandle(pi.hThread);
                            }
#else
                            const char *temp_path = getenv("TMPDIR");
                            if (!temp_path) temp_path = "/tmp";
                            char wav_file[512];
                            snprintf(wav_file, sizeof(wav_file), "%s/sphere_query_%d.wav", temp_path, (int)getpid());
                            char res_file[512];
                            snprintf(res_file, sizeof(res_file), "%s/sphere_stt_res_%d.txt", temp_path, (int)getpid());
                            unlink(res_file);

                            write_wav_file(wav_file, recorded_samples, samples_count);

                            char cmd[1024];
                            snprintf(cmd, sizeof(cmd), "python3 transcribe.py \"%s\" \"%s\" 2>/dev/null || python transcribe.py \"%s\" \"%s\" 2>/dev/null", wav_file, res_file, wav_file, res_file);
                            int ret = system(cmd);
                            (void)ret;
#endif

                            FILE *f = fopen(res_file, "r");
                            if (f) {
                                if (fgets(precise_text, sizeof(precise_text), f)) {
                                    size_t l = strlen(precise_text);
                                    while (l > 0 && (precise_text[l-1] == '\n' || precise_text[l-1] == '\r' || precise_text[l-1] == ' ')) {
                                        precise_text[--l] = '\0';
                                    }
                                    if (strlen(precise_text) > 0) got_precise = 1;
                                }
                                fclose(f);
#ifdef _WIN32
                                DeleteFileA(res_file);
#else
                                unlink(res_file);
#endif
                            }
#ifdef _WIN32
                            DeleteFileA(wav_file);
#else
                            unlink(wav_file);
#endif
                        }

                        audio_lock();
                        if (sapi_is_music || strlen(sapi_text) >= 12 || !got_precise) {
                            strncpy(final_speech_buf, sapi_text, sizeof(final_speech_buf) - 1);
                        } else {
                            strncpy(final_speech_buf, precise_text, sizeof(final_speech_buf) - 1);
                        }
                        final_speech_buf[sizeof(final_speech_buf) - 1] = '\0';
                        final_speech_ready = 1;
                        num_recorded_samples = 0;
                        audio_unlock();
                    } else if (strncmp(line_buf, "STREAM:", 7) == 0) {
                        const char *slice = line_buf + 7;
                        audio_lock();
                        snprintf(spoken_words_stream, sizeof(spoken_words_stream), "Sphere: %s", slice);
                        renderer_set_subtitle(spoken_words_stream);
                        audio_unlock();
                    } else if (strcmp(line_buf, "SPEAK_DONE") == 0) {
                        is_speaking = 0;
                    } else if (strncmp(line_buf, "MUSIC_STARTED", 13) == 0) {
                        is_music_playing = 1;
                    } else if (strcmp(line_buf, "MUSIC_DONE") == 0) {
                        is_music_playing = 0;
                    } else if (strncmp(line_buf, "INTERRUPTED", 11) == 0) {
                        is_speaking = 0;
                        is_music_playing = 0;
                        interrupted_detected = 1;
                        char lower_int[256];
                        size_t k = 0;
                        for (; line_buf[k] && k + 1 < sizeof(lower_int); k++) {
                            lower_int[k] = (char)tolower((unsigned char)line_buf[k]);
                        }
                        lower_int[k] = '\0';
                        if ((strstr(lower_int, "sphere") || strstr(lower_int, "spear") ||
                             strstr(lower_int, "hey") || strstr(lower_int, "her") ||
                             strstr(lower_int, "hi") || strstr(lower_int, "hello")) &&
                            !strstr(lower_int, "stop") && !strstr(lower_int, "quiet") &&
                            !strstr(lower_int, "pause") && !strstr(lower_int, "cancel") &&
                            !strstr(lower_int, "shut up")) {
                            interrupted_with_wake = 1;
                        } else {
                            interrupted_with_wake = 0;
                        }
                    } else if (strcmp(line_buf, "EXIT") == 0) {
                        is_speaking = 0;
                        is_music_playing = 0;
                        exit_detected = 1;
                    } else if (strcmp(line_buf, "TIMEOUT") == 0) {
                        timeout_detected = 1;
                        audio_lock();
                        is_recording_query = 0;
                        num_recorded_samples = 0;
                        audio_unlock();
                    }
                }
                line_len = 0;
            } else {
                if (line_len + 1 < (int)sizeof(line_buf)) {
                    line_buf[line_len++] = c;
                }
            }
        }
    }
    return 0;
}

int audio_init(void) {
#ifdef _WIN32
    InitializeCriticalSection(&audio_cs);

    if (_access("sphere_voice_service.exe", 0) != 0) {
        system("C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\csc.exe /optimize+ "
               "/reference:C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Speech\\v4.0_4.0.0.0__31bf3856ad364e35\\System.Speech.dll "
               "/out:sphere_voice_service.exe sphere_voice_service.cs >nul 2>&1");
    }

    WAVEFORMATEX pFormat;
    pFormat.wFormatTag = WAVE_FORMAT_PCM;
    pFormat.nChannels = 1;
    pFormat.nSamplesPerSec = SAMPLE_RATE;
    pFormat.wBitsPerSample = 16;
    pFormat.nBlockAlign = 2;
    pFormat.nAvgBytesPerSec = SAMPLE_RATE * 2;
    pFormat.cbSize = 0;

    MMRESULT res = waveInOpen(&hWaveIn, WAVE_MAPPER, &pFormat, (DWORD_PTR)waveInProc, 0, CALLBACK_FUNCTION);
    if (res == MMSYSERR_NOERROR) {
        for (int i = 0; i < 2; i++) {
            memset(&waveHeaders[i], 0, sizeof(WAVEHDR));
            waveHeaders[i].lpData = (LPSTR)audio_buffers[i];
            waveHeaders[i].dwBufferLength = sizeof(audio_buffers[i]);
            waveInPrepareHeader(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
            waveInAddBuffer(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
        }
        waveInStart(hWaveIn);
    } else {
        hWaveIn = NULL;
    }

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hChildStd_OUT_Wr = NULL;
    HANDLE hChildStd_IN_Rd = NULL;

    if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) return 0;
    SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr, &saAttr, 0)) {
        CloseHandle(hChildStd_OUT_Rd);
        CloseHandle(hChildStd_OUT_Wr);
        return 0;
    }
    SetHandleInformation(hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hChildStd_OUT_Wr;
    si.hStdOutput = hChildStd_OUT_Wr;
    si.hStdInput = hChildStd_IN_Rd;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    DWORD parent_pid = GetCurrentProcessId();
    char cmdLine[128];
    if (_access("sphere_voice_service.exe", 0) == 0) {
        snprintf(cmdLine, sizeof(cmdLine), "sphere_voice_service.exe %lu", parent_pid);
    } else {
        snprintf(cmdLine, sizeof(cmdLine), "python sphere_voice_service.py %lu", parent_pid);
    }
    BOOL success = CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    CloseHandle(hChildStd_OUT_Wr);
    CloseHandle(hChildStd_IN_Rd);

    if (!success) {
        CloseHandle(hChildStd_OUT_Rd);
        CloseHandle(hChildStd_IN_Wr);
        hChildStd_OUT_Rd = NULL;
        hChildStd_IN_Wr = NULL;
        return 0;
    }

    hProcess = pi.hProcess;
    CloseHandle(pi.hThread);

    HANDLE hJob = CreateJobObject(NULL, NULL);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        memset(&jeli, 0, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(hJob, hProcess);
    }

    service_running = 1;

    hReaderThread = CreateThread(NULL, 0, voice_reader_thread, NULL, 0, NULL);

    audio_set_standby_mode();
    return 1;
#else
    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);

        char parent_pid_str[32];
        snprintf(parent_pid_str, sizeof(parent_pid_str), "%d", (int)getppid());

        execlp("python3", "python3", "sphere_voice_service.py", parent_pid_str, (char *)NULL);
        execlp("python", "python", "sphere_voice_service.py", parent_pid_str, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        close(in_pipe[0]);
        close(out_pipe[1]);
        posix_child_in = in_pipe[1];
        posix_child_out = out_pipe[0];
        posix_child_pid = pid;
        service_running = 1;

        pthread_create(&posix_reader_thread, NULL, (void *(*)(void *))voice_reader_thread, NULL);
        audio_set_standby_mode();
        return 1;
    } else {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return 0;
    }
#endif
}

void audio_set_standby_mode(void) {
    audio_lock();
    is_recording_query = 0;
    num_recorded_samples = 0;
    audio_unlock();
    audio_send_cmd("CMD:WAKE_STANDBY");
}

void audio_set_listening_mode(void) {
    audio_lock();
    num_recorded_samples = 0;
    is_recording_query = 1;
    audio_unlock();
    audio_send_cmd("CMD:LISTEN_QUERY");
}

int audio_check_wake_detected(void) {
    if (wake_detected) {
        wake_detected = 0;
        return 1;
    }
    return 0;
}

int audio_check_interrupted(int *out_wake_retrigger) {
    if (interrupted_detected) {
        interrupted_detected = 0;
        if (out_wake_retrigger) {
            *out_wake_retrigger = interrupted_with_wake;
        }
        interrupted_with_wake = 0;
        return 1;
    }
    return 0;
}

int audio_check_exit_detected(void) {
    if (exit_detected) {
        exit_detected = 0;
        return 1;
    }
    return 0;
}

int audio_check_timeout_detected(void) {
    if (timeout_detected) {
        timeout_detected = 0;
        return 1;
    }
    return 0;
}

int audio_get_final_speech(char *out_text, size_t max_len) {
    if (!out_text || max_len == 0) return 0;
    int got = 0;
    audio_lock();
    if (final_speech_ready) {
        strncpy(out_text, final_speech_buf, max_len - 1);
        out_text[max_len - 1] = '\0';
        final_speech_buf[0] = '\0';
        final_speech_ready = 0;
        got = 1;
    }
    audio_unlock();
    return got;
}

float audio_get_current_amp(void) {
    float mic = (current_amp > input_amp) ? current_amp : input_amp;
    current_amp *= 0.93f;
    input_amp *= 0.93f;
    if (mic > 1.0f) mic = 1.0f;
    if (mic < 0.0f) mic = 0.0f;

    if (is_speaking) {
        static float synth_phase = 0.0f;
        synth_phase += 0.22f;
        float synth_amp = 0.28f + 0.20f * sinf(synth_phase) * cosf(synth_phase * 0.45f);
        if (synth_amp < 0.10f) synth_amp = 0.10f;
        if (mic > synth_amp) return mic;
        return synth_amp;
    }
    if (is_music_playing) {
        static float music_phase = 0.0f;
        music_phase += 0.16f;
        float music_amp = 0.20f + 0.10f * sinf(music_phase) * cosf(music_phase * 0.5f);
        if (music_amp < 0.10f) music_amp = 0.10f;
        if (mic > music_amp) return mic;
        return music_amp;
    }
    return mic;
}

static void clean_for_speech(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    size_t d = 0;
    int in_paren = 0;
    int in_bracket = 0;
    int paren_chars = 0;

    for (size_t s = 0; src[s] && d + 1 < max_len; s++) {
        char c = src[s];
        if (c == '(') { in_paren++; paren_chars = 0; continue; }
        if (c == ')') { if (in_paren > 0) in_paren--; continue; }
        if (c == '[') { in_bracket++; continue; }
        if (c == ']') { if (in_bracket > 0) in_bracket--; continue; }

        if (in_paren > 0) {
            paren_chars++;
            if (paren_chars > 45) {
                in_paren = 0;
            } else {
                continue;
            }
        }
        if (in_bracket > 0) continue;

        if (c == '*' || c == '#' || c == '`' || c == '_' || c == '"' || c == '\\') continue;
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ' && d > 0 && dst[d - 1] == ' ') continue;
        dst[d++] = c;
    }
    dst[d] = '\0';
}

void audio_speak(const char *text) {
    if (!text || strlen(text) == 0) return;
    if (is_music_playing) {
        audio_stop_music();
    }
    audio_lock();
    spoken_words_stream[0] = '\0';
    audio_unlock();

    char clean_text[4096];
    clean_for_speech(text, clean_text, sizeof(clean_text));

    if (strlen(clean_text) < 3) {
        size_t d = 0;
        for (size_t s = 0; text[s] && d + 1 < sizeof(clean_text); s++) {
            char c = text[s];
            if (c == '\n' || c == '\r' || c == '\t' || c == '"' || c == '\\') c = ' ';
            if (c == ' ' && d > 0 && clean_text[d - 1] == ' ') continue;
            clean_text[d++] = c;
        }
        clean_text[d] = '\0';
    }

    if (strlen(clean_text) == 0) return;

    char cmd[4096 + 32];
    snprintf(cmd, sizeof(cmd), "CMD:SPEAK %s", clean_text);
    is_speaking = 1;
    audio_send_cmd(cmd);
}

void audio_speak_exit(const char *text) {
    if (!text || strlen(text) == 0) return;
    if (is_music_playing) {
        audio_stop_music();
    }
    audio_lock();
    spoken_words_stream[0] = '\0';
    audio_unlock();

    char clean_text[4096];
    clean_for_speech(text, clean_text, sizeof(clean_text));

    if (strlen(clean_text) < 3) {
        size_t d = 0;
        for (size_t s = 0; text[s] && d + 1 < sizeof(clean_text); s++) {
            char c = text[s];
            if (c == '\n' || c == '\r' || c == '\t' || c == '"' || c == '\\') c = ' ';
            if (c == ' ' && d > 0 && clean_text[d - 1] == ' ') continue;
            clean_text[d++] = c;
        }
        clean_text[d] = '\0';
    }

    if (strlen(clean_text) == 0) return;

    char cmd[4096 + 32];
    snprintf(cmd, sizeof(cmd), "CMD:EXIT_SPEAK %s", clean_text);
    is_speaking = 1;
    audio_send_cmd(cmd);
}

void audio_stop_speech(void) {
    audio_send_cmd("CMD:STOP_SPEAK");
    is_speaking = 0;
}

int audio_is_speaking(void) {
    return is_speaking;
}

void audio_play_music(const char *url) {
    if (!url || strlen(url) == 0) return;
    if (is_speaking) {
        audio_stop_speech();
    }
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "CMD:PLAY_MUSIC %s", url);
    is_music_playing = 1;
    audio_send_cmd(cmd);
}

void audio_stop_music(void) {
    audio_send_cmd("CMD:STOP_MUSIC");
    is_music_playing = 0;
}

int audio_is_music_playing(void) {
    return is_music_playing;
}

void audio_cleanup(void) {
    service_running = 0;
    audio_send_cmd("CMD:STOP_SPEAK");
    audio_send_cmd("CMD:STOP_MUSIC");
    audio_send_cmd("CMD:EXIT");
    is_speaking = 0;
    is_music_playing = 0;
#ifdef _WIN32
    if (hWaveIn) {
        waveInReset(hWaveIn);
        for (int i = 0; i < 2; i++) {
            waveInUnprepareHeader(hWaveIn, &waveHeaders[i], sizeof(WAVEHDR));
        }
        waveInClose(hWaveIn);
        hWaveIn = NULL;
    }
    if (hProcess) {
        WaitForSingleObject(hProcess, 500);
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        hProcess = NULL;
    }
    if (hReaderThread) {
        WaitForSingleObject(hReaderThread, 200);
        CloseHandle(hReaderThread);
        hReaderThread = NULL;
    }
    if (hChildStd_IN_Wr) {
        CloseHandle(hChildStd_IN_Wr);
        hChildStd_IN_Wr = NULL;
    }
    if (hChildStd_OUT_Rd) {
        CloseHandle(hChildStd_OUT_Rd);
        hChildStd_OUT_Rd = NULL;
    }
    DeleteCriticalSection(&audio_cs);
#else
    if (posix_child_in >= 0) {
        close(posix_child_in);
        posix_child_in = -1;
    }
    if (posix_child_out >= 0) {
        close(posix_child_out);
        posix_child_out = -1;
    }
    if (posix_child_pid > 0) {
        kill(posix_child_pid, SIGTERM);
        waitpid(posix_child_pid, NULL, WNOHANG);
        posix_child_pid = -1;
    }
#endif
}
