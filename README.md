# SphereAI — Cross-Platform Native C Voice AI Agent & Braille Visualizer

An ultra-optimized, cross-platform Voice AI Agent built entirely in native C (`brain.c`) and lightweight audio IPC. Runs on **Windows**, **Linux**, and **Android (Termux)**. Requires **zero external heavy neural models (no Qwen/Ollama)**, **zero GPU (0% GPU usage)**, and responds **instantly in milliseconds**.

---

## 🌟 Key Features & Capabilities

1. **Cross-Platform Native Engine**:
   - Runs identically on **Windows (MinGW/MSVC)**, **Linux (Debian, Ubuntu, Fedora, Arch)**, and **Android Termux**.
   - Auto-detects OS capabilities: POSIX pipes & pthreads on Linux/Termux, Win32 threads & Job Objects on Windows.

2. **Wake-Word & Keyboard Voice Activation**:
   - **Voice**: The AI stays in low-power standby mode (gentle green Braille particle ball). Saying **"Hey Sphere"** or **"Sphere"** wakes it up (turns purple and begins listening).
   - **Single-breath queries**: Say *"Hey Sphere what is the speed of light"* and it wakes up, searches, and answers directly.
   - **Keyboard Trigger**: Press **Space** or **Enter** anytime to manually trigger voice listening mode (ideal for Termux/SSH or noisy environments).

3. **Real-time Live Speech Transcription (Google-style)**:
   - Your speech is recognized and displayed in real time on the screen word-by-word (`Listening: "what is the speed of light..."`).
   - The purple particle sphere revolves dynamically according to your incoming voice volume.

4. **Word-by-word Formatted Speech Streaming**:
   - High-speed speech output via native engines:
     - **Windows**: Windows SAPI Speech Synthesis (`System.Speech`).
     - **Linux**: `espeak-ng` or `spd-say` (Speech Dispatcher).
     - **Android Termux**: `termux-tts-speak` (Termux:API) with simulated cadence fallback.
   - Words are printed **one-by-one in exact synchronization with speech audio**, neatly word-wrapped and formatted.

5. **Instant Voice Barge-In / Interruption**:
   - Saying **"Hey Sphere"** or **"stop"** while the AI is speaking immediately cuts off speech audio (<1ms) and resumes listening for your new request.

6. **Native C AI Brain (`brain.c`)**:
   - **Math Reasoning**: Evaluates arithmetic, percentages, powers, and roots with 100% mathematical precision in $<0.001\text{ ms}$.
   - **Sensory Visual Descriptions**: 5-layer physical breakdown (form factor, materials, luster, lighting, and functional parts) fused with live web search definitions and Wikimedia Commons imagery.
   - **Cognitive Reasoning**: Instant comparative analysis, engineering tradeoffs, and advice.

7. **Braille Particle Visual Engine**:
   - 60 FPS flicker-free Braille particle 3D sphere rendered purely in terminal ANSI.
   - Morphs into a glowing golden smiling face when joyful or praised, then seamlessly reassembles into the sphere.

8. **Voice-Controlled Exits**:
   - Say **"exit"**, **"goodbye"**, **"quit"**, or press **Esc** / **'q'** to shut down.
   - Sphere announces **"see you later, bye"** with synchronized subtitles before cleanly terminating child processes and restoring terminal settings.

9. **Whole Song Music Playback & Dance Visualizer**:
   - Say *"Hey Sphere play Bohemian Rhapsody"*, *"play Despacito"*, or *"play Shape of You"*.
   - Streams the **full-length song** dynamically via `mpv`, `ffmpeg`, or `termux-media-player` while the 3D particle sphere pulses and dances to the music rhythm.
   - Say *"stop"* or press Esc to pause or exit music.

---

## 🚀 Quick Start Guide

### 📱 Android Termux
1. Run the automated installer:
   ```bash
   bash install_termux.sh
   ```
   *Or install manually:*
   ```bash
   pkg update -y
   pkg install -y clang make python python-pip git ffmpeg mpv termux-api
   pip install --break-system-packages yt-dlp speechrecognition 2>/dev/null || pip install yt-dlp speechrecognition
   make
   ```
2. Start Sphere AI:
   ```bash
   ./sphere_ai
   ```
   > **Termux Note**: For microphone and TTS support, install the free **Termux:API** companion app from F-Droid. You can also press **Space** or **Enter** to type or trigger voice prompts anytime.

---

### 🐧 Linux (Ubuntu, Debian, Fedora, Arch)
1. Run the automated installer:
   ```bash
   chmod +x install_linux.sh
   ./install_linux.sh
   ```
   *Or install manually on Debian/Ubuntu:*
   ```bash
   sudo apt update
   sudo apt install -y build-essential python3 python3-pip curl mpv espeak-ng ffmpeg portaudio19-dev
   pip3 install yt-dlp speechrecognition pyaudio
   make
   ```
2. Start Sphere AI:
   ```bash
   ./sphere_ai
   ```

---

### 🪟 Windows
1. Build using MinGW:
   ```bash
   mingw32-make
   ```
2. Run:
   ```bash
   .\sphere_ai.exe
   ```

---

## 🎮 Interaction & Controls

| Action | Voice Command | Keyboard Shortcut |
|---|---|---|
| **Activate / Listen** | *"Hey Sphere"* or *"Sphere"* | **Space** or **Enter** |
| **Ask Anything** | *"Hey Sphere what is 125 times 8?"*, *"what does a galaxy look like?"* | Type query in terminal |
| **Play Full Songs** | *"Hey Sphere play Bohemian Rhapsody"*, *"play jazz"* | Type `play [song]` |
| **Interrupt / Stop** | Say *"Hey Sphere"* or *"stop"* | Press **Space** |
| **Trigger Smile** | *"You did a great job"*, *"smile"*, *"happy"* | — |
| **Exit Sphere** | *"exit"*, *"goodbye"*, *"quit"* | **Esc** or **'q'** |
