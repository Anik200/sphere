import sys
import speech_recognition as sr

def main():
    r = sr.Recognizer()
    r.dynamic_energy_threshold = True
    print("DAEMON_READY", flush=True)

    for line in sys.stdin:
        wav_path = line.strip()
        if not wav_path:
            continue
        if wav_path == "QUIT":
            break

        text = ""
        try:
            with sr.AudioFile(wav_path) as source:
                audio = r.record(source)
            text = r.recognize_google(audio).strip()
        except sr.UnknownValueError:
            text = ""
        except Exception as e:
            text = ""

        print(f"RESULT:{text}", flush=True)

if __name__ == "__main__":
    main()
