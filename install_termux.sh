#!/data/data/com.termux/files/usr/bin/bash
# ==============================================================================
# Sphere AI - Termux (Android) Automated Installer & Setup Script
# ==============================================================================

set -e

echo ""
echo "=========================================================="
echo "         Sphere AI - Android Termux Setup                "
echo "=========================================================="
echo ""

# 1. Update Termux package repositories
echo "[1/4] Updating package repositories..."
pkg update -y || apt-get update -y

# 2. Install essential packages: compiler, build tools, python, audio tools
echo "[2/4] Installing C compiler, Python, ffmpeg, mpv, and audio tools..."
pkg install -y clang make python python-pip git ffmpeg mpv termux-api espeak || \
pkg install -y clang make python git ffmpeg mpv termux-api espeak

# 3. Install Python dependencies for full song search and voice support
echo "[3/4] Installing Python libraries (yt-dlp, SpeechRecognition)..."
# Do NOT run 'pip install --upgrade pip' in Termux as Termux manages pip via pkg
pip install --break-system-packages yt-dlp speechrecognition 2>/dev/null || \
pip install yt-dlp speechrecognition 2>/dev/null || \
pip install --break-system-packages yt-dlp 2>/dev/null || \
pip install yt-dlp 2>/dev/null || true

# 4. Compile native C binary
echo "[4/4] Compiling native Sphere AI binary..."
make clean || rm -f sphere_ai *.o
make

chmod +x sphere_ai sphere_voice_service.py music_search.py 2>/dev/null || true

echo ""
echo "=========================================================="
echo "              Installation Complete!                     "
echo "=========================================================="
echo ""
echo "To start Sphere AI, run:"
echo "    ./sphere_ai"
echo ""
echo "Controls & Tips:"
echo "  * Speech Output: Works automatically via 'termux-tts-speak' if"
echo "    the Termux:API app is installed, or simulates cadence text."
echo "  * Speech Input: Say 'Hey Sphere' or press Space / Enter anytime"
echo "    to trigger query listening mode."
echo "  * Full Songs: Say or type 'play [song name]'."
echo "  * Exit: Say or type 'exit' or press Esc (Sphere will say"
echo "    'see you later, bye' and exit cleanly)."
echo ""
