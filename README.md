# SphereAI

A fast, lightweight, native C voice assistant and 3D ANSI Braille visualizer. Runs on Windows, Linux, and Android (Termux) with zero GPU requirement and zero external heavy model dependencies.

```text
  ◆ SphereAI   ● STANDBY   │ › Ready. Say "Hey Sphere" to activate   MIC ▕███░░░░░░░▏ 30%
  ────────────────────────────────────────────────────────────────────────────────────────

                                     ⢀⢀⣀⣀⣀⡀⢀⡀
                                ⢀⡀⣰⠢⡎⢴⠘⢜⡣⠡⢰⠪⡰⢓⣂⡦⢀
                              ⣀⠖⠰⡁⠪⡀⠡⠄⠱⡀⠂⡌⠡⢐⠐⡁⢔⠂⡊⢱⢆⡀
                            ⡀⠞⠠⠭⠈⠤⠠⠡⠐⠁⡀⠐⠄ ⠅⠐⡀⠐⠄⡐⠊⠨⣐⠼⠡⡀
                           ⠄⡊⠐⢂⠐⠂⡂⠐⢂⡀⠊⡠⠄⠈⠰⠐⢀⠈⠔⠐⢀⠘⡀⠤⢈⠂⠃⡄
                          ⠐⡅⡌⢈⠄⠈⠂⡈⢐⢀⠐⢄⠐⢀⠑⠄⠂⡈⠄⠰ ⠁⡀⠔⠐⠄⠂⣁⠢
                           ⢣⠢⡈⡄⠉⢂⡀⠂⣀⠂⡀⠂⡂⠠⠈⢈⠐ ⠅⡈⠌⠐⠐ ⣁⣉⠴⠆
                            ⢣⡨⠄⠕ ⠐⢄ ⠂⠠⠂ ⠄⠁⡀⢄⠈⡀⠠⠁⠂⠡⢁⡠⠬⠂
                            ⠈⠝⣂⡃⠌⠂⠄ ⠆⠡⠈⢐⠐ ⡠⠐ ⢀⠂⠂⠅⢈⡠⠨⠗
                             ⡝⠤⠑⡘⡂⢠⠁⢀⠆⠑ ⠠⠁⠠⡀⡉⠠⠐⠂⡌⣀⡠⠍⠃

  Sphere › Sphere AI online. Say "Hey Sphere" to activate.

   Space  Talk   Enter  Submit   "Hey Sphere"  Wake   Esc  Exit
```

---

## Features

- **Native C Architecture**: Written in ANSI C (`main.c`, `renderer.c`, `audio.c`, `brain.c`, `search.c`, `agent.c`) with zero bloated frameworks.
- **Small AI Model & Dynamic Fetch Router**: Integrates local Small Language Models (SLMs) such as `qwen2.5:0.5b` or `smollm`. Intelligently decides whether to answer directly with AI reasoning or dynamically fetch live facts (weather, news, current events) first before generating speech.
- **True Real-Time Beat Visualizer**: Windows Audio Session API (WASAPI) master loopback capture with multi-band DSP (kick bass 40-160Hz, treble >2.6kHz, mid) and transient onset detection. The 3D Braille sphere pumps radially on bass kicks and ripples on beats like an audio visualizer.
- **Modern TCSS / Terminal UI**: OpenCode-inspired status pills, dynamic Unicode block volume meters, actor dialogue tags, and responsive keybinding pills.
- **3D Braille Particle Sphere**: Real-time 60 FPS 3D particle engine rendered via 2x4 Braille glyphs with smooth radial color blooms and audio-driven ripples.
- **Wake-Word & Push-to-Talk**: Instant activation on "Hey Sphere" or by pressing Space/Enter. Supports seamless single-breath queries.
- **Live Speech Transcription**: Word-by-word streaming STT and TTS speech output synchronized with onscreen dialogue subtitles.
- **Instant Interruption**: Barge-in support (<1ms latency) cuts off playback when you speak or press Space.
- **Full Track Music Playback**: Dynamically resolves and streams music tracks while the particle sphere visualizes rhythms.
- **Cross-Platform**: Native compilation on Windows (MinGW/MSVC), Linux, and Android (Termux).

---

## Quick Start

### Windows (PowerShell)

Recommended terminal: **Windows Terminal** (or standard PowerShell with a TrueType font like Cascadia Code, Consolas, or JetBrains Mono).

```powershell
# 1. Open PowerShell in the sphere directory
cd c:\path\to\sphere

# 2. (Recommended) Set console encoding to UTF-8 for smooth 3D Braille rendering
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# 3. Build the native engine and voice service
mingw32-make

# 4. (Optional) Run the ultra-fast small AI model locally (Qwen 0.5B, ~390 MB)
# If Ollama is installed, pull the small model once:
ollama pull qwen2.5:0.5b
# Start the Ollama background server if not already active:
ollama serve

# 5. Launch SphereAI
.\sphere_ai.exe
```

> [!TIP]
> **No Ollama? No Problem!** If Ollama is not running, SphereAI seamlessly falls back to its native offline reasoning engine and live web search without crashing or lagging. You can also run prebuilt binaries directly:
> ```powershell
> .\sphere_ai.exe
> ```

### Linux
```bash
chmod +x install_linux.sh
./install_linux.sh
./sphere_ai
```
*Or manual build on Debian/Ubuntu:*
```bash
sudo apt update && sudo apt install -y build-essential python3 python3-pip curl mpv espeak-ng ffmpeg portaudio19-dev
pip3 install yt-dlp speechrecognition pyaudio
make
./sphere_ai
```

### Android (Termux)
```bash
bash install_termux.sh
./sphere_ai
```
*Requires Termux:API companion app from F-Droid for microphone and TTS access.*

---

## Controls

| Key / Trigger | Action | Description |
|---|---|---|
| **Space** / **Enter** | Voice / Input | Activates listening mode or submits typed prompt |
| **"Hey Sphere"** | Wake Word | Activates listening mode via voice |
| **Direct Typing** | Prompt Input | Type a prompt directly into the interactive buffer |
| **"play \<track\>"** | Music Playback | Streams full song audio and visualizes frequency ripples |
| **"stop"** / **Space** | Interrupt | Immediately stops current speech or music playback |
| **Esc** / **'q'** | Exit | Cleanly terminates background processes and restores terminal |

---

## Technical Overview

| Component | Implementation |
|---|---|
| **Rendering** | Braille subpixel rasterization (2x4 dots per cell) at 60 FPS |
| **Audio IPC** | Platform-native audio backends (SAPI, espeak-ng, Termux:API) |
| **Reasoning Engine** | Native C mathematical parsing and deterministic inference (<1ms) |
| **Knowledge Retrieval** | Lightweight HTTP search queries and candidate scraping |
| **Streaming Media** | Low-latency audio stream extraction and playback |

---

## License

MIT
