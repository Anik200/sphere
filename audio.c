#include "audio.h"
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <io.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator_local, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator_local, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IAudioClient_local, 0x1CB9AD4C, 0xDBFA, 0x4C32, 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2);
DEFINE_GUID(IID_IAudioCaptureClient_local, 0xC8ADBD64, 0xE71E, 0x48A0, 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17);
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

static AudioVisData current_vis = {0};
static volatile int loopback_running = 0;
#ifdef _WIN32
static HANDLE hLoopbackThread = NULL;
static CRITICAL_SECTION vis_cs;
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

#define PRE_ROLL_SAMPLES (SAMPLE_RATE * 2)
static short preroll_buffer[PRE_ROLL_SAMPLES];
static int preroll_head = 0;
static int preroll_count = 0;

static void __attribute__((unused)) preroll_push(short sample) {
    preroll_buffer[preroll_head] = sample;
    preroll_head = (preroll_head + 1) % PRE_ROLL_SAMPLES;
    if (preroll_count < PRE_ROLL_SAMPLES) preroll_count++;
}

static int __attribute__((unused)) preroll_copy_to(short *dest, int max_samples, int requested_samples) {
    if (requested_samples > preroll_count) requested_samples = preroll_count;
    if (requested_samples > max_samples) requested_samples = max_samples;
    if (requested_samples <= 0) return 0;

    int start = (preroll_head - requested_samples + PRE_ROLL_SAMPLES) % PRE_ROLL_SAMPLES;
    for (int i = 0; i < requested_samples; i++) {
        dest[i] = preroll_buffer[(start + i) % PRE_ROLL_SAMPLES];
    }
    return requested_samples;
}

static char final_speech_buf[512] = {0};
static volatile int final_speech_ready = 0;

static char spoken_words_stream[4096] = {0};
static volatile int service_running = 0;

#ifdef _WIN32
static DWORD WINAPI wasapi_loopback_thread(LPVOID lpParam) {
    (void)lpParam;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 0;

    IMMDeviceEnumerator *pEnumerator = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator_local, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator_local, (void**)&pEnumerator);
    if (FAILED(hr) || !pEnumerator) {
        CoUninitialize();
        return 0;
    }

    IMMDevice *pDevice = NULL;
    hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(pEnumerator, eRender, eConsole, &pDevice);
    if (FAILED(hr) || !pDevice) {
        pEnumerator->lpVtbl->Release(pEnumerator);
        CoUninitialize();
        return 0;
    }

    IAudioClient *pAudioClient = NULL;
    hr = pDevice->lpVtbl->Activate(pDevice, &IID_IAudioClient_local, CLSCTX_ALL, NULL, (void**)&pAudioClient);
    if (FAILED(hr) || !pAudioClient) {
        pDevice->lpVtbl->Release(pDevice);
        pEnumerator->lpVtbl->Release(pEnumerator);
        CoUninitialize();
        return 0;
    }

    WAVEFORMATEX *pwfx = NULL;
    hr = pAudioClient->lpVtbl->GetMixFormat(pAudioClient, &pwfx);
    if (FAILED(hr) || !pwfx) {
        pAudioClient->lpVtbl->Release(pAudioClient);
        pDevice->lpVtbl->Release(pDevice);
        pEnumerator->lpVtbl->Release(pEnumerator);
        CoUninitialize();
        return 0;
    }

    int is_float = 0;
    if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        is_float = 1;
    } else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *pExt = (WAVEFORMATEXTENSIBLE*)pwfx;
        static const GUID guid_float = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
        if (memcmp(&pExt->SubFormat, &guid_float, sizeof(GUID)) == 0) {
            is_float = 1;
        }
    }

    int channels = pwfx->nChannels;
    if (channels < 1) channels = 1;

    REFERENCE_TIME hnsBufferDuration = 2000000; /* 200 ms */
    hr = pAudioClient->lpVtbl->Initialize(pAudioClient, AUDCLNT_SHAREMODE_SHARED,
                                         AUDCLNT_STREAMFLAGS_LOOPBACK,
                                         hnsBufferDuration, 0, pwfx, NULL);
    if (FAILED(hr)) {
        CoTaskMemFree(pwfx);
        pAudioClient->lpVtbl->Release(pAudioClient);
        pDevice->lpVtbl->Release(pDevice);
        pEnumerator->lpVtbl->Release(pEnumerator);
        CoUninitialize();
        return 0;
    }

    IAudioCaptureClient *pCaptureClient = NULL;
    hr = pAudioClient->lpVtbl->GetService(pAudioClient, &IID_IAudioCaptureClient_local, (void**)&pCaptureClient);
    if (FAILED(hr) || !pCaptureClient) {
        CoTaskMemFree(pwfx);
        pAudioClient->lpVtbl->Release(pAudioClient);
        pDevice->lpVtbl->Release(pDevice);
        pEnumerator->lpVtbl->Release(pEnumerator);
        CoUninitialize();
        return 0;
    }

    hr = pAudioClient->lpVtbl->Start(pAudioClient);
    if (FAILED(hr)) {
        pCaptureClient->lpVtbl->Release(pCaptureClient);
        CoTaskMemFree(pwfx);
        pAudioClient->lpVtbl->Release(pAudioClient);
        pDevice->lpVtbl->Release(pDevice);
        pEnumerator->lpVtbl->Release(pEnumerator);
        CoUninitialize();
        return 0;
    }

    float bass_lp1 = 0.0f, bass_lp2 = 0.0f;
    float hp_treble = 0.0f;
    float bass_history[40] = {0};
    int bass_hist_idx = 0;
    DWORD last_beat_tick = 0;
    float prev_bass_rms = 0.0f;

    while (loopback_running) {
        Sleep(8);

        UINT32 packetLength = 0;
        pCaptureClient->lpVtbl->GetNextPacketSize(pCaptureClient, &packetLength);

        if (packetLength == 0) {
            EnterCriticalSection(&vis_cs);
            current_vis.overall_amp *= 0.91f;
            current_vis.bass *= 0.89f;
            current_vis.mid *= 0.90f;
            current_vis.treble *= 0.88f;
            current_vis.beat_pulse *= 0.82f;
            current_vis.is_beat = 0;
            LeaveCriticalSection(&vis_cs);
            continue;
        }

        float sum_raw_sq = 0.0f;
        float sum_bass_sq = 0.0f;
        float sum_mid_sq = 0.0f;
        float sum_treble_sq = 0.0f;
        int total_frames = 0;

        while (packetLength > 0 && loopback_running) {
            BYTE *pData = NULL;
            UINT32 numFramesRead = 0;
            DWORD flags = 0;
            hr = pCaptureClient->lpVtbl->GetBuffer(pCaptureClient, &pData, &numFramesRead, &flags, NULL, NULL);
            if (SUCCEEDED(hr)) {
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData != NULL) {
                    if (is_float) {
                        const float *f_samples = (const float*)pData;
                        for (UINT32 i = 0; i < numFramesRead; i++) {
                            float mono = 0.0f;
                            for (int c = 0; c < channels; c++) {
                                mono += f_samples[i * channels + c];
                            }
                            mono /= (float)channels;

                            sum_raw_sq += mono * mono;

                            /* 2-pole cascaded low-pass for bass (cutoff ~160 Hz at 48kHz) */
                            bass_lp1 += 0.021f * (mono - bass_lp1);
                            bass_lp2 += 0.021f * (bass_lp1 - bass_lp2);
                            sum_bass_sq += bass_lp2 * bass_lp2;

                            /* Treble high-pass (cutoff ~2600 Hz) */
                            hp_treble += 0.29f * (mono - hp_treble);
                            float treble_s = mono - hp_treble;
                            sum_treble_sq += treble_s * treble_s;

                            /* Midrange */
                            float mid_s = hp_treble - bass_lp1;
                            sum_mid_sq += mid_s * mid_s;

                            total_frames++;
                        }
                    } else if (pwfx->wBitsPerSample == 16) {
                        const short *s_samples = (const short*)pData;
                        for (UINT32 i = 0; i < numFramesRead; i++) {
                            float mono = 0.0f;
                            for (int c = 0; c < channels; c++) {
                                mono += (float)s_samples[i * channels + c];
                            }
                            mono /= ((float)channels * 32768.0f);

                            sum_raw_sq += mono * mono;

                            bass_lp1 += 0.021f * (mono - bass_lp1);
                            bass_lp2 += 0.021f * (bass_lp1 - bass_lp2);
                            sum_bass_sq += bass_lp2 * bass_lp2;

                            hp_treble += 0.29f * (mono - hp_treble);
                            float treble_s = mono - hp_treble;
                            sum_treble_sq += treble_s * treble_s;

                            float mid_s = hp_treble - bass_lp1;
                            sum_mid_sq += mid_s * mid_s;

                            total_frames++;
                        }
                    }
                }
                pCaptureClient->lpVtbl->ReleaseBuffer(pCaptureClient, numFramesRead);
            }
            pCaptureClient->lpVtbl->GetNextPacketSize(pCaptureClient, &packetLength);
        }

        if (total_frames > 0) {
            float raw_rms = sqrtf(sum_raw_sq / total_frames);
            float bass_rms = sqrtf(sum_bass_sq / total_frames);
            float mid_rms = sqrtf(sum_mid_sq / total_frames);
            float treble_rms = sqrtf(sum_treble_sq / total_frames);

            /* Non-linear perceptual saturation */
            float n_amp = tanhf(raw_rms * 4.5f);
            float n_bass = tanhf(bass_rms * 7.2f);
            float n_mid = tanhf(mid_rms * 5.2f);
            float n_treble = tanhf(treble_rms * 5.8f);

            /* Beat Onset Detection */
            bass_history[bass_hist_idx] = n_bass;
            bass_hist_idx = (bass_hist_idx + 1) % 40;
            float bass_sum = 0.0f;
            for (int k = 0; k < 40; k++) bass_sum += bass_history[k];
            float bass_avg = bass_sum / 40.0f;

            DWORD now = GetTickCount();
            int beat_hit = 0;
            float delta_bass = n_bass - prev_bass_rms;
            prev_bass_rms = n_bass;

            if (n_bass > 0.08f && n_bass > (bass_avg * 1.25f) && delta_bass > 0.015f && (now - last_beat_tick) >= 160) {
                beat_hit = 1;
                last_beat_tick = now;
            }

            EnterCriticalSection(&vis_cs);
            if (n_amp > current_vis.overall_amp) {
                current_vis.overall_amp = current_vis.overall_amp * 0.15f + n_amp * 0.85f;
            } else {
                current_vis.overall_amp = current_vis.overall_amp * 0.72f + n_amp * 0.28f;
            }

            if (n_bass > current_vis.bass) {
                current_vis.bass = current_vis.bass * 0.12f + n_bass * 0.88f;
            } else {
                current_vis.bass = current_vis.bass * 0.68f + n_bass * 0.32f;
            }

            if (n_mid > current_vis.mid) {
                current_vis.mid = current_vis.mid * 0.20f + n_mid * 0.80f;
            } else {
                current_vis.mid = current_vis.mid * 0.78f + n_mid * 0.22f;
            }

            if (n_treble > current_vis.treble) {
                current_vis.treble = current_vis.treble * 0.15f + n_treble * 0.85f;
            } else {
                current_vis.treble = current_vis.treble * 0.72f + n_treble * 0.28f;
            }

            if (beat_hit) {
                current_vis.beat_pulse = 1.0f;
                current_vis.is_beat = 1;
            }
            LeaveCriticalSection(&vis_cs);
        }
    }

    pAudioClient->lpVtbl->Stop(pAudioClient);
    pCaptureClient->lpVtbl->Release(pCaptureClient);
    CoTaskMemFree(pwfx);
    pAudioClient->lpVtbl->Release(pAudioClient);
    pDevice->lpVtbl->Release(pDevice);
    pEnumerator->lpVtbl->Release(pEnumerator);
    CoUninitialize();
    return 0;
}
#endif

static void __attribute__((unused)) write_wav_file(const char *filename, const short *pcm_data, int num_samples) {
    if (!filename || !pcm_data || num_samples <= 0) return;
    FILE *f = fopen(filename, "wb");
    if (!f) return;

    short max_v = 0;
    for (int i = 0; i < num_samples; i++) {
        short abs_v = (pcm_data[i] < 0) ? (short)(-pcm_data[i]) : pcm_data[i];
        if (abs_v > max_v) max_v = abs_v;
    }
    float gain = 1.0f;
    if (max_v > 80 && max_v < 18000) {
        gain = 24000.0f / (float)max_v;
        if (gain > 8.0f) gain = 8.0f;
    }

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

    if (gain > 1.05f) {
        short gain_buf[1024];
        int idx = 0;
        while (idx < num_samples) {
            int chunk = num_samples - idx;
            if (chunk > 1024) chunk = 1024;
            for (int k = 0; k < chunk; k++) {
                int s = (int)(pcm_data[idx + k] * gain);
                if (s > 32767) s = 32767;
                else if (s < -32768) s = -32768;
                gain_buf[k] = (short)s;
            }
            fwrite(gain_buf, 2, chunk, f);
            idx += chunk;
        }
    } else {
        fwrite(pcm_data, 2, num_samples, f);
    }
    fclose(f);
}

#ifdef _WIN32
static void audio_send_cmd(const char *cmd);

static const char *get_python_command(void) {
    static char s_py_cmd[MAX_PATH] = {0};
    if (s_py_cmd[0] != '\0') return s_py_cmd;

    const char *local_app = getenv("LOCALAPPDATA");
    if (local_app) {
        char cand[MAX_PATH];
        for (int v = 14; v >= 8; v--) {
            snprintf(cand, sizeof(cand), "%s\\Programs\\Python\\Python3%d\\python.exe", local_app, v);
            if (_access(cand, 0) == 0) {
                snprintf(s_py_cmd, sizeof(s_py_cmd), "\"%s\"", cand);
                return s_py_cmd;
            }
        }
    }

    if (system("python --version >nul 2>&1") == 0) {
        strcpy(s_py_cmd, "python");
        return s_py_cmd;
    }
    if (system("py -3 --version >nul 2>&1") == 0) {
        strcpy(s_py_cmd, "py -3");
        return s_py_cmd;
    }

    strcpy(s_py_cmd, "python");
    return s_py_cmd;
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

static void strip_wake_prefix(char *text) {
    if (!text || strlen(text) == 0) return;

    char *start = text;
    while (*start && (isspace((unsigned char)*start) || *start == '"' || *start == '\'' || *start == ',' || *start == '.')) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    if (strlen(text) == 0) return;

    char lower[512];
    size_t len = strlen(text);
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)text[i]);
    }
    lower[len] = '\0';

    const char *greetings[] = {
        "hey", "hi", "hello", "ok", "okay", "yo", "ay", "hay", "her", "here", "hear", "wake up", "listen", "the", "a"
    };
    const char *sphere_names[] = {
        "spike", "spik", "spyke", "sphere", "spear", "fear", "spare", "sphear"
    };

    const char *p = lower;
    for (size_t g = 0; g < sizeof(greetings)/sizeof(greetings[0]); g++) {
        size_t glen = strlen(greetings[g]);
        if (strncmp(p, greetings[g], glen) == 0) {
            char next = p[glen];
            if (next == ' ' || next == ',' || next == '.' || next == '!' || next == '?' || next == '-' || next == '\0') {
                p += glen;
                while (*p && (*p == ' ' || *p == ',' || *p == '.' || *p == '!' || *p == '?' || *p == '-' || *p == ':')) {
                    p++;
                }
                break;
            }
        }
    }

    int matched_sphere = 0;
    for (size_t s = 0; s < sizeof(sphere_names)/sizeof(sphere_names[0]); s++) {
        size_t slen = strlen(sphere_names[s]);
        if (strncmp(p, sphere_names[s], slen) == 0) {
            char next = p[slen];
            if (next == ' ' || next == ',' || next == '.' || next == '!' || next == '?' || next == '-' || next == ':' || next == '\0') {
                p += slen;
                while (*p && (*p == ' ' || *p == ',' || *p == '.' || *p == '!' || *p == '?' || *p == '-' || *p == ':' || *p == '"' || *p == '\'')) {
                    p++;
                }
                matched_sphere = 1;
                break;
            }
        }
    }

    if (strcmp(lower, "hey spike") == 0 || strcmp(lower, "spike") == 0 ||
        strcmp(lower, "spike wake up") == 0 || strcmp(lower, "wake up spike") == 0 ||
        strcmp(lower, "listen spike") == 0 || strcmp(lower, "listen") == 0 ||
        strcmp(lower, "hi spike") == 0 || strcmp(lower, "hello spike") == 0 ||
        strcmp(lower, "ok spike") == 0 || strcmp(lower, "okay spike") == 0 ||
        strcmp(lower, "hey sphere") == 0 || strcmp(lower, "sphere") == 0 ||
        strcmp(lower, "hi sphere") == 0 || strcmp(lower, "hello sphere") == 0 ||
        strcmp(lower, "ok sphere") == 0 || strcmp(lower, "okay sphere") == 0 ||
        strcmp(lower, "spear") == 0 || strcmp(lower, "hey spear") == 0 ||
        strcmp(lower, "wake up") == 0 || strcmp(lower, "wake up sphere") == 0) {
        text[0] = '\0';
        return;
    }

    if (matched_sphere) {
        if (strncmp(p, "please ", 7) == 0) p += 7;
        while (*p && isspace((unsigned char)*p)) p++;

        size_t offset = (size_t)(p - lower);
        const char *remaining = text + offset;
        while (*remaining && (*remaining == ' ' || *remaining == ',' || *remaining == '.' || *remaining == ':' || *remaining == '!' || *remaining == '?' || *remaining == '"' || *remaining == '\'')) {
            remaining++;
        }

        if (strlen(remaining) > 0) {
            memmove(text, remaining, strlen(remaining) + 1);
        } else {
            text[0] = '\0';
        }
    }
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
                    if (strncmp(line_buf, "AUDIO_DEVICE:", 13) == 0) {
                        printf("[Sphere Audio] Active Microphone: %s\n", line_buf + 13);
                        fflush(stdout);
                    } else if (strncmp(line_buf, "AMP:", 4) == 0) {
                        float a = (float)atof(line_buf + 4);
                        if (a > input_amp) {
                            input_amp = input_amp * 0.20f + a * 0.80f;
                        } else {
                            input_amp = input_amp * 0.65f + a * 0.35f;
                        }
                    } else if (strncmp(line_buf, "HYP:", 4) == 0) {
                        const char *hyp = line_buf + 4;
                        char clean_hyp[512];
                        snprintf(clean_hyp, sizeof(clean_hyp), "%s", hyp);
                        strip_wake_prefix(clean_hyp);
                        char display[600];
                        snprintf(display, sizeof(display), "Listening: \"%s...\"", clean_hyp);
                        renderer_set_subtitle(display);
                        renderer_set_input_buffer(clean_hyp);
                    } else if (strcmp(line_buf, "WAKE") == 0) {
                        wake_detected = 1;
                        audio_lock();
                        num_recorded_samples = preroll_copy_to(recorded_samples, MAX_RECORDED_SAMPLES, (int)(SAMPLE_RATE * 1.0f));
                        is_recording_query = 1;
                        audio_unlock();
                    } else if (strncmp(line_buf, "FINAL:", 6) == 0) {
                        audio_lock();
                        is_recording_query = 0;
                        num_recorded_samples = 0;
                        audio_unlock();

                        const char *sapi_text = line_buf + 6;
                        while (*sapi_text && isspace((unsigned char)*sapi_text)) sapi_text++;

                        char precise_text[512] = {0};
                        int got_precise = 0;

                        char temp_path[MAX_PATH];
#ifdef _WIN32
                        GetTempPathA(MAX_PATH, temp_path);
#else
                        const char *env_tmp = getenv("TMPDIR");
                        if (!env_tmp) env_tmp = "/tmp";
                        snprintf(temp_path, sizeof(temp_path), "%s/", env_tmp);
#endif
                        char wav_file[MAX_PATH];
                        snprintf(wav_file, sizeof(wav_file), "%ssphere_query.wav", temp_path);
                        char sre_wav_file[MAX_PATH];
                        snprintf(sre_wav_file, sizeof(sre_wav_file), "%ssphere_sre_audio.wav", temp_path);
                        char res_file[MAX_PATH];
                        snprintf(res_file, sizeof(res_file), "%ssphere_stt_res.txt", temp_path);

                        const char *target_wav = NULL;

                        FILE *fcheck = fopen(wav_file, "rb");
                        if (fcheck) {
                            fseek(fcheck, 0, SEEK_END);
                            long sz = ftell(fcheck);
                            fclose(fcheck);
                            if (sz > 2000) target_wav = wav_file;
                        }

                        if (!target_wav) {
                            FILE *fsre = fopen(sre_wav_file, "rb");
                            if (fsre) {
                                fseek(fsre, 0, SEEK_END);
                                long sz = ftell(fsre);
                                fclose(fsre);
                                if (sz > 2000) {
                                    target_wav = sre_wav_file;
                                }
                            }
                        }

                        if (target_wav) {
#ifdef _WIN32
                            DeleteFileA(res_file);

                            char exe_dir[MAX_PATH] = {0};
                            GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
                            char *last_bs = strrchr(exe_dir, '\\');
                            if (last_bs) *last_bs = '\0';
                            else strcpy(exe_dir, ".");

                            char transcribe_path[MAX_PATH];
                            snprintf(transcribe_path, sizeof(transcribe_path), "%s\\transcribe.py", exe_dir);
                            if (_access(transcribe_path, 0) != 0) {
                                strcpy(transcribe_path, "transcribe.py");
                            }

                            const char *py_bin = get_python_command();

                            char cmd[MAX_PATH * 3];
                            snprintf(cmd, sizeof(cmd), "%s \"%s\" \"%s\" \"%s\"", py_bin, transcribe_path, target_wav, res_file);

                            STARTUPINFOA si;
                            PROCESS_INFORMATION pi;
                            memset(&si, 0, sizeof(si));
                            si.cb = sizeof(si);
                            si.dwFlags = STARTF_USESHOWWINDOW;
                            si.wShowWindow = SW_HIDE;

                            if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                                WaitForSingleObject(pi.hProcess, 6000);
                                CloseHandle(pi.hProcess);
                                CloseHandle(pi.hThread);
                            }

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
                                DeleteFileA(res_file);
                            }
                            DeleteFileA(wav_file);
                            DeleteFileA(sre_wav_file);
#else
                            unlink(res_file);
                            char cmd[1024];
                            snprintf(cmd, sizeof(cmd), "python3 transcribe.py \"%s\" \"%s\" 2>/dev/null || python transcribe.py \"%s\" \"%s\" 2>/dev/null", target_wav, res_file, target_wav, res_file);
                            int ret = system(cmd);
                            (void)ret;

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
                                unlink(res_file);
                            }
                            unlink(wav_file);
                            unlink(sre_wav_file);
#endif
                        }

                        audio_lock();
                        if (got_precise && strlen(precise_text) > 0) {
                            strip_wake_prefix(precise_text);
                            snprintf(final_speech_buf, sizeof(final_speech_buf), "%s", precise_text);
                        } else {
                            char clean_sapi[512];
                            snprintf(clean_sapi, sizeof(clean_sapi), "%s", sapi_text);
                            strip_wake_prefix(clean_sapi);
                            snprintf(final_speech_buf, sizeof(final_speech_buf), "%s", clean_sapi);
                        }
                        final_speech_buf[sizeof(final_speech_buf) - 1] = '\0';
                        if (strlen(final_speech_buf) > 0) {
                            final_speech_ready = 1;
                        }
                        num_recorded_samples = 0;
                        audio_unlock();
                    } else if (strncmp(line_buf, "STREAM:", 7) == 0) {
                        const char *slice = line_buf + 7;
                        audio_lock();
                        snprintf(spoken_words_stream, sizeof(spoken_words_stream), "Spike: %s", slice);
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
    InitializeCriticalSection(&vis_cs);
    loopback_running = 1;
    hLoopbackThread = CreateThread(NULL, 0, wasapi_loopback_thread, NULL, 0, NULL);

    char exe_dir[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *last_bs = strrchr(exe_dir, '\\');
    if (last_bs) *last_bs = '\0';
    else strcpy(exe_dir, ".");

    char voice_exe[MAX_PATH];
    snprintf(voice_exe, sizeof(voice_exe), "%s\\sphere_voice_service.exe", exe_dir);

    if (_access(voice_exe, 0) != 0 && _access("sphere_voice_service.exe", 0) != 0) {
        system("C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\csc.exe /optimize+ "
               "/reference:C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Speech\\v4.0_4.0.0.0__31bf3856ad364e35\\System.Speech.dll "
               "/out:sphere_voice_service.exe sphere_voice_service.cs >nul 2>&1");
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
    char cmdLine[MAX_PATH * 2];
    if (_access(voice_exe, 0) == 0) {
        snprintf(cmdLine, sizeof(cmdLine), "\"%s\" %lu", voice_exe, parent_pid);
    } else if (_access("sphere_voice_service.exe", 0) == 0) {
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
    if (!is_recording_query) {
        num_recorded_samples = preroll_copy_to(recorded_samples, MAX_RECORDED_SAMPLES, (int)(SAMPLE_RATE * 0.8f));
        is_recording_query = 1;
    }
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

void audio_get_vis_data(AudioVisData *out_vis) {
    if (!out_vis) return;
#ifdef _WIN32
    EnterCriticalSection(&vis_cs);
    *out_vis = current_vis;
    current_vis.beat_pulse *= 0.82f;
    current_vis.is_beat = 0;
    LeaveCriticalSection(&vis_cs);
#else
    audio_lock();
    *out_vis = current_vis;
    current_vis.beat_pulse *= 0.82f;
    current_vis.is_beat = 0;
    audio_unlock();
#endif

    float mic = (current_amp > input_amp) ? current_amp : input_amp;
    current_amp *= 0.93f;
    input_amp *= 0.93f;
    if (mic > 1.0f) mic = 1.0f;
    if (mic < 0.0f) mic = 0.0f;

    if (!is_music_playing) {
        if (mic > out_vis->overall_amp) {
            out_vis->overall_amp = mic;
            out_vis->mid = mic * 0.85f;
            out_vis->bass = mic * 0.50f;
        }
        if (is_speaking && out_vis->overall_amp < 0.12f) {
            static float synth_phase = 0.0f;
            synth_phase += 0.22f;
            float synth_amp = 0.25f + 0.18f * sinf(synth_phase) * cosf(synth_phase * 0.45f);
            if (synth_amp < 0.10f) synth_amp = 0.10f;
            if (synth_amp > out_vis->overall_amp) {
                out_vis->overall_amp = synth_amp;
                out_vis->mid = synth_amp * 0.80f;
                out_vis->bass = synth_amp * 0.45f;
            }
        }
    }
}

float audio_get_current_amp(void) {
    AudioVisData v;
    audio_get_vis_data(&v);
    return v.overall_amp;
}

static void clean_for_speech(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    size_t d = 0;
    int in_paren = 0;
    int in_bracket = 0;
    int paren_chars = 0;

    for (size_t s = 0; src[s] && d + 1 < max_len; s++) {
        unsigned char uc = (unsigned char)src[s];

        /* Skip 4-byte UTF-8 emojis (0xF0..0xF4) */
        if (uc >= 0xF0 && uc <= 0xF4) {
            for (int k = 0; k < 3 && src[s + 1] && ((unsigned char)src[s + 1] & 0xC0) == 0x80; k++) {
                s++;
            }
            continue;
        }

        /* Skip 3-byte UTF-8 symbols / pictographs (0xE2..0xEF) except Braille which won't appear in speech anyway */
        if (uc >= 0xE2 && uc <= 0xEF) {
            /* Check if degree sign or similar */
            for (int k = 0; k < 2 && src[s + 1] && ((unsigned char)src[s + 1] & 0xC0) == 0x80; k++) {
                s++;
            }
            continue;
        }

        /* Degree symbol: 0xC2 0xB0 (in UTF-8) */
        if (uc == 0xC2 && (unsigned char)src[s + 1] == 0xB0) {
            s++;
            const char *deg = " degrees ";
            for (int k = 0; deg[k] && d + 1 < max_len; k++) {
                dst[d++] = deg[k];
            }
            continue;
        }

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
    interrupted_detected = 0;
    interrupted_with_wake = 0;
    wake_detected = 0;
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
    loopback_running = 0;
    if (hLoopbackThread) {
        WaitForSingleObject(hLoopbackThread, 600);
        CloseHandle(hLoopbackThread);
        hLoopbackThread = NULL;
    }
    DeleteCriticalSection(&vis_cs);
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
