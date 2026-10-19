#!/usr/bin/env bash

set -e

echo ""
echo "=========================================================="
echo "              Sphere AI - Linux Setup                     "
echo "=========================================================="
echo ""

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "[ERROR] Please run this script with root permissions or install sudo."
        exit 1
    fi
fi

echo "[1/4] Detecting package manager and installing dependencies..."

if command -v apt-get >/dev/null 2>&1; then
    echo "Detected Debian / Ubuntu / Mint..."
    $SUDO apt-get update -y
    $SUDO apt-get install -y build-essential python3 python3-pip curl mpv espeak-ng ffmpeg \
                            libasound2-dev portaudio19-dev speech-dispatcher
elif command -v dnf >/dev/null 2>&1; then
    echo "Detected Fedora / RHEL..."
    $SUDO dnf install -y gcc make python3 python3-pip curl mpv espeak-ng ffmpeg \
                        portaudio-devel alsa-lib-devel
elif command -v pacman >/dev/null 2>&1; then
    echo "Detected Arch Linux / Manjaro..."
    $SUDO pacman -Sy --noconfirm base-devel python python-pip curl mpv espeak-ng ffmpeg \
                                portaudio alsa-lib
elif command -v zypper >/dev/null 2>&1; then
    echo "Detected openSUSE..."
    $SUDO zypper install -y gcc make python3 python3-pip curl mpv espeak-ng ffmpeg \
                          portaudio-devel
else
    echo "[WARNING] Unknown package manager. Please ensure gcc, make, python3, curl, mpv, and espeak-ng are installed."
fi

echo "[2/4] Installing Python packages (yt-dlp, speechrecognition, pyaudio)..."
PIP_CMD="pip3"
if ! command -v pip3 >/dev/null 2>&1; then
    PIP_CMD="pip"
fi

$PIP_CMD install --upgrade pip 2>/dev/null || true
$PIP_CMD install --break-system-packages yt-dlp speechrecognition pyaudio 2>/dev/null || \
$PIP_CMD install yt-dlp speechrecognition pyaudio 2>/dev/null || \
$PIP_CMD install --break-system-packages yt-dlp speechrecognition 2>/dev/null || \
$PIP_CMD install yt-dlp speechrecognition 2>/dev/null || true

echo "[3/4] Compiling native Sphere AI binary..."
make clean
make

echo "[4/4] Setting execution permissions..."
chmod +x sphere_ai sphere_voice_service.py music_search.py

echo ""
echo "=========================================================="
echo "              Installation Complete!                     "
echo "=========================================================="
echo ""
echo "To start Sphere AI, run:"
echo "    ./sphere_ai"
echo ""
echo "Controls & Tips:"
echo "  * Speech Output: High-speed speech output via espeak-ng or spd-say"
echo "  * Speech Input: Say 'Hey Sphere' to activate, or press Space / Enter"
echo "    to manually trigger voice listening mode"
echo "  * Full Songs: Say or type 'play [song name]' (streamed via mpv/ffmpeg)"
echo "  * Exit: Say or type 'exit' or press Esc (Sphere will say"
echo "    'see you later, bye' and exit cleanly)"
echo ""
