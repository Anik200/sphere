#!/usr/bin/env python3

import sys
import os
import time
import threading
import subprocess
import shutil
import re
import math
import tempfile

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", line_buffering=True)

current_mode = "STANDBY"
mode_lock = threading.Lock()
is_speaking = False
is_music_playing = False
speech_cancel_flag = False
current_speaking_proc = None
current_music_proc = None
wake_start_time = 0.0

EXIT_REGEX = re.compile(
    r"^(exit|quit|goodbye|good\s+bye|bye|bye\s+bye|see\s+you\s+later|close\s+app|close\s+the\s+app|exit\s+app|quit\s+app|shut\s+down|shutdown|terminate)$",
    re.IGNORECASE
)
EXIT_WAKE_REGEX = re.compile(
    r"^(hey\s+sphere|sphere|hi\s+sphere|hello\s+sphere)\s+(exit|quit|goodbye|good\s+bye|bye|bye\s+bye|see\s+you\s+later|close\s+app|shutdown|shut\s+down)$",
    re.IGNORECASE
)
STOP_REGEX = re.compile(
    r"^(hey\s+sphere\s+|sphere\s+)?(stop|be\s+quiet|shut\s+up|pause|cancel|stop\s+talking|stop\s+it|stop\s+music|stop\s+the\s+music)$",
    re.IGNORECASE
)
WAKE_REGEX = re.compile(
    r"^(hey\s+sphere|sphere|hi\s+sphere|hello\s+sphere|ok\s+sphere|okay\s+sphere|yo\s+sphere|hey\s+spear|spear|wake\s+up\s+sphere|wake\s+up|her\s+sphere|here\s+sphere|hey\s+fear|hey\s+spare|hay\s+sphere)(\s+(?P<q>.*))?$",
    re.IGNORECASE
)

def emit(msg):
    try:
        sys.stdout.write(msg + "\n")
        sys.stdout.flush()
    except Exception:
        pass

def is_exit_phrase(text):
    if not text:
        return False
    clean = text.strip().strip(".?!, '\"")
    return bool(EXIT_REGEX.match(clean) or EXIT_WAKE_REGEX.match(clean))

def get_available_tts():
    if shutil.which("espeak-ng"):
        return "espeak-ng"
    if shutil.which("espeak"):
        return "espeak"

    if shutil.which("spd-say"):
        return "spd-say"

    if shutil.which("say"):
        return "say"

    if shutil.which("termux-tts-speak"):
        return "termux-tts-speak"
    return "simulated"

TTS_ENGINE = get_available_tts()

def get_available_music_player():
    if shutil.which("mpv"):
        return "mpv"
    if shutil.which("ffplay"):
        return "ffplay"
    if shutil.which("termux-media-player"):
        return "termux-media-player"
    if shutil.which("cvlc"):
        return "cvlc"
    if shutil.which("vlc"):
        return "vlc"
    if shutil.which("play"):
        return "play"
    if shutil.which("aplay"):
        return "aplay"
    return None

MUSIC_PLAYER = get_available_music_player()

def set_mode(new_mode):
    global current_mode, wake_start_time
    with mode_lock:
        current_mode = new_mode
        wake_start_time = time.time()
        emit(f"MODE:{new_mode}")

def stop_music():
    global is_music_playing, current_music_proc
    is_music_playing = False
    if current_music_proc:
        try:
            current_music_proc.terminate()
            current_music_proc.kill()
        except Exception:
            pass
        current_music_proc = None

def stop_speech():
    global is_speaking, speech_cancel_flag, current_speaking_proc
    speech_cancel_flag = True
    is_speaking = False
    if current_speaking_proc:
        try:
            current_speaking_proc.terminate()
            current_speaking_proc.kill()
        except Exception:
            pass
        current_speaking_proc = None

def speak_thread_worker(text, is_exit=False):
    global is_speaking, speech_cancel_flag, current_speaking_proc
    words = text.split()
    if not words:
        emit("SPEAK_DONE")
        return

    is_speaking = True
    speech_cancel_flag = False

    tts_proc = None
    tmp_wav = None

    try:
        if TTS_ENGINE in ("espeak-ng", "espeak"):
            if MUSIC_PLAYER == "mpv":
                with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tf:
                    tmp_wav = tf.name
                gen = subprocess.run([TTS_ENGINE, "-s", "155", "-w", tmp_wav, text], capture_output=True)
                if gen.returncode == 0 and os.path.exists(tmp_wav) and os.path.getsize(tmp_wav) > 44:
                    tts_proc = subprocess.Popen(["mpv", "--no-video", "--really-quiet", tmp_wav],
                                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                else:
                    tts_proc = subprocess.Popen([TTS_ENGINE, "-s", "155", text],
                                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                tts_proc = subprocess.Popen([TTS_ENGINE, "-s", "155", text],
                                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elif TTS_ENGINE == "spd-say":
            tts_proc = subprocess.Popen(["spd-say", "-r", "10", text], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elif TTS_ENGINE == "say":
            tts_proc = subprocess.Popen(["say", text], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elif TTS_ENGINE == "termux-tts-speak":
            tts_proc = subprocess.Popen(["termux-tts-speak", text], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass

    current_speaking_proc = tts_proc

    accumulated = []

    delay = 0.22 if len(words) > 5 else 0.30

    for w in words:
        if speech_cancel_flag:
            break
        accumulated.append(w)
        emit(f"STREAM:{' '.join(accumulated)}")
        emit("AMP:0.04")
        time.sleep(delay)

    if tts_proc and not speech_cancel_flag:
        try:
            tts_proc.wait(timeout=5.0 if is_exit else 2.5)
        except Exception:
            try:
                tts_proc.kill()
            except Exception:
                pass

    if tmp_wav and os.path.exists(tmp_wav):
        try:
            os.remove(tmp_wav)
        except Exception:
            pass

    emit(f"STREAM:{text}")
    emit("SPEAK_DONE")
    is_speaking = False
    current_speaking_proc = None

    if not is_exit:
        with mode_lock:
            if current_mode == "SPEAKING":
                set_mode("STANDBY")

def play_music_worker(url):
    global is_music_playing, current_music_proc
    stop_music()
    stop_speech()

    if not MUSIC_PLAYER:
        emit("ERROR:NO_MUSIC_PLAYER_FOUND")
        emit("MUSIC_DONE")
        set_mode("STANDBY")
        return

    cmd = []
    if MUSIC_PLAYER == "mpv":
        cmd = ["mpv", "--no-video", "--volume=75", "--really-quiet", url]
    elif MUSIC_PLAYER == "ffplay":
        cmd = ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", url]
    elif MUSIC_PLAYER == "termux-media-player":
        cmd = ["termux-media-player", "play", url]
    elif MUSIC_PLAYER in ("cvlc", "vlc"):
        cmd = [MUSIC_PLAYER, "--no-video", "--play-and-exit", "-q", url]
    elif MUSIC_PLAYER == "play":
        cmd = ["play", "-q", url]
    elif MUSIC_PLAYER == "aplay":
        cmd = ["aplay", "-q", url]
    else:
        cmd = [MUSIC_PLAYER, url]

    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        current_music_proc = proc
        is_music_playing = True
        set_mode("SPEAKING")
        emit("MUSIC_STARTED")

        phase = 0.0
        while is_music_playing and proc.poll() is None:
            time.sleep(0.08)
            phase += 0.25

            amp = abs(math.sin(phase)) * 0.45 + (0.20 if math.sin(phase * 2.0) > 0.6 else 0.0)
            emit(f"AMP:{amp:.2f}")

        is_music_playing = False
        current_music_proc = None
        emit("MUSIC_DONE")
        set_mode("STANDBY")
    except Exception as ex:
        emit(f"ERROR:PLAY_MUSIC:{ex}")
        is_music_playing = False
        emit("MUSIC_DONE")
        set_mode("STANDBY")

def speech_recognition_worker():
    try:
        import speech_recognition as sr
        try:
            mics = sr.Microphone.list_microphone_names()
            if not mics:
                return
        except Exception:
            return

        r = sr.Recognizer()
        r.energy_threshold = 280
        r.dynamic_energy_threshold = True

        with sr.Microphone() as source:
            r.adjust_for_ambient_noise(source, duration=0.8)

            while True:
                with mode_lock:
                    mode = current_mode
                    speaking = is_speaking or is_music_playing

                try:
                    if mode == "LISTENING":
                        audio = r.listen(source, timeout=7.0, phrase_time_limit=12.0)
                        try:
                            text = r.recognize_google(audio).strip()
                            if text:
                                if is_exit_phrase(text):
                                    emit("EXIT")
                                    continue
                                if STOP_REGEX.match(text):
                                    emit("TIMEOUT")
                                    set_mode("STANDBY")
                                    continue
                                emit(f"FINAL:{text}")
                                set_mode("STANDBY")
                        except sr.UnknownValueError:
                            emit("TIMEOUT")
                            set_mode("STANDBY")
                        except Exception:
                            emit("TIMEOUT")
                            set_mode("STANDBY")

                    elif mode == "SPEAKING" or speaking:
                        try:
                            audio = r.listen(source, timeout=0.6, phrase_time_limit=3.0)
                            try:
                                text = r.recognize_google(audio).strip().lower()
                                if text:
                                    if is_exit_phrase(text):
                                        stop_speech()
                                        stop_music()
                                        emit("EXIT")
                                        continue
                                    if any(w in text for w in ("stop", "quiet", "shut up", "pause", "cancel", "halt")):
                                        stop_speech()
                                        stop_music()
                                        emit(f"INTERRUPTED:{text}")
                                        set_mode("STANDBY")
                                        continue
                                    if any(w in text for w in ("sphere", "spear", "wake up")):
                                        stop_speech()
                                        stop_music()
                                        emit(f"INTERRUPTED:{text}")
                                        emit("WAKE")
                                        set_mode("LISTENING")
                                        continue
                            except Exception:
                                pass
                        except Exception:
                            pass

                    elif mode == "STANDBY":
                        audio = r.listen(source, timeout=None, phrase_time_limit=6.0)
                        try:
                            text = r.recognize_google(audio).strip()
                            if not text:
                                continue

                            if is_exit_phrase(text):
                                emit("EXIT")
                                continue

                            lower = text.lower()
                            if lower.startswith("play "):
                                emit("WAKE")
                                emit(f"FINAL:{text}")
                                set_mode("STANDBY")
                                continue

                            m = WAKE_REGEX.match(text)
                            if m:
                                q = m.group("q")
                                emit("WAKE")
                                if q and q.strip():
                                    emit(f"FINAL:{q.strip()}")
                                    set_mode("STANDBY")
                                else:
                                    set_mode("LISTENING")
                        except Exception:
                            pass

                    elif speaking:
                        time.sleep(0.3)

                except sr.WaitTimeoutError:
                    if mode == "LISTENING":
                        emit("TIMEOUT")
                        set_mode("STANDBY")
                except Exception:
                    time.sleep(0.5)

    except Exception:
        pass

def main():
    emit("STATUS:INITIALIZING")

    if len(sys.argv) > 1:
        try:
            parent_pid = int(sys.argv[1])
            def watchdog():
                while True:
                    time.sleep(0.5)
                    try:
                        os.kill(parent_pid, 0)
                    except OSError:
                        stop_music()
                        stop_speech()
                        os._exit(0)
            wt = threading.Thread(target=watchdog, daemon=True)
            wt.start()
        except Exception:
            pass

    stt_thread = threading.Thread(target=speech_recognition_worker, daemon=True)
    stt_thread.start()

    emit("STATUS:READY")
    emit("MODE:STANDBY")

    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue

        if line == "CMD:WAKE_STANDBY":
            stop_music()
            stop_speech()
            set_mode("STANDBY")

        elif line == "CMD:LISTEN_QUERY":
            stop_music()
            stop_speech()
            set_mode("LISTENING")

        elif line.startswith("CMD:SPEAK "):
            stop_music()
            stop_speech()
            text_to_speak = line[10:].strip()
            set_mode("SPEAKING")
            t = threading.Thread(target=speak_thread_worker, args=(text_to_speak, False), daemon=True)
            t.start()

        elif line.startswith("CMD:EXIT_SPEAK "):
            stop_music()
            stop_speech()
            text_to_speak = line[15:].strip()
            set_mode("SPEAKING")
            t = threading.Thread(target=speak_thread_worker, args=(text_to_speak, True), daemon=True)
            t.start()

        elif line == "CMD:STOP_SPEAK":
            stop_speech()
            set_mode("STANDBY")

        elif line.startswith("CMD:PLAY_MUSIC "):
            url = line[15:].strip()
            t = threading.Thread(target=play_music_worker, args=(url,), daemon=True)
            t.start()

        elif line == "CMD:STOP_MUSIC":
            stop_music()
            emit("MUSIC_DONE")
            set_mode("STANDBY")

        elif line == "CMD:EXIT":
            stop_music()
            stop_speech()
            sys.exit(0)

    stop_music()
    stop_speech()
    sys.exit(0)

if __name__ == "__main__":
    main()
