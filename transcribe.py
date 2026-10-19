import sys
import os
import locale
import speech_recognition as sr

def get_locale_language():
    try:
        loc = locale.getlocale()[0] or ""
    except Exception:
        loc = ""
    if not loc:
        try:
            loc = locale.getdefaultlocale()[0] or ""
        except Exception:
            loc = ""
    loc_upper = loc.upper()
    if "IN" in loc_upper:
        return "en-IN"
    if "GB" in loc_upper or "UK" in loc_upper:
        return "en-GB"
    if "AU" in loc_upper:
        return "en-AU"
    if "CA" in loc_upper:
        return "en-CA"
    return "en-US"

def transcribe_wav(wav_path):
    if not os.path.exists(wav_path) or os.path.getsize(wav_path) < 1000:
        return ""
    r = sr.Recognizer()
    r.dynamic_energy_threshold = False
    r.energy_threshold = 50

    try:
        with sr.AudioFile(wav_path) as source:
            audio = r.record(source)

        primary = get_locale_language()
        lang_order = [primary]
        for l in ["en-IN", "en-US", "en-GB"]:
            if l not in lang_order:
                lang_order.append(l)

        for lang in lang_order:
            try:
                text = r.recognize_google(audio, language=lang).strip()
                if text and len(text) > 0:
                    return text
            except sr.UnknownValueError:
                continue
            except Exception:
                break

        try:
            return r.recognize_google(audio).strip()
        except Exception:
            return ""
    except Exception:
        return ""

if __name__ == "__main__":
    if len(sys.argv) > 1:
        wav_file = sys.argv[1]
        out_file = sys.argv[2] if len(sys.argv) > 2 else "sphere_stt_result.txt"
        result = transcribe_wav(wav_file)
        with open(out_file, "w", encoding="utf-8") as f:
            f.write(result)
