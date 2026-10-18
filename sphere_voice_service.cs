using System;
using System.IO;
using System.Globalization;
using System.Speech.Recognition;
using System.Speech.Synthesis;
using System.Threading;
using System.Diagnostics;
using System.Text.RegularExpressions;

namespace SphereVoiceEngine {
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
        static int lastAudioLevel = 0;
        static DateTime lastAmpSendTime = DateTime.UtcNow;
        static string currentSpeakingText = "";

        static dynamic mediaPlayer = null;
        static bool isMusicPlaying = false;
        static Thread musicMonitorThread = null;

        static readonly Regex ExitWordRegex = new Regex(
            @"^(exit|quit|goodbye|good\s+bye|bye|bye\s+bye|see\s+you\s+later|close\s+app|close\s+the\s+app|exit\s+app|exit\s+the\s+app|quit\s+app|shut\s+down|shutdown|terminate)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly Regex ExitWakeRegex = new Regex(
            @"^(hey\s+sphere|sphere|her\s+sphere|hi\s+sphere|hello\s+sphere)\s+(exit|quit|goodbye|good\s+bye|bye|bye\s+bye|see\s+you\s+later|close\s+app|close\s+the\s+app|shutdown|shut\s+down)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly Regex BargeInStopRegex = new Regex(
            @"^(hey\s+sphere\s+|sphere\s+)?(stop|be\s+quiet|shut\s+up|pause|cancel|stop\s+talking|stop\s+it|stop\s+music|stop\s+the\s+music)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly Regex BargeInWakeRegex = new Regex(
            @"^(hey\s+sphere|sphere|hi\s+sphere|hello\s+sphere|ok\s+sphere|okay\s+sphere|yo\s+sphere|hey\s+spear|spear|wake\s+up\s+sphere|wake\s+up|her\s+sphere|here\s+sphere|hey\s+fear|hey\s+spare|hay\s+sphere)(\s+(?<q>.*))?$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static readonly Regex BargeInExitRegex = new Regex(
            @"^(hey\s+sphere\s+|sphere\s+)?(exit|quit|close\s+app|close\s+the\s+app|shut\s+down|shutdown|goodbye|good\s+bye|bye|bye\s+bye|see\s+you\s+later)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        static bool IsExitPhrase(string text) {
            if (string.IsNullOrEmpty(text)) return false;
            string clean = text.Trim().Trim('.', '?', '!', ',', ' ', '"', '\'');
            if (ExitWordRegex.IsMatch(clean)) return true;
            if (ExitWakeRegex.IsMatch(clean)) return true;
            return false;
        }

        static void Main(string[] args) {
            Console.OutputEncoding = System.Text.Encoding.UTF8;
            Console.WriteLine("STATUS:INITIALIZING");
            Console.Out.Flush();

            // Watchdog thread: immediately abort speech and exit if parent process terminates
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
                // Initialize Synthesizer
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

                // Initialize Recognizer
                CultureInfo culture = new CultureInfo("en-US");
                try {
                    sre = new SpeechRecognitionEngine(culture);
                } catch {
                    sre = new SpeechRecognitionEngine();
                }

                sre.SetInputToDefaultAudioDevice();

                // 1. Wake Grammar (Expanded with phonetic variations: spear, fear, spare, okay sphere, wake up, etc.)
                Choices wakeChoices = new Choices(new string[] {
                    "hey sphere", "sphere", "hi sphere", "hello sphere", "ok sphere", "okay sphere", "yo sphere",
                    "hey spear", "spear", "hi spear", "hello spear", "ok spear", "okay spear",
                    "hey fear", "hi fear", "hello fear", "her sphere", "here sphere", "hear sphere",
                    "hey spare", "the sphere", "a sphere", "hay sphere", "ay sphere",
                    "wake up", "wake up sphere",
                    "stop", "exit", "quit", "goodbye", "good bye", "bye", "bye bye", "shutdown", "shut down",
                    "close app", "exit app", "quit app", "see you later",
                    "hey sphere exit", "sphere exit", "hey sphere close", "sphere close",
                    "hey sphere quit", "sphere quit", "hey sphere bye", "sphere bye",
                    "play", "play song", "play music", "hey sphere play", "sphere play"
                });
                GrammarBuilder wakeGb = new GrammarBuilder(wakeChoices);
                wakeGrammar = new Grammar(wakeGb);
                wakeGrammar.Name = "WakeGrammar";
                wakeGrammar.Weight = 1.0f;
                sre.LoadGrammar(wakeGrammar);

                // 2. Wake + Dictation Grammar (Direct queries in one breath)
                Choices wakePrefixChoices = new Choices(new string[] {
                    "hey sphere", "sphere", "hi sphere", "hello sphere", "ok sphere", "okay sphere", "yo sphere",
                    "hey spear", "spear", "hi spear", "hello spear", "ok spear", "okay spear",
                    "her sphere", "here sphere", "hey fear", "wake up sphere", "wake up",
                    "hey sphere play", "sphere play", "play"
                });
                GrammarBuilder wakeDictGb = new GrammarBuilder();
                wakeDictGb.Append(wakePrefixChoices);
                wakeDictGb.AppendDictation();
                wakeDictGrammar = new Grammar(wakeDictGb);
                wakeDictGrammar.Name = "WakeDictGrammar";
                wakeDictGrammar.Weight = 0.5f;
                sre.LoadGrammar(wakeDictGrammar);

                // 3. Dictation Grammar - Only enabled during active Listening mode to prevent noise hallucinations
                dictGrammar = new DictationGrammar();
                dictGrammar.Name = "DictationGrammar";
                sre.LoadGrammar(dictGrammar);
                dictGrammar.Enabled = false;

                // 4. Interrupt Grammar (Barge-in while speaking or playing music)
                Choices interruptChoices = new Choices(new string[] {
                    "stop", "be quiet", "shut up", "pause", "cancel", "stop talking", "stop it", "stop music", "stop the music",
                    "hey sphere", "sphere", "hi sphere", "hello sphere", "ok sphere", "okay sphere", "yo sphere",
                    "hey spear", "spear", "hi spear", "hello spear", "ok spear", "okay spear",
                    "exit", "quit", "goodbye", "good bye", "bye", "bye bye", "see you later", "close app", "close the app", "exit app", "exit the app", "quit app",
                    "hey sphere stop", "sphere stop", "hey sphere shut up", "hey sphere pause", "hey sphere be quiet", "hey sphere cancel",
                    "hey sphere exit", "sphere exit", "hey sphere quit", "sphere quit", "hey sphere goodbye"
                });
                GrammarBuilder intGb = new GrammarBuilder(interruptChoices);
                interruptGrammar = new Grammar(intGb);
                interruptGrammar.Name = "InterruptGrammar";
                sre.LoadGrammar(interruptGrammar);
                interruptGrammar.Enabled = false;

                // Event Handlers
                sre.SpeechHypothesized += Sre_SpeechHypothesized;
                sre.SpeechRecognized += Sre_SpeechRecognized;
                sre.SpeechRecognitionRejected += Sre_SpeechRecognitionRejected;
                sre.AudioLevelUpdated += Sre_AudioLevelUpdated;
                sre.SpeechDetected += Sre_SpeechDetected;

                sre.RecognizeAsync(RecognizeMode.Multiple);

                Console.WriteLine("STATUS:READY");
                Console.Out.Flush();

                // Background stdin command processor thread
                Thread cmdThread = new Thread(CommandLoop);
                cmdThread.IsBackground = true;
                cmdThread.Start();

                // Background watchdog for silence timeout during active listening
                while (true) {
                    Thread.Sleep(100);
                    lock (modeLock) {
                        if (currentMode == ServiceMode.Listening) {
                            TimeSpan elapsed = DateTime.UtcNow - wakeStartTime;
                            if (!speechHeardInListening && elapsed.TotalSeconds > 7.0) {
                                Console.WriteLine("TIMEOUT");
                                Console.Out.Flush();
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

        static bool IsWakePhrase(string text, out string remainingQuery) {
            remainingQuery = "";
            if (string.IsNullOrEmpty(text)) return false;
            string lower = text.ToLowerInvariant().Trim();

            string[] sphereKeywords = new string[] {
                "hey sphere play", "sphere play", "hey sphere", "hi sphere", "hello sphere",
                "ok sphere", "okay sphere", "yo sphere", "her sphere", "here sphere", "hear sphere",
                "hey spear", "hi spear", "hello spear", "ok spear", "okay spear", "spear",
                "hey fear", "hi fear", "hello fear", "hey spare", "the sphere", "a sphere",
                "hay sphere", "ay sphere", "wake up sphere", "wake up", "sphere"
            };

            foreach (string kw in sphereKeywords) {
                int idx = lower.IndexOf(kw);
                if (idx != -1) {
                    // Ensure keyword starts at word boundary
                    if (idx > 0 && char.IsLetterOrDigit(lower[idx - 1])) continue;
                    int queryStart = idx + kw.Length;
                    if (queryStart < lower.Length && char.IsLetterOrDigit(lower[queryStart])) continue;

                    if (queryStart < text.Length) {
                        remainingQuery = text.Substring(queryStart).Trim();
                        while (remainingQuery.Length > 0 && (remainingQuery[0] == ',' || remainingQuery[0] == '.' || remainingQuery[0] == '?' || remainingQuery[0] == ' ' || remainingQuery[0] == '!')) {
                            remainingQuery = remainingQuery.Substring(1).Trim();
                        }
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
                Type wmpType = Type.GetTypeFromProgID("WMPlayer.OCX");
                if (wmpType == null) {
                    Console.WriteLine("ERROR:WMP_NOT_FOUND");
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
                            Thread.Sleep(250);
                            try {
                                if (mediaPlayer != null) {
                                    int state = (int)mediaPlayer.playState;
                                    // 3 = Playing, 6 = Buffering, 9 = Transitioning, 10 = Ready
                                    if (state == 3 || state == 6 || state == 9 || state == 10) {
                                        hasStarted = true;
                                        stoppedCount = 0;
                                    } else if (state == 8) {
                                        // 8 = MediaEnded
                                        isMusicPlaying = false;
                                        Console.WriteLine("MUSIC_DONE");
                                        Console.Out.Flush();
                                        SetMode(ServiceMode.Standby);
                                        break;
                                    } else if (hasStarted && state == 1) {
                                        // Require consecutive stopped checks so transient buffer pauses don't abort song
                                        stoppedCount++;
                                        if (stoppedCount >= 4) { // 1 second of confirmed stop
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
                Console.Out.Flush();
                isMusicPlaying = false;
            }
        }

        static void StopMusic() {
            isMusicPlaying = false;
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
                speechHeardInListening = false;

                if (newMode == ServiceMode.Standby) {
                    wakeGrammar.Enabled = true;
                    wakeDictGrammar.Enabled = true;
                    dictGrammar.Enabled = false; // Clean standby: only listen for wake phrases, ignore room noise
                    interruptGrammar.Enabled = false;
                    Console.WriteLine("MODE:STANDBY");
                    Console.Out.Flush();
                } else if (newMode == ServiceMode.Listening) {
                    wakeGrammar.Enabled = false; // Disable wake words so dictation doesn't fight wake grammar
                    wakeDictGrammar.Enabled = false;
                    dictGrammar.Enabled = true; // Dedicated, accurate dictation
                    interruptGrammar.Enabled = true;
                    Console.WriteLine("MODE:LISTENING");
                    Console.Out.Flush();
                } else if (newMode == ServiceMode.Speaking) {
                    wakeGrammar.Enabled = false;
                    wakeDictGrammar.Enabled = false; // Disable dictation so it does NOT swallow barge-in commands!
                    dictGrammar.Enabled = false;
                    interruptGrammar.Enabled = true; // Pure constrained barge-in grammar
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

                if (line.Equals("CMD:WAKE_STANDBY", StringComparison.OrdinalIgnoreCase)) {
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
                    SetMode(ServiceMode.Standby);
                } else if (line.StartsWith("CMD:PLAY_MUSIC ", StringComparison.OrdinalIgnoreCase)) {
                    string musicUrl = line.Substring(15).Trim();
                    PlayMusic(musicUrl);
                } else if (line.Equals("CMD:STOP_MUSIC", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    SetMode(ServiceMode.Standby);
                } else if (line.Equals("CMD:EXIT", StringComparison.OrdinalIgnoreCase)) {
                    StopMusic();
                    try { synth.SpeakAsyncCancelAll(); } catch { }
                    try { sre.RecognizeAsyncStop(); } catch { }
                    Environment.Exit(0);
                }
            }

            // Stdin EOF reached: Parent process has exited or pipe was closed!
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
            if (Math.Abs(level - lastAudioLevel) >= 1 || (now - lastAmpSendTime).TotalMilliseconds >= 25) {
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
                    // Never interrupt speech or music on tentative hypotheses!
                    // Waiting for high-confidence Sre_SpeechRecognized ensures speaker echo
                    // or music vocals never cause speech cutoffs or ghost exits.
                    return;
                }

                if (currentMode == ServiceMode.Listening) {
                    speechHeardInListening = true;

                    // Filter out jittery low-confidence sub-word noises
                    if (e.Result.Confidence < 0.18f && hypText.Length < 7) return;

                    // Clean leading wake words if caught
                    string lowerHyp = hypText.ToLowerInvariant();
                    if (lowerHyp.StartsWith("hey sphere ")) hypText = hypText.Substring(11).Trim();
                    else if (lowerHyp.StartsWith("sphere ")) hypText = hypText.Substring(7).Trim();
                    else if (lowerHyp.StartsWith("her sphere ")) hypText = hypText.Substring(11).Trim();
                    else if (lowerHyp.StartsWith("hi sphere ")) hypText = hypText.Substring(10).Trim();

                    // Phonetic corrections for common SAPI acoustic confusions
                    hypText = Regex.Replace(hypText, @"\bcomputer mouth\b", "computer mouse", RegexOptions.IgnoreCase);
                    hypText = Regex.Replace(hypText, @"\ba mouth look\b", "a mouse look", RegexOptions.IgnoreCase);
                    hypText = Regex.Replace(hypText, @"\bthe mouth look\b", "the mouse look", RegexOptions.IgnoreCase);
                    hypText = Regex.Replace(hypText, @"\bwhat a mouth\b", "what a mouse", RegexOptions.IgnoreCase);
                    hypText = Regex.Replace(hypText, @"\bwhat does a mouth\b", "what does a mouse", RegexOptions.IgnoreCase);
                    hypText = Regex.Replace(hypText, @"\blook likes\b", "look like", RegexOptions.IgnoreCase);

                    if (hypText.Length > 0) {
                        Console.WriteLine("HYP:" + hypText);
                        Console.Out.Flush();
                    }
                } else if (currentMode == ServiceMode.Standby) {
                    // Instant wake-up on hypothesized wake phrase for zero-latency response!
                    string remaining;
                    if (IsWakePhrase(hypText, out remaining)) {
                        Console.WriteLine("WAKE");
                        if (!string.IsNullOrEmpty(remaining) && remaining.Length > 2) {
                            Console.WriteLine("FINAL:" + remaining);
                            SetMode(ServiceMode.Standby);
                        } else {
                            SetMode(ServiceMode.Listening);
                        }
                        Console.Out.Flush();
                        return;
                    }
                    return;
                }
            }
        }

        static void Sre_SpeechRecognized(object sender, SpeechRecognizedEventArgs e) {
            if (e.Result == null || string.IsNullOrEmpty(e.Result.Text)) return;
            string text = e.Result.Text.Trim();

            lock (modeLock) {
                if (currentMode == ServiceMode.Speaking || isMusicPlaying) {
                    // Require confidence >= 0.35f to avoid echo while speaking/playing music
                    if (e.Result.Confidence < 0.35f) {
                        return;
                    }

                    bool isExit = IsExitPhrase(text) || BargeInExitRegex.IsMatch(text);
                    bool isStop = BargeInStopRegex.IsMatch(text);
                    string wakeQuery = null;
                    bool isWake = BargeInWakeRegex.IsMatch(text) || IsWakePhrase(text, out wakeQuery);

                    // Must match an explicit exit, stop, or wake command
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
                    // Lowered threshold to 0.15f: constrained wake choices already prevent false positives!
                    if (e.Result.Confidence < 0.15f) {
                        return;
                    }

                    // Check for exit
                    if (IsExitPhrase(text)) {
                        Console.WriteLine("EXIT");
                        Console.Out.Flush();
                        return;
                    }

                    // Check for direct play command in Standby (e.g. "play despacito", "play some music")
                    string lowerText = text.ToLowerInvariant().Trim();
                    if (lowerText.StartsWith("play ") || lowerText.StartsWith("play the song ") || lowerText.StartsWith("play song ")) {
                        Console.WriteLine("WAKE");
                        Console.WriteLine("FINAL:" + text);
                        SetMode(ServiceMode.Standby);
                        Console.Out.Flush();
                        return;
                    }

                    // Check for wake word and possible immediate query
                    string query;
                    if (IsWakePhrase(text, out query)) {
                        if (IsExitPhrase(query)) {
                            Console.WriteLine("EXIT");
                            Console.Out.Flush();
                            return;
                        }
                        Console.WriteLine("WAKE");
                        if (!string.IsNullOrEmpty(query)) {
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
                    // Check for exit
                    if (IsExitPhrase(text)) {
                        Console.WriteLine("EXIT");
                        Console.Out.Flush();
                        return;
                    }

                    // Check for stop or cancel
                    if (BargeInStopRegex.IsMatch(text)) {
                        Console.WriteLine("TIMEOUT");
                        Console.Out.Flush();
                        SetMode(ServiceMode.Standby);
                        return;
                    }

                    // If user repeated wake word while already listening, reset silence timer
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
                            return;
                        }
                    }

                    // Phonetic corrections
                    text = Regex.Replace(text, @"\bcomputer mouth\b", "computer mouse", RegexOptions.IgnoreCase);
                    text = Regex.Replace(text, @"\ba mouth look\b", "a mouse look", RegexOptions.IgnoreCase);
                    text = Regex.Replace(text, @"\bthe mouth look\b", "the mouse look", RegexOptions.IgnoreCase);
                    text = Regex.Replace(text, @"\bwhat a mouth\b", "what a mouse", RegexOptions.IgnoreCase);
                    text = Regex.Replace(text, @"\bwhat does a mouth\b", "what does a mouse", RegexOptions.IgnoreCase);

                    if (!string.IsNullOrEmpty(text)) {
                        Console.WriteLine("FINAL:" + text);
                        Console.Out.Flush();
                        SetMode(ServiceMode.Standby);
                    }
                }
            }
        }

        static void Sre_SpeechRecognitionRejected(object sender, SpeechRecognitionRejectedEventArgs e) {
            if (e.Result == null || string.IsNullOrEmpty(e.Result.Text)) return;
            lock (modeLock) {
                if (currentMode == ServiceMode.Standby) {
                    string text = e.Result.Text.Trim();
                    string query;
                    if (IsWakePhrase(text, out query)) {
                        // Rescued wake word from SAPI low confidence rejection
                        Console.WriteLine("WAKE");
                        if (!string.IsNullOrEmpty(query)) {
                            Console.WriteLine("FINAL:" + query);
                            SetMode(ServiceMode.Standby);
                        } else {
                            SetMode(ServiceMode.Listening);
                        }
                        Console.Out.Flush();
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
