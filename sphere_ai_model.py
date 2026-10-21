import sys
import os
import json
import re
import urllib.parse
import urllib.request
import subprocess
import time
import shutil
import dynamic_artist

AVAILABLE_SHAPES = {
    # Faces
    "happy": ["happy", "smile", "smiling", "joyful"],
    "sad": ["sad", "cry", "crying", "sob", "frown"],
    "surprised": ["surprised", "surprise", "shocked", "gasp", "amazed"],
    "angry": ["angry", "mad", "furious", "rage"],
    "wink": ["wink", "winking"],
    "cool": ["cool", "sunglasses", "shades"],
    "silly": ["silly", "goofy", "tongue", "stick out your tongue", ":p"],
    "laugh": ["laugh", "laughing", "xd", "chuckle"],
    "confused": ["confused", "puzzled", "thinking", "?_?"],
    "cat": ["cat", "kitty", "meow", ":3"],
    "skull": ["skull", "skeleton", "spooky"],
    # Gestures
    "thumbs_up": ["thumbs up", "thumb up", "like", "approve", "good job"],
    "peace": ["peace", "victory", "v sign"],
    "wave": ["wave", "waving", "say hi", "hello", "greet"],
    "rock": ["rock on", "metal", "horns"],
    "ok": ["ok sign", "okay sign", "👌", "a-ok"],
    # Figures & Shapes
    "heart": ["heart", "love"],
    "star": ["star"],
    "cube": ["cube", "box", "3d cube"],
    "pyramid": ["pyramid", "3d pyramid"],
    "donut": ["donut", "torus"],
    "spiral": ["spiral", "galaxy", "vortex"],
    "flower": ["flower", "bloom", "blossom"],
    "infinity": ["infinity", "loop", "lemniscate"],
    "sphere": ["sphere", "reset", "normal"]
}

DRAW_TRIGGERS = [
    r"\b(draw|sketch|paint|illustrate|render|shape of|make a|create a|show me a|can you make|draw me|make me)\b"
]

def has_drawing_intent(query: str) -> bool:
    q = query.lower()
    for pat in DRAW_TRIGGERS:
        if re.search(pat, q):
            return True
    for k in dynamic_artist.SEMANTIC_BUILDERS:
        if re.search(r"\b" + re.escape(k) + r"\b", q):
            return True
    return False

def detect_shape_from_text(query: str, response: str) -> str:
    # 1. Check explicit tag like [shape: wave] or shape: wave
    m = re.search(r"\[?\bshape\s*:\s*([a-zA-Z0-9_]+)\]?", response, re.IGNORECASE)
    if m:
        shape_cand = m.group(1).lower().strip()
        if shape_cand in AVAILABLE_SHAPES:
            return shape_cand
        for shape, triggers in AVAILABLE_SHAPES.items():
            if shape_cand in triggers:
                return shape

    # 2. Check query and response keywords
    comb = (query + " " + response).lower()
    for shape, triggers in AVAILABLE_SHAPES.items():
        for trig in triggers:
            if re.search(r"\b" + re.escape(trig) + r"\b", comb):
                return shape

    return ""

FETCH_PATTERNS = [
    r"\b(weather|forecast|temperature|rain|snow|humidity)\b",
    r"\b(news|headline|latest|recent|current|today|tonight|yesterday|now)\b",
    r"\b(stock|price|market|crypto|bitcoin|shares)\b",
    r"\b(who won|score|match|game|tournament|champion|medal)\b",
    r"\b(when is the next|release date|upcoming|scheduled)\b",
    r"\b(who is the (current|new|present) (president|prime minister|ceo|leader|governor|mayor))\b",
    r"\b(what happened (to|in|at)|events in)\b",
    r"\b(traffic in|flight status)\b",
    r"\b(population of|exchange rate)\b"
]

def should_fetch(query: str) -> bool:
    q = query.lower().strip()
    for pattern in FETCH_PATTERNS:
        if re.search(pattern, q):
            return True
    return False

def sanitize_text(text: str) -> str:
    if not text:
        return ""
    # Normalize degree notations to clean spoken/readable English
    text = text.replace("°C", " degrees Celsius").replace("℃", " degrees Celsius")
    text = text.replace("°F", " degrees Fahrenheit").replace("℉", " degrees Fahrenheit")
    text = text.replace("°", " degrees")
    
    # Remove emojis and miscellaneous Unicode pictographs/surrogates
    emoji_pattern = re.compile(
        "["
        "\U0001F600-\U0001F64F"  # emoticons
        "\U0001F300-\U0001F5FF"  # symbols & pictographs
        "\U0001F680-\U0001F6FF"  # transport & map
        "\U0001F1E0-\U0001F1FF"  # flags (iOS)
        "\U00002702-\U000027B0"
        "\U000024C2-\U0001F251"
        "\U0001F900-\U0001F9FF"  # supplemental symbols
        "\U0001FA70-\U0001FAFF"  # symbols and pictographs extended-a
        "\U00002600-\U000026FF"  # miscellaneous symbols (sun, umbrella, cloud, etc.)
        "\U00002B00-\U00002BFF"
        "\U0000FE00-\U0000FE0F"  # variation selectors
        "]+",
        flags=re.UNICODE
    )
    text = emoji_pattern.sub("", text)
    # Strip any stray ASCII control characters
    text = re.sub(r"[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    return text

def extract_city_for_weather(query: str) -> str:
    # 1. Strip wake words and conversational greetings
    q = re.sub(r"^(hey\s+spike|spike|listen\s+spike|listen|spike\s+wake\s+up|wake\s+up\s+spike|hey\s+sphere|sphere|please|can\s+you\s+tell\s+me|tell\s+me)\b", "", query, flags=re.IGNORECASE).strip()
    
    # 2. Extract city following 'in', 'for', 'at'
    m = re.search(r"\b(?:in|for|at)\s+([a-zA-Z\s\.\-]+?)(?:\s+(?:today|tomorrow|now|currently|outside|right\s+now))?$", q, flags=re.IGNORECASE)
    if m:
        candidate = m.group(1).strip()
        if candidate and len(candidate) > 1:
            return candidate

    # 3. Strip common weather query phrases
    cleaned = re.sub(r"\b(what(\x27?s|\s+is)?(\s+the)?|how(\x27?s|\s+is)?(\s+the)?|tell\s+me(\s+the)?|the|weather|temperature|forecast|outside|today|tomorrow|now|currently|report|like)\b", " ", q, flags=re.IGNORECASE)
    cleaned = re.sub(r"\s+", " ", cleaned).strip()
    if cleaned and len(cleaned) > 1:
        return cleaned
    return "London"

def fetch_weather(query: str) -> str:
    city = extract_city_for_weather(query)
    try:
        fmt = "%l: %C, temperature %t (humidity: %h)"
        url = f"https://wttr.in/{urllib.parse.quote(city)}?format={urllib.parse.quote(fmt)}"
        req = urllib.request.Request(url, headers={"User-Agent": "curl/7.88.1"})
        with urllib.request.urlopen(req, timeout=3.5) as resp:
            data = resp.read().decode("utf-8", errors="ignore").strip()
            if data and len(data) > 3 and "404" not in data:
                clean_data = sanitize_text(data)
                return f"Live weather report: {clean_data}"
    except Exception:
        pass
    return ""

def fetch_wikipedia_info(query: str) -> str:
    cleaned = re.sub(r"^(hey\s+spike|spike|listen\s+spike|listen|hey\s+sphere|sphere|please|can\s+you\s+tell\s+me|tell\s+me|what\s+is|who\s+is|where\s+is|tell\s+me\s+about)\s+", "", query, flags=re.IGNORECASE).strip()
    if not cleaned:
        cleaned = query
    encoded_q = urllib.parse.quote(cleaned)

    try:
        search_url = f"https://en.wikipedia.org/w/api.php?action=query&list=search&srsearch={encoded_q}&srlimit=1&format=json"
        req = urllib.request.Request(search_url, headers={"User-Agent": "SphereAI/2.0 (voice-client)"})
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
            search_items = data.get("query", {}).get("search", [])
            if not search_items:
                return ""
            title = search_items[0].get("title", "")
            if not title:
                return ""

        ext_url = f"https://en.wikipedia.org/w/api.php?action=query&prop=extracts&exintro=1&explaintext=1&titles={urllib.parse.quote(title)}&format=json"
        req2 = urllib.request.Request(ext_url, headers={"User-Agent": "SphereAI/2.0 (voice-client)"})
        with urllib.request.urlopen(req2, timeout=3.0) as resp2:
            data2 = json.loads(resp2.read().decode("utf-8", errors="ignore"))
            pages = data2.get("query", {}).get("pages", {})
            for p in pages.values():
                ext = p.get("extract", "").strip()
                if ext and len(ext) > 20:
                    return ext
    except Exception:
        pass
    return ""

def fetch_duckduckgo_info(query: str) -> str:
    cleaned = re.sub(r"^(hey\s+sphere|sphere|please)\s+", "", query, flags=re.IGNORECASE).strip()
    encoded = urllib.parse.quote(cleaned)
    try:
        url = f"https://api.duckduckgo.com/?q={encoded}&format=json&no_html=1&skip_disambig=1"
        req = urllib.request.Request(url, headers={"User-Agent": "SphereAI/2.0"})
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
            abstract = data.get("AbstractText", "").strip()
            if abstract and len(abstract) > 20:
                return abstract
            answer = data.get("Answer", "").strip()
            if answer:
                return answer
    except Exception:
        pass
    return ""

def fetch_live_info(query: str) -> str:
    q_lower = query.lower()
    if any(w in q_lower for w in ["weather", "forecast", "temperature", "rain"]):
        w = fetch_weather(query)
        if w:
            return w

    ddg = fetch_duckduckgo_info(query)
    if ddg:
        return ddg

    wiki = fetch_wikipedia_info(query)
    if wiki:
        return wiki

    return ""

def find_ollama_binary() -> str:
    p = shutil.which("ollama")
    if p:
        return p
    local_app = os.environ.get("LOCALAPPDATA", "")
    if local_app:
        cand = os.path.join(local_app, "Programs", "Ollama", "ollama.exe")
        if os.path.exists(cand):
            return cand
    prog_files = os.environ.get("ProgramFiles", "")
    if prog_files:
        cand = os.path.join(prog_files, "Ollama", "ollama.exe")
        if os.path.exists(cand):
            return cand
    prog_files_x86 = os.environ.get("ProgramFiles(x86)", "")
    if prog_files_x86:
        cand = os.path.join(prog_files_x86, "Ollama", "ollama.exe")
        if os.path.exists(cand):
            return cand
    return ""

def ensure_ollama_running() -> bool:
    try:
        req = urllib.request.Request("http://localhost:11434/api/tags", headers={"User-Agent": "SphereAI/2.0"})
        with urllib.request.urlopen(req, timeout=0.8) as resp:
            return True
    except Exception:
        pass

    ollama_path = find_ollama_binary()
    if ollama_path and os.path.exists(ollama_path):
        try:
            flags = 0x08000000 if os.name == "nt" else 0
            subprocess.Popen([ollama_path, "serve"], creationflags=flags)
            for _ in range(6):
                time.sleep(0.5)
                try:
                    req = urllib.request.Request("http://localhost:11434/api/tags", headers={"User-Agent": "SphereAI/2.0"})
                    with urllib.request.urlopen(req, timeout=0.8) as resp:
                        return True
                except Exception:
                    pass
        except Exception:
            pass
    return False

def get_best_ollama_model() -> str:
    pref_models = ["qwen2.5:0.5b", "smollm", "qwen2.5-coder:7b-instruct", "qwen-agent", "llama3.2:1b"]
    custom_model = os.environ.get("SPHERE_AI_MODEL", "").strip()
    if custom_model:
        return custom_model

    try:
        req = urllib.request.Request("http://localhost:11434/api/tags", headers={"User-Agent": "SphereAI/2.0"})
        with urllib.request.urlopen(req, timeout=1.5) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
            installed = [m.get("name", "") for m in data.get("models", [])]
            for p in pref_models:
                for inst in installed:
                    if inst.startswith(p):
                        return inst
            if installed:
                return installed[0]
    except Exception:
        pass
    return "qwen2.5:0.5b"

def query_fallback_ai_model(prompt: str) -> str:
    try:
        encoded = urllib.parse.quote(prompt)
        url = f"https://text.pollinations.ai/{encoded}?model=openai"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
        with urllib.request.urlopen(req, timeout=6.0) as resp:
            ans = resp.read().decode("utf-8", errors="ignore").strip()
            if ans and len(ans) > 3:
                ans = ans.strip('"').strip("'").strip()
                ans = re.sub(r"[\*#`_]", "", ans)
                return ans
    except Exception:
        pass

    try:
        url = f"https://api.duckduckgo.com/?q={urllib.parse.quote(prompt)}&format=json&no_html=1"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=4.0) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
            ans = data.get("AbstractText", "") or data.get("Answer", "")
            if ans and len(ans) > 3:
                return ans.strip()
    except Exception:
        pass

    return ""

def generate_ai_answer(prompt: str, model_name: str) -> str:
    # 1. First priority: Local Ollama model
    if ensure_ollama_running():
        try:
            url = "http://localhost:11434/api/generate"
            payload = {
                "model": model_name,
                "prompt": prompt,
                "stream": False,
                "options": {
                    "temperature": 0.65,
                    "num_predict": 120,
                    "top_p": 0.9
                }
            }
            data_bytes = json.dumps(payload).encode("utf-8")
            req = urllib.request.Request(url, data=data_bytes, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=12.0) as resp:
                res_json = json.loads(resp.read().decode("utf-8", errors="ignore"))
                ans = res_json.get("response", "").strip()
                ans = ans.strip('"').strip("'").strip()
                ans = re.sub(r"[\*#`_]", "", ans)
                if ans and len(ans) > 3:
                    return ans
        except Exception:
            pass

    # 2. Second priority: High-speed cloud AI model fallback
    fallback = query_fallback_ai_model(prompt)
    if fallback and len(fallback) > 3:
        return fallback

    return ""

def load_personality() -> dict:
    cfg_path = os.path.join(os.path.dirname(__file__), "personality.json")
    if os.path.exists(cfg_path):
        try:
            with open(cfg_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {
        "name": "Spike",
        "title": "Spike",
        "tone": "sharp, quick-witted, confident, helpful, and friendly",
        "color": "cyan",
        "prompt": "Your name is Spike. Never say you are Qwen, JARVIS, SphereAI, or created by Alibaba Cloud. You are Spike, a sharp, quick-witted, and genuinely helpful personal AI assistant. Keep answers concise (1-2 spoken sentences suitable for voice synthesis). Never use bullet points, asterisks, emojis, or markdown. Always maintain your confident, friendly, and helpful Spike persona."
    }

def process_query(query: str, existing_context: str = "") -> dict:
    needs_fetch = should_fetch(query)
    fetched_context = existing_context

    if needs_fetch and not fetched_context:
        fetched_context = fetch_live_info(query)

    model_name = get_best_ollama_model()

    persona = load_personality()
    persona_prompt = persona.get("prompt", "You are Spike, a sharp, quick-witted, and helpful AI assistant.")

    system_instruction = (
        f"{persona_prompt} "
        "You have direct neural control over a 600-particle 3D canvas and can physically draw or morph into "
        "ANY object, creature, vehicle, tool, weapon, landscape, or abstract shape! "
        "When asked to draw or create something, describe what you are creating with subtle dry humor. "
        "Answer in 1-2 spoken sentences suitable for voice synthesis. Do not use bullet points, asterisks, or markdown."
    )

    if fetched_context:
        prompt = (
            f"{system_instruction}\n\n"
            f"Verified live factual context:\n{fetched_context}\n\n"
            f"User Question: {query}\n\n"
            f"Answer based on the facts in 1-2 spoken sentences:"
        )
        action = "fetch_and_answer"
    else:
        prompt = (
            f"{system_instruction}\n\n"
            f"User Question: {query}\n\n"
            f"Answer concisely in 1-2 spoken sentences:"
        )
        action = "direct_answer"

    ai_answer = generate_ai_answer(prompt, model_name)

    if not ai_answer and fetched_context:
        ai_answer = fetched_context
        sentences = re.split(r"(?<=[.!?])\s+", ai_answer)
        if len(sentences) > 2:
            ai_answer = " ".join(sentences[:2])

        # Sanitize any model self-identification leakage and clean output text
    ai_answer = re.sub(r'\bI am (Qwen|JARVIS|SphereAI|Sphere)(, an AI assistant created by Alibaba Cloud)?\b', 'I am Spike, your personal AI assistant', ai_answer, flags=re.IGNORECASE)
    ai_answer = re.sub(r'\b(Qwen|Alibaba Cloud|JARVIS)\b', 'Spike', ai_answer, flags=re.IGNORECASE)
    ai_answer = sanitize_text(ai_answer)
    detected_shape = detect_shape_from_text(query, ai_answer)

    custom_points = None
    custom_shape_name = ""
    custom_color = ""

    # Synthesize custom particles if drawing intent is detected
    if has_drawing_intent(query) or "[strokes:" in ai_answer.lower():
        pts, name, col = dynamic_artist.synthesize_custom_shape(query, ai_answer)
        custom_points = pts
        custom_shape_name = name
        custom_color = col
        if not ai_answer or len(ai_answer) < 5:
            ai_answer = f"Morphing my 600 glowing particles to draw a {name.lower()} for you! Watch the particles align in 3D space."

    # Remove any technical tags from the spoken voice output
    ai_answer = re.sub(r"\[?\bshape\s*:\s*[a-zA-Z0-9_]+\]?", "", ai_answer, flags=re.IGNORECASE)
    ai_answer = re.sub(r"\[strokes\s*:.*?\]", "", ai_answer, flags=re.IGNORECASE | re.DOTALL).strip()

    is_happy = 0
    lower_ans = ai_answer.lower()
    if any(w in lower_ans for w in ["joy", "happy", "wonderful", "delighted", "awesome", "fantastic", "glad", "love"]):
        is_happy = 1

    result = {
        "found": 1 if len(ai_answer) > 0 else 0,
        "answer": ai_answer,
        "action": action,
        "model": model_name,
        "is_happy": is_happy,
        "shape": detected_shape
    }

    if custom_points:
        result["custom_points"] = custom_points
        result["custom_shape_name"] = custom_shape_name
        result["color"] = custom_color

    return result

def main():
    if len(sys.argv) < 3:
        print("Usage: python sphere_ai_model.py <query> <output_json_path> [context]")
        sys.exit(1)

    query = sys.argv[1].strip()
    out_file = sys.argv[2].strip()
    existing_context = sys.argv[3].strip() if len(sys.argv) >= 4 else ""

    result = process_query(query, existing_context)

    try:
        with open(out_file, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False)
    except Exception as ex:
        print("Error writing output file:", ex)

if __name__ == "__main__":
    main()
