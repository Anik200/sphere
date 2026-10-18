# SphereAI — Custom Native C Voice AI Describing Agent

A custom, ultra-optimized Voice AI Describing Agent built entirely in native C (`brain.c`) and low-level Windows audio IPC. Requires **zero external heavy models (no Qwen/Ollama)**, **zero GPU (0% GPU usage)**, and responds **instantly in milliseconds**.

---

## Key Features & Capabilities

1. **Wake-Word Voice Activation ("Hey Sphere")**:
   - The AI remains in low-power standby mode (gentle green Braille particle ball).
   - Saying **"Hey Sphere"** or **"Sphere"** immediately wakes it up (ball turns purple and begins listening to your voice).
   - Supports single-breath queries: say *"Hey Sphere what is the speed of light"* and it wakes up, searches, and answers directly!

2. **Real-time Live Speech Transcription (Google-style)**:
   - As you speak into the microphone, your words appear **in real time on the screen** word-by-word just like Google Voice Search (`Listening: "what is the speed of light..."`).
   - The purple particle sphere revolves dynamically according to your incoming voice volume.

3. **Word-by-word Formatted Speech Streaming**:
   - When the AI speaks, it does not dump walls of text.
   - It prints its words **one by one on the screen in exact synchronization with the speech synthesizer**, neatly word-wrapped and formatted.
   - Sphere revolves in green while speaking.

4. **Instant Voice Barge-In / Interruption**:
   - Saying **"Hey Sphere"** or **"stop"** while the AI is speaking immediately cuts off speech audio (<1ms) and resumes listening for your new request.

5. **Native C AI Brain (`brain.c`)**:
   - **Math Reasoning**: Evaluates arithmetic, percentages, powers, and roots with 100% mathematical precision in $<0.001\text{ ms}$.
   - **Sensory Visual Descriptions**: 5-layer physical breakdown (form factor, materials, luster, lighting, and functional parts) fused with live web search definitions and Wikimedia Commons imagery.

6. **Braille Particle Visual Engine**:
   - 60 FPS flicker-free Braille particle 3D sphere.
   - Morphs into a glowing golden smiling face when joyful or praised, then seamlessly reassembles into the sphere.

7. **Voice-Controlled Exits**:
   - Say **"goodbye"**, **"exit"**, **"quit"**, or **"turn off"** to cleanly shut down.

---

## Build & Run

### Compiling on Windows:
```bash
mingw32-make
```
*(Compiles both `sphere_voice_service.exe` and `sphere_ai.exe`)*

### Running:
```bash
.\sphere_ai.exe
```

---

## Voice Interaction Guide

| Action | Voice Trigger |
|---|---|
| **Activate AI** | *"Hey Sphere"* or *"Sphere"* |
| **Direct Query** | *"Hey Sphere what is 125 times 8?"*, *"Hey Sphere what does a car look like?"* |
| **Interrupt Speaking** | Say *"Hey Sphere"* or *"stop"* |
| **Trigger Happy Smile** | *"You did a great job"*, *"smile"*, *"happy"*, or tell a joke |
| **Exit / Shutdown** | *"goodbye"*, *"exit"*, *"quit"* |
