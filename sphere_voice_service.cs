using System;
using System.IO;
using System.Globalization;
using System.Speech.Recognition;
using System.Speech.Synthesis;
using System.Threading;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Runtime.InteropServices;

namespace SphereVoiceEngine {
    public enum ERole : uint {
        eConsole = 0,
        eMultimedia = 1,
        eCommunications = 2
    }

    [ComImport, Guid("F8679F50-850A-41CF-9C72-430F290290C8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IPolicyConfig {
        [PreserveSig] int GetMixFormat();
        [PreserveSig] int GetDeviceFormat();
        [PreserveSig] int ResetDeviceFormat();
        [PreserveSig] int SetDeviceFormat();
        [PreserveSig] int GetProcessingPeriod();
        [PreserveSig] int SetProcessingPeriod();
        [PreserveSig] int GetShareMode();
        [PreserveSig] int SetShareMode();
        [PreserveSig] int GetPropertyValue();
        [PreserveSig] int SetPropertyValue();
        [PreserveSig] int SetDefaultEndpoint([MarshalAs(UnmanagedType.LPWStr)] string wszDeviceId, ERole eRole);
        [PreserveSig] int SetEndpointVisibility();
    }

    [ComImport, Guid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")]
    internal class CPolicyConfigClientWin10 { }

    [ComImport, Guid("2A417537-8861-4603-B087-F82329DC375A")]
    internal class CPolicyConfigClientWin7 { }

    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    class MMDeviceEnumeratorLocal { }

    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection ppDevices);
        [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice ppDevice);
    }

    [Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceCollection {
        [PreserveSig] int GetCount(out int pcDevices);
        [PreserveSig] int Item(int nDevice, out IMMDevice ppDevice);
    }

    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr pActivationParams, [MarshalAs(UnmanagedType.IUnknown)] out object ppInterface);
        [PreserveSig] int OpenPropertyStore(int stgmAccess, out IPropertyStore ppProperties);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string ppstrId);
        [PreserveSig] int GetState(out int pdwState);
    }

    [Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IPropertyStore {
        [PreserveSig] int GetCount(out int cProps);
        [PreserveSig] int GetAt(int iProp, out PROPERTYKEY pkey);
        [PreserveSig] int GetValue(ref PROPERTYKEY key, out PROPVARIANT pv);
        [PreserveSig] int SetValue(ref PROPERTYKEY key, ref PROPVARIANT propvar);
        [PreserveSig] int Commit();
    }

    [Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioEndpointVolume {
        [PreserveSig] int RegisterControlChangeNotify(IntPtr pNotify);
        [PreserveSig] int UnregisterControlChangeNotify(IntPtr pNotify);
        [PreserveSig] int GetChannelCount(out int pnChannelCount);
        [PreserveSig] int SetMasterVolumeLevel(float fLevelDB, ref Guid pguidEventContext);
        [PreserveSig] int SetMasterVolumeLevelScalar(float fLevel, ref Guid pguidEventContext);
        [PreserveSig] int GetMasterVolumeLevel(out float pfLevelDB);
        [PreserveSig] int GetMasterVolumeLevelScalar(out float pfLevel);
        [PreserveSig] int SetChannelVolumeLevel(int nChannel, float fLevelDB, ref Guid pguidEventContext);
        [PreserveSig] int SetChannelVolumeLevelScalar(int nChannel, float fLevel, ref Guid pguidEventContext);
        [PreserveSig] int GetChannelVolumeLevel(int nChannel, out float pfLevelDB);
        [PreserveSig] int GetChannelVolumeLevelScalar(int nChannel, out float pfLevel);
        [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool bMute, ref Guid pguidEventContext);
        [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool pbMute);
    }

    [StructLayout(LayoutKind.Sequential)]
    struct PROPERTYKEY {
        public Guid fmtid;
        public int pid;
    }

    [StructLayout(LayoutKind.Explicit)]
    struct PROPVARIANT {
        [FieldOffset(0)] public short vt;
        [FieldOffset(8)] public IntPtr pwszVal;
    }

    static class AudioDeviceManager {
        static string GetDeviceName(IMMDevice dev) {
            try {
                IPropertyStore store;
                dev.OpenPropertyStore(0, out store);
                PROPERTYKEY key = new PROPERTYKEY { fmtid = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"), pid = 14 };
                PROPVARIANT pv;
                store.GetValue(ref key, out pv);
                return Marshal.PtrToStringUni(pv.pwszVal) ?? "Unknown";
            } catch { return "Unknown"; }
        }

        public static bool SetDefaultEndpoint(string deviceId) {
            try {
                var policy = (IPolicyConfig)new CPolicyConfigClientWin10();
                policy.SetDefaultEndpoint(deviceId, ERole.eConsole);
                policy.SetDefaultEndpoint(deviceId, ERole.eMultimedia);
                policy.SetDefaultEndpoint(deviceId, ERole.eCommunications);
                return true;
            } catch {
                try {
                    var policy = (IPolicyConfig)new CPolicyConfigClientWin7();
                    policy.SetDefaultEndpoint(deviceId, ERole.eConsole);
                    policy.SetDefaultEndpoint(deviceId, ERole.eMultimedia);
                    policy.SetDefaultEndpoint(deviceId, ERole.eCommunications);
                    return true;
                } catch {
                    return false;
                }
            }
        }

        public static string ConfigureBestMicrophone() {
            try {
                var enumerator = (IMMDeviceEnumerator)(new MMDeviceEnumeratorLocal());
                IMMDevice defaultDev = null;
                string defaultId = "";
                string defaultName = "";
                int hr = enumerator.GetDefaultAudioEndpoint(1, 0, out defaultDev); // eCapture=1, eConsole=0
                if (hr == 0 && defaultDev != null) {
                    defaultDev.GetId(out defaultId);
                    defaultName = GetDeviceName(defaultDev);
                }

                IMMDeviceCollection col;
                hr = enumerator.EnumAudioEndpoints(1, 1, out col); // eCapture=1, DEVICE_STATE_ACTIVE=1
                if (hr != 0 || col == null) return defaultName;

                int count;
                col.GetCount(out count);

                string[] dummyKeywords = new string[] {
                    "audiocular", "dac", "steam", "stereo mix", "stereo-mix",
                    "wave out", "what u hear", "cable", "droidcam", "wo mic",
                    "line in", "line-in", "virtual", "blackhole", "voicemeeter", "vb-audio"
                };

                string bestId = defaultId;
                string bestName = defaultName;
                int bestScore = -999;

                for (int i = 0; i < count; i++) {
                    IMMDevice dev;
                    col.Item(i, out dev);
                    if (dev == null) continue;

                    string id;
                    dev.GetId(out id);
                    string name = GetDeviceName(dev);
                    string lower = name.ToLowerInvariant();

                    int score = 0;
                    bool isDummy = false;
                    foreach (string d in dummyKeywords) {
                        if (lower.Contains(d)) {
                            isDummy = true;
                            break;
                        }
                    }

                    if (isDummy) {
                        score = -100;
                    } else {
                        if (lower.Contains("array")) score += 40;
                        if (lower.Contains("amd") || lower.Contains("realtek") || lower.Contains("intel") || lower.Contains("synaptics") || lower.Contains("conexant")) score += 30;
                        else if (lower.Contains("omen") || lower.Contains("webcam") || lower.Contains("camera")) score += 15;
                        if (lower.Contains("usb") || lower.Contains("yeti") || lower.Contains("fifine") || lower.Contains("hyperx") || lower.Contains("rode") || lower.Contains("shure")) score += 35;
                        if (lower.Contains("microphone") || lower.Contains("mic")) score += 10;
                        if (id == defaultId) score += 5;
                    }

                    if (score > bestScore) {
                        bestScore = score;
                        bestId = id;
                        bestName = name;
                    }
                }

                if (!string.IsNullOrEmpty(bestId) && bestId != defaultId && bestScore > 0) {
                    SetDefaultEndpoint(bestId);
                }

                // Ensure unmuted and master volume >= 95% on all physical/active mics
                for (int i = 0; i < count; i++) {
                    IMMDevice dev;
                    col.Item(i, out dev);
                    if (dev == null) continue;
                    string name = GetDeviceName(dev).ToLowerInvariant();
                    bool isDummy = false;
                    foreach (string d in dummyKeywords) {
                        if (name.Contains(d)) { isDummy = true; break; }
                    }
                    if (!isDummy) {
                        try {
                            Guid iidVol = new Guid("5CDF2C82-841E-4546-9722-0CF74078229A");
                            object oVol;
                            dev.Activate(ref iidVol, 1, IntPtr.Zero, out oVol);
                            if (oVol != null) {
                                var vol = (IAudioEndpointVolume)oVol;
                                bool isMuted;
                                vol.GetMute(out isMuted);
                                Guid g = Guid.Empty;
                                if (isMuted) {
                                    vol.SetMute(false, ref g);
                                }
                                float level;
                                vol.GetMasterVolumeLevelScalar(out level);
                                if (level < 0.85f) {
                                    vol.SetMasterVolumeLevelScalar(0.95f, ref g);
                                }
                            }
                        } catch { }
                    }
                }

                return bestName;
            } catch {
                return "Default";
            }
        }
    }

    enum ServiceMode {
        Standby,
        Listening,
        Speaking
    }

    class Program {
        static SpeechRecognitionEngine sre;
        static SpeechSynthesizer synth;
        static Grammar wakeGrammar;
        static Grammar wakeDictGrammar;
        static Grammar dictGrammar;
        static Grammar interruptGrammar;

        static ServiceMode currentMode = ServiceMode.Standby;
        static readonly object modeLock = new object();
        static DateTime lastSpeechTime = DateTime.UtcNow;
        static DateTime wakeStartTime = DateTime.UtcNow;
        static bool speechHeardInListening = false;
        static string lastHypothesizedText = "";
        static int lastAudioLevel = 0;
        static DateTime lastAmpSendTime = DateTime.UtcNow;
        static string currentSpeakingText = "";

        static dynamic mediaPlayer = null;
        static Process musicProcess = null;
        static string ffplayPath = null;
        static bool isMusicPlaying = false;
        static Thread musicMonitorThread = null;

        static string FindFfplay() {
            try {
                string pathEnv = Environment.GetEnvironmentVariable("PATH") ?? "";
                string[] paths = pathEnv.Split(';');
                foreach (string p in paths) {
                    if (string.IsNullOrWhiteSpace(p)) continue;
                    try {
                        string full = Path.Combine(p.Trim(), "ffplay.exe");
                        if (File.Exists(full)) return full;
                    } catch { }
                }
                string localApp = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                if (!string.IsNullOrEmpty(localApp)) {
                    string wingetDir = Path.Combine(localApp, "Microsoft", "WinGet", "Packages");
                    if (Directory.Exists(wingetDir)) {
                        string[] matches = Directory.GetFiles(wingetDir, "ffplay.exe", SearchOption.AllDirectories);
                        if (matches.Length > 0) return matches[0];
                    }
                }
            } catch { }
            return null;
        }

        static readonly Regex ExitWordRegex = new Regex(
            @"^(close\s+app|close\s+the\s+app|exit\s+app|exit\s+the\s+app|quit\s+app|shutdown\s+sphere|shut\s+down\s+sphere|terminate\s+sphere)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly Regex ExitWakeRegex = new Regex(
            @"^(hey\s+sphere|sphere|her\s+sphere|hi\s+sphere|hello\s+sphere)\s+(close\s+app|close\s+the\s+app|exit\s+app|exit\s+the\s+app|quit\s+app|shutdown\s+sphere|shut\s+down\s+sphere|terminate\s+sphere)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly Regex BargeInStopRegex = new Regex(
            @"^(hey\s+sphere\s+|sphere\s+|hi\s+sphere\s+|hello\s+sphere\s+|hey\s+spear\s+|spear\s+)?(stop|be\s+quiet|shut\s+up|pause|cancel|stop\s+talking|stop\s+it|stop\s+music|stop\s+the\s+music|stop\s+playing|pause\s+music|pause\s+song|stop\s+song|quiet|silence|enough|halt|skip|next)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly Regex BargeInWakeRegex = new Regex(
            @"^(hey\s+sphere|sphere|hi\s+sphere|hello\s+sphere|ok\s+sphere|okay\s+sphere|yo\s+sphere|hey\s+spear|spear|wake\s+up\s+sphere|wake\s+up|her\s+sphere|here\s+sphere|hey\s+fear|hey\s+spare|hay\s+sphere)(\s+(?<q>.*))?$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly Regex BargeInExitRegex = new Regex(
            @"^(hey\s+sphere\s+|sphere\s+)?(close\s+app|close\s+the\s+app|exit\s+app|exit\s+the\s+app|quit\s+app|shutdown\s+sphere|shut\s+down\s+sphere|terminate\s+sphere)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly System.Collections.Generic.List<byte> accumulatedListeningPcm = new System.Collections.Generic.List<byte>();
        static readonly object pcmLock = new object();
        static string accumulatedListeningText = "";

        static void WritePcmToWav(string filePath, byte[] pcmData, int sampleRate, short channels, short bitsPerSample) {
            try {
                using (FileStream fs = new FileStream(filePath, FileMode.Create, FileAccess.Write, FileShare.ReadWrite))
                using (BinaryWriter bw = new BinaryWriter(fs)) {
                    int byteRate = sampleRate * channels * (bitsPerSample / 8);
                    short blockAlign = (short)(channels * (bitsPerSample / 8));
                    int subChunk2Size = pcmData.Length;
                    int chunkSize = 36 + subChunk2Size;

                    bw.Write(System.Text.Encoding.ASCII.GetBytes("RIFF"));
                    bw.Write(chunkSize);
                    bw.Write(System.Text.Encoding.ASCII.GetBytes("WAVE"));

                    bw.Write(System.Text.Encoding.ASCII.GetBytes("fmt "));
                    bw.Write(16);
                    bw.Write((short)1); // PCM
                    bw.Write(channels);
                    bw.Write(sampleRate);
                    bw.Write(byteRate);
                    bw.Write(blockAlign);
                    bw.Write(bitsPerSample);

                    bw.Write(System.Text.Encoding.ASCII.GetBytes("data"));
                    bw.Write(subChunk2Size);
                    bw.Write(pcmData);
                }
            } catch { }
        }

        static void SaveAccumulatedAudio() {
            try {
                byte[] pcm;
                lock (pcmLock) {
                    pcm = accumulatedListeningPcm.ToArray();
                }
                string tempPath = Path.GetTempPath();
                string queryPath = Path.Combine(tempPath, "sphere_query.wav");
                string srePath = Path.Combine(tempPath, "sphere_sre_audio.wav");

                if (pcm != null && pcm.Length >= 1600) {
                    WritePcmToWav(queryPath, pcm, 16000, 1, 16);
                    try { File.Copy(queryPath, srePath, true); } catch { }
                }
            } catch { }
        }

        static void SaveAudioIfAvailable(RecognitionResult result) {
            if (result == null || result.Audio == null) return;
            try {
                using (MemoryStream ms = new MemoryStream()) {
                    result.Audio.WriteToWaveStream(ms);
                    byte[] wav = ms.ToArray();
                    if (wav.Length > 44) {
                        byte[] pcm = new byte[wav.Length - 44];
                        Array.Copy(wav, 44, pcm, 0, pcm.Length);
                        lock (pcmLock) {
                            accumulatedListeningPcm.AddRange(pcm);
                        }
                    }
                }
            } catch { }
        }

        static bool IsExitPhrase(string text) {
            if (string.IsNullOrEmpty(text)) return false;
            string clean = text.Trim().Trim('.', '?', '!', ',', ' ', '"', '\'');
            if (ExitWordRegex.IsMatch(clean)) return true;
            if (ExitWakeRegex.IsMatch(clean)) return true;
            return false;
        }

        static bool IsStopPhrase(string text) {
            if (string.IsNullOrEmpty(text)) return false;
            string lower = text.ToLowerInvariant().Trim().Trim('.', '?', '!', ',', ' ', '"', '\'');
            if (BargeInStopRegex.IsMatch(lower)) return true;
            string[] stopWords = new string[] {
                "stop", "be quiet", "shut up", "pause", "cancel", "quiet", "silence",
                "enough", "halt", "shush", "wait", "hold on", "stop talking", "stop music",
                "stop playing", "cut it out"
            };
            foreach (string w in stopWords) {
                int idx = lower.IndexOf(w);
                if (idx != -1) {
                    if (idx > 0 && char.IsLetterOrDigit(lower[idx - 1])) continue;
                    int end = idx + w.Length;
                    if (end < lower.Length && char.IsLetterOrDigit(lower[end])) continue;
                    return true;
                }
            }
            return false;
        }

        static void Main(string[] args) {
            Console.OutputEncoding = System.Text.Encoding.UTF8;
            Console.WriteLine("STATUS:INITIALIZING");
            Console.Out.Flush();

            if (args != null && args.Length > 0) {
                int parentPid;
                if (int.TryParse(args[0], out parentPid)) {
                    Thread watchdog = new Thread(() => {
                        try {
                            Process parent = Process.GetProcessById(parentPid);
                            parent.WaitForExit();
                        } catch { }
                        try { synth.SpeakAsyncCancelAll(); } catch { }
                        try { sre.RecognizeAsyncStop(); } catch { }
                        Environment.Exit(0);
                    });
                    watchdog.IsBackground = true;
                    watchdog.Start();
                }
            }

            try {
                synth = new SpeechSynthesizer();
                synth.Rate = 1;
                synth.SpeakProgress += Synth_SpeakProgress;
                synth.SpeakCompleted += Synth_SpeakCompleted;

                foreach (InstalledVoice v in synth.GetInstalledVoices()) {
                    if (v.Enabled) {
                        synth.SelectVoice(v.VoiceInfo.Name);
                        break;
                    }
                }

                RecognizerInfo recognizer = null;
                foreach (RecognizerInfo r in SpeechRecognitionEngine.InstalledRecognizers()) {
                    if (r.Culture.Name.Equals("en-US", StringComparison.OrdinalIgnoreCase)) {
                        recognizer = r;
                        break;
                    }
                }
                if (recognizer == null) {
                    foreach (RecognizerInfo r in SpeechRecognitionEngine.InstalledRecognizers()) {
                        if (r.Culture.Name.StartsWith("en", StringComparison.OrdinalIgnoreCase)) {
                            recognizer = r;
                            break;
                        }
                    }
                }
                if (recognizer == null && SpeechRecognitionEngine.InstalledRecognizers().Count > 0) {
                    recognizer = SpeechRecognitionEngine.InstalledRecognizers()[0];
                }

                if (recognizer != null) {
                    sre = new SpeechRecognitionEngine(recognizer.Id);
                } else {
                    sre = new SpeechRecognitionEngine();
                }

                CultureInfo recCulture = sre.RecognizerInfo.Culture;

                string activeMic = AudioDeviceManager.ConfigureBestMicrophone();
                Console.WriteLine("AUDIO_DEVICE:" + activeMic);
                Console.Out.Flush();

                bool inputConfigured = false;
                for (int attempt = 0; attempt < 3; attempt++) {
                    try {
                        sre.SetInputToDefaultAudioDevice();
                        inputConfigured = true;
                        break;
                    } catch {
                        Thread.Sleep(250);
                    }
                }
                if (!inputConfigured) {
                    Console.WriteLine("WARNING:DEFAULT_AUDIO_DEVICE_UNAVAILABLE");
                }

                try {
                    sre.EndSilenceTimeout = TimeSpan.FromMilliseconds(200);
                    sre.EndSilenceTimeoutAmbiguous = TimeSpan.FromMilliseconds(250);
                    sre.InitialSilenceTimeout = TimeSpan.FromSeconds(5.0);
                } catch { }

                Choices wakeChoices = new Choices(new string[] {
                    "hey sphere", "sphere", "hi sphere", "hello sphere", "ok sphere", "okay sphere", "yo sphere",
                    "hey spear", "spear", "hi spear", "hello spear", "ok spear", "okay spear",
                    "hey sfeer", "sfeer", "hey sfear", "sfear",
                    "hey square", "square", "hey sphear", "hey severe", "severe", "hey steer", "steer", "hey spire", "spire", "hey spir",
                    "hey fear", "hi fear", "hello fear", "her sphere", "here sphere", "hear sphere",
                    "hey spare", "the sphere", "a sphere", "hay sphere", "ay sphere", "hey siri", "hey sir",
                    "hey peer", "peer", "hey pier", "pier", "hey beer", "hey sere", "hey speed", "hey spirit", "spirit", "hey dear", "hey clear",
                    "wake up", "wake up sphere",
                    "stop", "be quiet", "shut up", "pause", "cancel",
                    "close app", "close the app", "exit app", "exit the app", "quit app",
                    "hey sphere exit app", "sphere exit app", "hey sphere close app", "sphere close app",
                    "play", "play song", "play music", "hey sphere play", "sphere play"
                });
                GrammarBuilder wakeGb = new GrammarBuilder(wakeChoices);
                wakeGb.Culture = recCulture;
                wakeGrammar = new Grammar(wakeGb);
                wakeGrammar.Name = "WakeGrammar";
                wakeGrammar.Weight = 1.0f;
                try { sre.LoadGrammar(wakeGrammar); } catch { }

                Choices wakePrefixChoices = new Choices(new string[] {
                    "hey sphere", "sphere", "hi sphere", "hello sphere", "ok sphere", "okay sphere", "yo sphere",
                    "hey spear", "spear", "hi spear", "hello spear", "ok spear", "okay spear",
                    "hey sfeer", "sfeer", "hey sfear", "sfear",
                    "hey square", "square", "hey sphear", "hey severe", "severe", "hey steer", "steer", "hey spire", "spire",
                    "her sphere", "here sphere", "hey fear", "wake up sphere", "wake up",
                    "hey spare", "hay sphere", "ay sphere", "hey siri",
                    "hey peer", "peer", "hey pier", "pier", "hey beer", "hey sere", "hey speed", "hey spirit", "spirit", "hey dear", "hey clear",
                    "hey sphere play", "sphere play", "play"
                });
                GrammarBuilder wakeDictGb = new GrammarBuilder();
                wakeDictGb.Culture = recCulture;
                wakeDictGb.Append(wakePrefixChoices);
                wakeDictGb.AppendDictation();
                wakeDictGrammar = new Grammar(wakeDictGb);
                wakeDictGrammar.Name = "WakeDictGrammar";
                wakeDictGrammar.Weight = 0.5f;
                try { sre.LoadGrammar(wakeDictGrammar); } catch { }
                wakeDictGrammar.Enabled = false;

                dictGrammar = new DictationGrammar();
                dictGrammar.Name = "DictationGrammar";
                try { sre.LoadGrammar(dictGrammar); } catch { }
                dictGrammar.Enabled = false;

                Choices interruptChoices = new Choices(new string[] {
                    "stop", "stop it", "stop music", "stop the music", "stop song", "stop playing", "stop talking",
                    "pause", "pause music", "pause the music", "pause song",
                    "be quiet", "quiet", "shut up", "silence", "enough", "cancel", "halt", "skip", "next",
                    "hey sphere", "sphere", "hi sphere", "hello sphere", "ok sphere", "okay sphere", "yo sphere",
                    "hey spear", "spear", "hi spear", "hello spear", "ok spear", "okay spear",
                    "hey fear", "hi fear", "her sphere", "here sphere", "wake up sphere", "wake up",
                    "hey sphere stop", "sphere stop", "hey sphere shut up", "sphere shut up",
                    "hey sphere pause", "sphere pause", "hey sphere be quiet", "sphere be quiet",
                    "hey sphere quiet", "sphere quiet", "hey sphere cancel", "sphere cancel",
                    "hey sphere stop music", "sphere stop music", "hey sphere pause music", "sphere pause music",
                    "exit", "quit", "goodbye", "good bye", "bye", "bye bye", "see you later",
                    "close app", "close the app", "exit app", "exit the app", "quit app", "shut down", "shutdown",
                    "hey sphere exit", "sphere exit", "hey sphere quit", "sphere quit", "hey sphere goodbye"
                });
                GrammarBuilder intGb = new GrammarBuilder(interruptChoices);
                intGb.Culture = recCulture;
                interruptGrammar = new Grammar(intGb);
                interruptGrammar.Name = "InterruptGrammar";
                try { sre.LoadGrammar(interruptGrammar); } catch { }
                interruptGrammar.Enabled = false;

                sre.SpeechHypothesized += Sre_SpeechHypothesized;
                sre.SpeechRecognized += Sre_SpeechRecognized;
                sre.SpeechRecognitionRejected += Sre_SpeechRecognitionRejected;
                sre.AudioLevelUpdated += Sre_AudioLevelUpdated;
                sre.SpeechDetected += Sre_SpeechDetected;

                SetMode(ServiceMode.Standby);
                sre.RecognizeAsync(RecognizeMode.Multiple);

                Console.WriteLine("STATUS:READY");
                Console.Out.Flush();

                Thread cmdThread = new Thread(CommandLoop);
                cmdThread.IsBackground = true;
                cmdThread.Start();

                while (true) {
                    Thread.Sleep(80);
                    lock (modeLock) {
                        if (currentMode == ServiceMode.Listening) {
                            TimeSpan elapsed = DateTime.UtcNow - wakeStartTime;
                            TimeSpan silence = DateTime.UtcNow - lastSpeechTime;

                            string bestText = !string.IsNullOrWhiteSpace(accumulatedListeningText) ? accumulatedListeningText : lastHypothesizedText;
                            bestText = StripWakePrefix(bestText);

                            if (speechHeardInListening && silence.TotalMilliseconds >= 1800 && !string.IsNullOrWhiteSpace(bestText) && !IsWakeOnlyWord(bestText) && bestText.Length >= 2) {
                                SaveAccumulatedAudio();
                                Console.WriteLine("FINAL:" + bestText);
                                Console.Out.Flush();
                                accumulatedListeningText = "";
                                lastHypothesizedText = "";
                                SetMode(ServiceMode.Standby);
                            } else if (elapsed.TotalSeconds > 8.5) {
                                if (speechHeardInListening && !string.IsNullOrWhiteSpace(bestText) && !IsWakeOnlyWord(bestText) && bestText.Length >= 2) {
                                    SaveAccumulatedAudio();
                                    Console.WriteLine("FINAL:" + bestText);
                                    Console.Out.Flush();
                                } else {
                                    Console.WriteLine("TIMEOUT");
                                    Console.Out.Flush();
                                }
                                accumulatedListeningText = "";
                                lastHypothesizedText = "";
                                SetMode(ServiceMode.Standby);
                            }
                        }
                    }
                }
            } catch (Exception ex) {
                Console.WriteLine("ERROR:" + ex.Message);
                Console.Out.Flush();
            }
        }

        static bool IsWakeOnlyWord(string text) {
            if (string.IsNullOrWhiteSpace(text)) return true;
            string clean = text.Trim().ToLowerInvariant().Trim('.', '?', '!', ',', ' ', '"', '\'', '-', ':');
            string[] wakes = new string[] {
                "hey sphere", "sphere", "hi sphere", "hello sphere", "ok sphere", "okay sphere", "yo sphere",
                "hey spear", "spear", "hi spear", "hello spear", "ok spear", "okay spear",
                "hey sfeer", "sfeer", "hey sfear", "sfear",
                "hey square", "square", "hey sphear", "hey severe", "severe", "hey steer", "steer", "hey spire", "spire", "hey spir",
                "hey fear", "hi fear", "hello fear", "her sphere", "here sphere", "hear sphere",
                "hey spare", "the sphere", "a sphere", "my sphere", "hay sphere", "ay sphere", "hey siri", "hey sir",
                "hey peer", "peer", "hey pier", "pier", "hey beer", "hey sere", "hey speed", "hey spirit", "spirit", "hey dear", "hey clear",
                "wake up", "wake up sphere"
            };
            foreach (string w in wakes) {
                if (clean == w) return true;
            }
            return clean.Length <= 1;
        }

        static string StripWakePrefix(string text) {
            if (string.IsNullOrEmpty(text)) return "";
            string clean = text.Trim();
            string lower = clean.ToLowerInvariant();
            string[] prefixes = new string[] {
                "hey, sphere", "hey sphere", "hi, sphere", "hi sphere", "hello, sphere", "hello sphere",
                "ok, sphere", "ok sphere", "okay, sphere", "okay sphere", "yo, sphere", "yo sphere",
                "hey, spear", "hey spear", "hi, spear", "hi spear", "hello, spear", "hello sphere",
                "ok spear", "okay spear", "wake up sphere", "wake up", "her sphere", "here sphere", "hear sphere",
                "hey sfeer", "sfeer", "hey sfear", "sfear",
                "hey square", "square", "hey sphear", "hey severe", "severe", "hey steer", "steer", "hey spire", "spire",
                "hey fear", "hi fear", "hello fear", "hey spare", "the sphere", "a sphere", "my sphere",
                "hay sphere", "ay sphere", "hey siri", "hey sir",
                "hey peer", "peer", "hey pier", "pier", "hey beer", "hey sere", "hey speed", "hey spirit", "spirit", "hey dear", "hey clear",
                "sphere", "spear"
            };
            foreach (string p in prefixes) {
                if (lower == p) {
                    return "";
                }
                if (lower.StartsWith(p)) {
                    if (lower.Length == p.Length || !char.IsLetterOrDigit(lower[p.Length])) {
                        string rem = clean.Substring(p.Length).Trim();
                        while (rem.Length > 0 && (rem[0] == ',' || rem[0] == ':' || rem[0] == '.' || rem[0] == '!' || rem[0] == '?' || rem[0] == ' ' || rem[0] == '-' || rem[0] == '"' || rem[0] == '\'')) {
                            rem = rem.Substring(1).Trim();
                        }
                        return rem;
                    }
                }
            }
            return clean;
        }

        static bool IsWakePhrase(string text, out string remainingQuery) {
            remainingQuery = "";
            if (string.IsNullOrEmpty(text)) return false;
            string lower = text.ToLowerInvariant().Trim();

            string[] sphereKeywords = new string[] {
                "hey, sphere play", "hey sphere play", "sphere play",
                "hey, sphere", "hey sphere", "hi, sphere", "hi sphere", "hello, sphere", "hello sphere",
                "ok, sphere", "ok sphere", "okay, sphere", "okay sphere", "yo, sphere", "yo sphere",
                "her sphere", "here sphere", "hear sphere",
                "hey, spear", "hey spear", "hi, spear", "hi spear", "hello, spear", "hello sphere",
                "ok spear", "okay spear", "spear",
                "hey sfeer", "sfeer", "hey sfear", "sfear",
                "hey square", "square", "hey sphear", "hey severe", "severe", "hey steer", "steer", "hey spire", "spire", "hey spir",
                "hey fear", "hi fear", "hello fear", "hey spare", "the sphere", "a sphere", "my sphere",
                "hay sphere", "ay sphere", "hey siri", "hey sir", "wake up sphere", "wake up", "sphere",
                "hey peer", "peer", "hey pier", "pier", "hey beer", "hey beer sphere",
                "hey sere", "hey speed", "hey spirit", "spirit", "hey dear", "hey clear", "hey there sphere"
            };

            foreach (string kw in sphereKeywords) {
                int idx = lower.IndexOf(kw);
                if (idx != -1) {
                    if (idx > 0 && char.IsLetterOrDigit(lower[idx - 1])) continue;
                    int queryStart = idx + kw.Length;
                    if (queryStart < lower.Length && char.IsLetterOrDigit(lower[queryStart])) continue;

                    if (queryStart < text.Length) {
                        remainingQuery = text.Substring(queryStart).Trim();
                        while (remainingQuery.Length > 0 && (remainingQuery[0] == ',' || remainingQuery[0] == '.' || remainingQuery[0] == '?' || remainingQuery[0] == ' ' || remainingQuery[0] == '!' || remainingQuery[0] == ':')) {
                            remainingQuery = remainingQuery.Substring(1).Trim();
                        }
                    }
                    if (kw.Contains("play") && !remainingQuery.ToLowerInvariant().StartsWith("play")) {
                        remainingQuery = "play " + remainingQuery;
                    }
                    return true;
                }
            }
            return false;
        }

        static void PlayMusic(string url) {
            try {
                StopMusic();
                try { synth.SpeakAsyncCancelAll(); } catch { }

                if (string.IsNullOrEmpty(ffplayPath)) {
                    ffplayPath = FindFfplay();
                }

                if (!string.IsNullOrEmpty(ffplayPath) && File.Exists(ffplayPath)) {
                    ProcessStartInfo psi = new ProcessStartInfo {
                        FileName = ffplayPath,
                        Arguments = "-nodisp -autoexit -loglevel quiet -volume 85 \"" + url + "\"",
                        UseShellExecute = false,
                        CreateNoWindow = true,
                        WindowStyle = ProcessWindowStyle.Hidden
                    };
                    musicProcess = Process.Start(psi);
                    isMusicPlaying = true;
                    SetMode(ServiceMode.Speaking);

                    if (musicMonitorThread == null || !musicMonitorThread.IsAlive) {
                        musicMonitorThread = new Thread(() => {
                            while (isMusicPlaying) {
                                Thread.Sleep(80);
                                if (musicProcess != null) {
                                    if (musicProcess.HasExited) {
                                        isMusicPlaying = false;
                                        Console.WriteLine("MUSIC_DONE");
                                        Console.Out.Flush();
                                        SetMode(ServiceMode.Standby);
                                        break;
                                    }
                                }
                            }
                        });
                        musicMonitorThread.IsBackground = true;
                        musicMonitorThread.Start();
                    }
                    Console.WriteLine("MUSIC_STARTED");
                    Console.Out.Flush();
                    return;
                }

                Type wmpType = Type.GetTypeFromProgID("WMPlayer.OCX");
                if (wmpType == null) {
                    Console.WriteLine("ERROR:NO_PLAYER_FOUND");
                    Console.WriteLine("MUSIC_DONE");
                    Console.Out.Flush();
                    return;
                }
                mediaPlayer = Activator.CreateInstance(wmpType);
                mediaPlayer.settings.volume = 75;
                mediaPlayer.URL = url;
                mediaPlayer.controls.play();
                isMusicPlaying = true;
                SetMode(ServiceMode.Speaking);

                if (musicMonitorThread == null || !musicMonitorThread.IsAlive) {
                    musicMonitorThread = new Thread(() => {
                        bool hasStarted = false;
                        int stoppedCount = 0;
                        while (isMusicPlaying) {
                            Thread.Sleep(80);
                            try {
                                if (mediaPlayer != null) {
                                    int state = (int)mediaPlayer.playState;

                                    if (state == 3 || state == 6 || state == 9 || state == 10) {
                                        hasStarted = true;
                                        stoppedCount = 0;
                                    } else if (state == 8) {
                                        isMusicPlaying = false;
                                        Console.WriteLine("MUSIC_DONE");
                                        Console.Out.Flush();
                                        SetMode(ServiceMode.Standby);
                                        break;
                                    } else if (hasStarted && state == 1) {
                                        stoppedCount++;
                                        if (stoppedCount >= 4) {
                                            isMusicPlaying = false;
                                            Console.WriteLine("MUSIC_DONE");
                                            Console.Out.Flush();
                                            SetMode(ServiceMode.Standby);
                                            break;
                                        }
                                    } else {
                                        stoppedCount = 0;
                                    }
                                }
                            } catch {
                                break;
                            }
                        }
                    });
                    musicMonitorThread.IsBackground = true;
                    musicMonitorThread.Start();
                }
                Console.WriteLine("MUSIC_STARTED");
                Console.Out.Flush();
            } catch (Exception ex) {
                Console.WriteLine("ERROR:PLAY_MUSIC:" + ex.Message);
                Console.WriteLine("MUSIC_DONE");
                Console.Out.Flush();
                isMusicPlaying = false;
            }
        }

        static void StopMusic() {
            isMusicPlaying = false;
            if (musicProcess != null) {
                try {
                    if (!musicProcess.HasExited) {
                        musicProcess.Kill();
                        musicProcess.WaitForExit(300);
                    }
                } catch { }
                musicProcess = null;
            }
            if (mediaPlayer != null) {
                try {
                    mediaPlayer.controls.stop();
                    mediaPlayer.close();
                } catch { }
                mediaPlayer = null;
            }
        }

        static void SetMode(ServiceMode newMode) {
            lock (modeLock) {
                currentMode = newMode;
                wakeStartTime = DateTime.UtcNow;
                lastSpeechTime = DateTime.UtcNow;
                speechHeardInListening = false;
                lastHypothesizedText = "";

                if (newMode == ServiceMode.Standby) {
                    try {
                        sre.EndSilenceTimeout = TimeSpan.FromMilliseconds(200);
                        sre.EndSilenceTimeoutAmbiguous = TimeSpan.FromMilliseconds(250);
                        sre.InitialSilenceTimeout = TimeSpan.FromSeconds(5.0);
                    } catch { }
                    wakeGrammar.Enabled = true;
                    wakeDictGrammar.Enabled = false;
                    dictGrammar.Enabled = false;
                    interruptGrammar.Enabled = false;
                    Console.WriteLine("MODE:STANDBY");
                    Console.Out.Flush();
                } else if (newMode == ServiceMode.Listening) {
                    try {
                        sre.EndSilenceTimeout = TimeSpan.FromMilliseconds(1200);
                        sre.EndSilenceTimeoutAmbiguous = TimeSpan.FromMilliseconds(1500);
                        sre.InitialSilenceTimeout = TimeSpan.FromSeconds(8.0);
                    } catch { }
                    wakeGrammar.Enabled = false;
                    wakeDictGrammar.Enabled = false;
                    dictGrammar.Enabled = true;
                    interruptGrammar.Enabled = true;
                    accumulatedListeningText = "";
                    lock (pcmLock) {
                        accumulatedListeningPcm.Clear();
                    }
                    Console.WriteLine("MODE:LISTENING");
                    Console.Out.Flush();
                } else if (newMode == ServiceMode.Speaking) {
                    try {
                        sre.EndSilenceTimeout = TimeSpan.FromMilliseconds(250);
                        sre.EndSilenceTimeoutAmbiguous = TimeSpan.FromMilliseconds(300);
                    } catch { }
                    wakeGrammar.Enabled = true;
                    wakeDictGrammar.Enabled = false;
                    dictGrammar.Enabled = false;
                    interruptGrammar.Enabled = true;
                    Console.WriteLine("MODE:SPEAKING");
                    Console.Out.Flush();
                }
            }
        }

        static void CommandLoop() {
            string line;
            while ((line = Console.ReadLine()) != null) {
                line = line.Trim();
                if (string.IsNullOrEmpty(line)) continue;

                if (line.Equals("CMD:CHECK_AUDIO", StringComparison.OrdinalIgnoreCase)) {
                    string mic = AudioDeviceManager.ConfigureBestMicrophone();
                    Console.WriteLine("AUDIO_DEVICE:" + mic);
                    Console.Out.Flush();
                } else if (line.Equals("CMD:WAKE_STANDBY", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    try { synth.SpeakAsyncCancelAll(); } catch { }
                    SetMode(ServiceMode.Standby);
                } else if (line.Equals("CMD:LISTEN_QUERY", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    try { synth.SpeakAsyncCancelAll(); } catch { }
                    SetMode(ServiceMode.Listening);
                } else if (line.StartsWith("CMD:SPEAK ", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    string textToSpeak = line.Substring(10).Trim();
                    currentSpeakingText = textToSpeak;
                    SetMode(ServiceMode.Speaking);
                    try {
                        synth.SpeakAsyncCancelAll();
                        synth.SpeakAsync(textToSpeak);
                    } catch (Exception ex) {
                        Console.WriteLine("ERROR:SPEAK:" + ex.Message);
                        Console.Out.Flush();
                        SetMode(ServiceMode.Standby);
                    }
                } else if (line.StartsWith("CMD:EXIT_SPEAK ", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    try { sre.RecognizeAsyncCancel(); } catch { }
                    try { sre.RecognizeAsyncStop(); } catch { }
                    string textToSpeak = line.Substring(15).Trim();
                    currentSpeakingText = textToSpeak;
                    SetMode(ServiceMode.Speaking);
                    try {
                        synth.SpeakAsyncCancelAll();
                        synth.SpeakAsync(textToSpeak);
                    } catch (Exception ex) {
                        Console.WriteLine("ERROR:EXIT_SPEAK:" + ex.Message);
                        Console.WriteLine("SPEAK_DONE");
                        Console.Out.Flush();
                        SetMode(ServiceMode.Standby);
                    }
                } else if (line.Equals("CMD:STOP_SPEAK", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    try { synth.SpeakAsyncCancelAll(); } catch { }
                    Console.WriteLine("SPEAK_DONE");
                    Console.Out.Flush();
                    SetMode(ServiceMode.Standby);
                } else if (line.StartsWith("CMD:PLAY_MUSIC ", StringComparison.OrdinalIgnoreCase)) {
                    string musicUrl = line.Substring(15).Trim();
                    PlayMusic(musicUrl);
                } else if (line.Equals("CMD:STOP_MUSIC", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    Console.WriteLine("MUSIC_DONE");
                    Console.Out.Flush();
                    SetMode(ServiceMode.Standby);
                } else if (line.Equals("CMD:EXIT", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    try { synth.SpeakAsyncCancelAll(); } catch { }
                    try { sre.RecognizeAsyncStop(); } catch { }
                    Environment.Exit(0);
                }
            }

            StopMusic();
            try { synth.SpeakAsyncCancelAll(); } catch { }
            try { sre.RecognizeAsyncStop(); } catch { }
            Environment.Exit(0);
        }

        static void Sre_SpeechDetected(object sender, SpeechDetectedEventArgs e) {
            lock (modeLock) {
                speechHeardInListening = true;
                lastSpeechTime = DateTime.UtcNow;
            }
        }

        static void Sre_AudioLevelUpdated(object sender, AudioLevelUpdatedEventArgs e) {
            int level = e.AudioLevel;
            DateTime now = DateTime.UtcNow;
            bool changed = Math.Abs(level - lastAudioLevel) >= 1;
            bool periodic = (now - lastAmpSendTime).TotalMilliseconds >= 50;
            if (changed || (level > 0 && periodic) || (lastAudioLevel > 0 && level == 0)) {
                lastAudioLevel = level;
                lastAmpSendTime = now;
                float normAmp = Math.Min(1.0f, level / 40.0f);
                Console.WriteLine("AMP:" + normAmp.ToString("0.00", CultureInfo.InvariantCulture));
                Console.Out.Flush();
            }
        }

        static void Sre_SpeechHypothesized(object sender, SpeechHypothesizedEventArgs e) {
            if (e.Result == null || string.IsNullOrEmpty(e.Result.Text)) return;
            string hypText = e.Result.Text.Trim();

            lock (modeLock) {
                if (currentMode == ServiceMode.Speaking || isMusicPlaying) {
                    bool isHypExit = IsExitPhrase(hypText) || BargeInExitRegex.IsMatch(hypText);
                    bool isHypStop = IsStopPhrase(hypText);
                    string hypWakeQuery = null;
                    bool isHypWake = BargeInWakeRegex.IsMatch(hypText) || IsWakePhrase(hypText, out hypWakeQuery);

                    if (isHypStop || isHypExit || isHypWake) {
                        try { synth.SpeakAsyncCancelAll(); } catch { }
                        StopMusic();

                        if (isHypExit) {
                            Console.WriteLine("EXIT");
                            Console.Out.Flush();
                            return;
                        }

                        if (isHypStop) {
                            Console.WriteLine("INTERRUPTED:" + hypText);
                            Console.Out.Flush();
                            SetMode(ServiceMode.Standby);
                            return;
                        }

                        if (isHypWake) {
                            Console.WriteLine("INTERRUPTED:" + hypText);
                            Console.WriteLine("WAKE");
                            if (!string.IsNullOrEmpty(hypWakeQuery)) {
                                Console.WriteLine("FINAL:" + hypWakeQuery);
                                SetMode(ServiceMode.Standby);
                            } else {
                                SetMode(ServiceMode.Listening);
                            }
                            Console.Out.Flush();
                            return;
                        }
                    }
                    return;
                }

                if (currentMode == ServiceMode.Listening) {
                    speechHeardInListening = true;
                    lastSpeechTime = DateTime.UtcNow;

                    hypText = StripWakePrefix(hypText);
                    if (!string.IsNullOrWhiteSpace(hypText) && !IsWakeOnlyWord(hypText)) {
                        hypText = Regex.Replace(hypText, @"\bcomputer mouth\b", "computer mouse", RegexOptions.IgnoreCase);
                        hypText = Regex.Replace(hypText, @"\ba mouth look\b", "a mouse look", RegexOptions.IgnoreCase);
                        hypText = Regex.Replace(hypText, @"\bthe mouth look\b", "the mouse look", RegexOptions.IgnoreCase);
                        hypText = Regex.Replace(hypText, @"\bwhat a mouth\b", "what a mouse", RegexOptions.IgnoreCase);
                        hypText = Regex.Replace(hypText, @"\bwhat does a mouth\b", "what does a mouse", RegexOptions.IgnoreCase);
                        hypText = Regex.Replace(hypText, @"\blook likes\b", "look like", RegexOptions.IgnoreCase);

                        if (hypText.Length > 0) {
                            lastHypothesizedText = hypText;
                            Console.WriteLine("HYP:" + hypText);
                            Console.Out.Flush();
                        }
                    }
                } else if (currentMode == ServiceMode.Standby) {
                    string hypWakeQuery = null;
                    if (IsWakePhrase(hypText, out hypWakeQuery)) {
                        Console.WriteLine("WAKE");
                        hypWakeQuery = StripWakePrefix(hypWakeQuery);
                        if (!string.IsNullOrWhiteSpace(hypWakeQuery) && !IsWakeOnlyWord(hypWakeQuery) && hypWakeQuery.Length >= 2) {
                            Console.WriteLine("FINAL:" + hypWakeQuery);
                            SetMode(ServiceMode.Standby);
                        } else {
                            SetMode(ServiceMode.Listening);
                        }
                        Console.Out.Flush();
                        return;
                    }
                }
            }
        }

        static void Sre_SpeechRecognized(object sender, SpeechRecognizedEventArgs e) {
            if (e.Result == null || string.IsNullOrEmpty(e.Result.Text)) return;
            string text = e.Result.Text.Trim();

            lock (modeLock) {
                if (currentMode == ServiceMode.Speaking || isMusicPlaying) {
                    bool isExit = IsExitPhrase(text) || BargeInExitRegex.IsMatch(text);
                    bool isStop = IsStopPhrase(text);
                    string wakeQuery = null;
                    bool isWake = BargeInWakeRegex.IsMatch(text) || IsWakePhrase(text, out wakeQuery);

                    if (!isExit && !isWake && !isStop) {
                        return;
                    }

                    try { synth.SpeakAsyncCancelAll(); } catch { }
                    StopMusic();

                    if (isExit) {
                        Console.WriteLine("EXIT");
                        Console.Out.Flush();
                        return;
                    }

                    if (isStop) {
                        Console.WriteLine("INTERRUPTED:" + text);
                        Console.Out.Flush();
                        SetMode(ServiceMode.Standby);
                        return;
                    }

                    if (isWake) {
                        Console.WriteLine("INTERRUPTED:" + text);
                        Console.WriteLine("WAKE");
                        if (!string.IsNullOrEmpty(wakeQuery)) {
                            Console.WriteLine("FINAL:" + wakeQuery);
                            SetMode(ServiceMode.Standby);
                        } else {
                            SetMode(ServiceMode.Listening);
                        }
                        Console.Out.Flush();
                        return;
                    }
                    return;
                }

                if (currentMode == ServiceMode.Standby) {
                    if (e.Result.Confidence < 0.15f) {
                        return;
                    }

                    SaveAudioIfAvailable(e.Result);

                    if (IsExitPhrase(text)) {
                        Console.WriteLine("EXIT");
                        Console.Out.Flush();
                        return;
                    }

                    string lowerText = text.ToLowerInvariant().Trim();
                    if (lowerText.StartsWith("play ") || lowerText.StartsWith("play the song ") || lowerText.StartsWith("play song ")) {
                        Console.WriteLine("WAKE");
                        Console.WriteLine("FINAL:" + text);
                        SetMode(ServiceMode.Standby);
                        Console.Out.Flush();
                        return;
                    }

                    string query;
                    if (IsWakePhrase(text, out query)) {
                        if (IsExitPhrase(query)) {
                            Console.WriteLine("EXIT");
                            Console.Out.Flush();
                            return;
                        }
                        Console.WriteLine("WAKE");
                        query = StripWakePrefix(query);
                        if (!string.IsNullOrWhiteSpace(query) && !IsWakeOnlyWord(query) && query.Length >= 2) {
                            Console.WriteLine("FINAL:" + query);
                            SetMode(ServiceMode.Standby);
                        } else {
                            SetMode(ServiceMode.Listening);
                        }
                        Console.Out.Flush();
                        return;
                    }
                    return;
                }

                if (currentMode == ServiceMode.Listening) {
                    SaveAudioIfAvailable(e.Result);
                    if (IsExitPhrase(text)) {
                        Console.WriteLine("EXIT");
                        Console.Out.Flush();
                        return;
                    }

                    if (BargeInStopRegex.IsMatch(text)) {
                        Console.WriteLine("TIMEOUT");
                        Console.Out.Flush();
                        SetMode(ServiceMode.Standby);
                        return;
                    }

                    string query;
                    if (IsWakePhrase(text, out query)) {
                        if (IsExitPhrase(query)) {
                            Console.WriteLine("EXIT");
                            Console.Out.Flush();
                            return;
                        }
                        if (!string.IsNullOrEmpty(query)) {
                            text = query;
                        } else {
                            wakeStartTime = DateTime.UtcNow;
                            lastSpeechTime = DateTime.UtcNow;
                            return;
                        }
                    }

                    text = StripWakePrefix(text);
                    if (string.IsNullOrWhiteSpace(text) || IsWakeOnlyWord(text) || text.Length < 2) {
                        wakeStartTime = DateTime.UtcNow;
                        lastSpeechTime = DateTime.UtcNow;
                        return;
                    }

                    text = Regex.Replace(text, @"\bcomputer mouth\b", "computer mouse", RegexOptions.IgnoreCase);
                    text = Regex.Replace(text, @"\ba mouth look\b", "a mouse look", RegexOptions.IgnoreCase);
                    text = Regex.Replace(text, @"\bthe mouth look\b", "the mouse look", RegexOptions.IgnoreCase);
                    text = Regex.Replace(text, @"\bwhat a mouth\b", "what a mouse", RegexOptions.IgnoreCase);
                    text = Regex.Replace(text, @"\bwhat does a mouth\b", "what does a mouse", RegexOptions.IgnoreCase);

                    accumulatedListeningText = (accumulatedListeningText + " " + text).Trim();
                    lastSpeechTime = DateTime.UtcNow;
                    speechHeardInListening = true;
                }
            }
        }

        static void Sre_SpeechRecognitionRejected(object sender, SpeechRecognitionRejectedEventArgs e) {
            if (e.Result == null || string.IsNullOrEmpty(e.Result.Text)) return;
            lock (modeLock) {
                if (currentMode == ServiceMode.Speaking || isMusicPlaying) {
                    string rejText = e.Result.Text.Trim();
                    bool isExit = IsExitPhrase(rejText) || BargeInExitRegex.IsMatch(rejText);
                    bool isStop = IsStopPhrase(rejText);
                    string wakeQuery = null;
                    bool isWake = BargeInWakeRegex.IsMatch(rejText) || IsWakePhrase(rejText, out wakeQuery);

                    if (!isExit && !isStop && !isWake && e.Result.Alternates != null) {
                        foreach (RecognizedPhrase alt in e.Result.Alternates) {
                            string altText = alt.Text ?? "";
                            if (IsExitPhrase(altText) || BargeInExitRegex.IsMatch(altText)) {
                                isExit = true;
                                break;
                            }
                            if (IsStopPhrase(altText)) {
                                isStop = true;
                                break;
                            }
                            if (BargeInWakeRegex.IsMatch(altText) || IsWakePhrase(altText, out wakeQuery)) {
                                isWake = true;
                                break;
                            }
                        }
                    }

                    if (isExit || isStop || isWake) {
                        try { synth.SpeakAsyncCancelAll(); } catch { }
                        StopMusic();

                        if (isExit) {
                            Console.WriteLine("EXIT");
                            Console.Out.Flush();
                            return;
                        }

                        if (isWake) {
                            Console.WriteLine("INTERRUPTED:" + rejText);
                            Console.WriteLine("WAKE");
                            if (!string.IsNullOrEmpty(wakeQuery) && wakeQuery.Length > 2) {
                                Console.WriteLine("FINAL:" + wakeQuery);
                                SetMode(ServiceMode.Standby);
                            } else {
                                SetMode(ServiceMode.Listening);
                            }
                            Console.Out.Flush();
                            return;
                        }

                        if (isStop) {
                            Console.WriteLine("INTERRUPTED:" + rejText);
                            Console.Out.Flush();
                            SetMode(ServiceMode.Standby);
                            return;
                        }
                    }
                    return;
                }

                if (currentMode == ServiceMode.Listening) {
                    SaveAudioIfAvailable(e.Result);
                    if (e.Result != null && e.Result.Alternates != null && e.Result.Alternates.Count > 0) {
                        string alt = e.Result.Alternates[0].Text ?? "";
                        if (!string.IsNullOrEmpty(alt)) {
                            string stripped = StripWakePrefix(alt);
                            if (!string.IsNullOrWhiteSpace(stripped) && !IsWakeOnlyWord(stripped)) {
                                lastHypothesizedText = stripped;
                                lastSpeechTime = DateTime.UtcNow;
                                speechHeardInListening = true;
                            }
                        }
                    }
                    // Do not abort listening! Allow user to finish speaking or watchdog to finalize!
                    return;
                }

                if (currentMode == ServiceMode.Standby) {
                    string text = e.Result != null && !string.IsNullOrEmpty(e.Result.Text) ? e.Result.Text.Trim() : "";
                    string query;
                    if (!string.IsNullOrEmpty(text) && IsWakePhrase(text, out query)) {
                        Console.WriteLine("WAKE");
                        query = StripWakePrefix(query);
                        if (!string.IsNullOrWhiteSpace(query) && !IsWakeOnlyWord(query) && query.Length >= 2) {
                            Console.WriteLine("FINAL:" + query);
                            SetMode(ServiceMode.Standby);
                        } else {
                            SetMode(ServiceMode.Listening);
                        }
                        Console.Out.Flush();
                        return;
                    }

                    if (e.Result != null && e.Result.Alternates != null) {
                        foreach (RecognizedPhrase alt in e.Result.Alternates) {
                            string altText = alt.Text ?? "";
                            if (!string.IsNullOrEmpty(altText) && IsWakePhrase(altText, out query)) {
                                Console.WriteLine("WAKE");
                                query = StripWakePrefix(query);
                                if (!string.IsNullOrWhiteSpace(query) && !IsWakeOnlyWord(query) && query.Length >= 2) {
                                    Console.WriteLine("FINAL:" + query);
                                    SetMode(ServiceMode.Standby);
                                } else {
                                    SetMode(ServiceMode.Listening);
                                }
                                Console.Out.Flush();
                                return;
                            }
                        }
                    }
                }
            }
        }

        static void Synth_SpeakProgress(object sender, SpeakProgressEventArgs e) {
            if (!string.IsNullOrEmpty(currentSpeakingText)) {
                int len = Math.Min(currentSpeakingText.Length, e.CharacterPosition + e.CharacterCount);
                string slice = currentSpeakingText.Substring(0, len).Trim();
                Console.WriteLine("STREAM:" + slice);
                Console.Out.Flush();
            }
        }

        static void Synth_SpeakCompleted(object sender, SpeakCompletedEventArgs e) {
            if (!string.IsNullOrEmpty(currentSpeakingText)) {
                Console.WriteLine("STREAM:" + currentSpeakingText);
            }
            Console.WriteLine("SPEAK_DONE");
            Console.Out.Flush();
            lock (modeLock) {
                if (currentMode == ServiceMode.Speaking) {
                    SetMode(ServiceMode.Standby);
                }
            }
        }
    }
}
