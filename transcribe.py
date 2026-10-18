import sys
import speech_recognition as sr

def transcribe_wav(wav_path):
    r = sr.Recognizer()
    try:
        with sr.AudioFile(wav_path) as source:
            audio = r.record(source)
        text = r.recognize_google(audio)
        return text.strip()
    except Exception:
        return ""

if __name__ == "__main__":
    if len(sys.argv) > 1:
        wav_file = sys.argv[1]
        out_file = sys.argv[2] if len(sys.argv) > 2 else "sphere_stt_result.txt"
        result = transcribe_wav(wav_file)
        with open(out_file, "w", encoding="utf-8") as f:
            f.write(result)
