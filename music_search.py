import sys
import json
import urllib.parse
import urllib.request
import re
import os
import hashlib
import tempfile

if sys.platform == 'win32':
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

def clean_song_title(title):
    if not title:
        return "Unknown Track"
    patterns = [
        r'\s*[\(\[\{]?(?:official\s+)?(?:music\s+)?(?:video|audio|visualizer|lyric\s+video|lyrics|hd|4k|remastered|explicit)[\)\]\}]?',
        r'\s*[\(\[\{](?:feat\.|ft\.)[^\)\]\}]+[\)\]\}]',
        r'\s*[\(\[\{]audio[\)\]\}]?',
    ]
    cleaned = title
    for p in patterns:
        cleaned = re.sub(p, '', cleaned, flags=re.IGNORECASE)
    cleaned = cleaned.strip(" -|[]()")
    return cleaned if cleaned else title

def score_candidate(entry, query):
    title = (entry.get('title') or '').lower()
    uploader = (entry.get('uploader') or '').lower()
    duration = int(entry.get('duration') or 0)

    if duration > 0 and (duration < 60 or duration > 600):
        return -100

    score = 50

    if uploader.endswith(' - topic'):
        score += 45
    elif 'vevo' in uploader:
        score += 40
    elif 'official' in uploader:
        score += 20
    elif 'records' in uploader or 'music' in uploader:
        score += 15

    bad_markers = [
        '1 hour', '10 hours', 'loop', 'reaction', 'reacts', 'tutorial',
        'lesson', 'parody', 'slowed', 'reverb', 'sped up', 'speed up',
        'nightcore', 'bass boosted', '8d audio', 'karaoke', 'instrumental',
        'behind the scenes', 'making of', 'teaser', 'trailer', 'mashup'
    ]
    for bm in bad_markers:
        if bm in title and bm not in query.lower():
            score -= 40

    good_markers = ['official audio', 'official music video', 'official video', 'official lyric video', 'lyrics']
    for gm in good_markers:
        if gm in title:
            score += 15
            break

    q_words = re.findall(r'\w+', query.lower())
    for w in q_words:
        if len(w) > 2 and (w in title or w in uploader):
            score += 12

    if 120 <= duration <= 320:
        score += 10

    return score

def search_full_song(query):
    cache_dir = os.path.join(tempfile.gettempdir(), 'sphere_music_cache')
    os.makedirs(cache_dir, exist_ok=True)

    q_norm = query.lower().strip()
    q_hash = hashlib.md5(q_norm.encode('utf-8')).hexdigest()
    cache_meta = os.path.join(cache_dir, f"{q_hash}.json")
    cache_audio = os.path.join(cache_dir, f"{q_hash}.m4a")

    if os.path.exists(cache_meta) and os.path.exists(cache_audio) and os.path.getsize(cache_audio) > 100000:
        try:
            with open(cache_meta, "r", encoding="utf-8") as mf:
                meta = json.load(mf)
                meta['url'] = cache_audio
                meta['found'] = True
                return meta
        except Exception:
            pass

    clean_q = re.sub(r'^(?:can\s+you\s+|please\s+|could\s+you\s+)?(?:play\s+(?:the\s+song\s+|a\s+song\s+|song\s+|track\s+|me\s+)?)+', '', query, flags=re.IGNORECASE).strip()
    if not clean_q or clean_q.lower() in ('music', 'some music', 'a song'):
        clean_q = "Blinding Lights The Weeknd"

    try:
        import yt_dlp
        ydl_opts = {
            'format': 'ba[ext=m4a]/140/bestaudio[ext=m4a]/ba/bestaudio/b',
            'noplaylist': True,
            'quiet': True,
            'no_warnings': True,
            'default_search': 'ytsearch',
        }
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            search_term = f"ytsearch5:{clean_q}"
            info = ydl.extract_info(search_term, download=False)
            entries = info.get('entries', []) if info else []

            if entries:
                scored = []
                for e in entries:
                    if e and e.get('url'):
                        s = score_candidate(e, clean_q)
                        scored.append((s, e))

                scored.sort(key=lambda x: x[0], reverse=True)
                best_entry = scored[0][1] if scored else entries[0]

                raw_title = best_entry.get('title', clean_q)
                clean_t = clean_song_title(raw_title)
                artist = best_entry.get('artist')
                if not artist or artist == "Unknown":
                    if " - " in clean_t:
                        parts = clean_t.split(" - ", 1)
                        artist = parts[0].strip()
                        clean_t = parts[1].strip()
                    else:
                        uploader = best_entry.get('uploader') or ""
                        uploader = re.sub(r'\s*-\s*Topic$', '', uploader, flags=re.IGNORECASE).strip()
                        artist = uploader if uploader else "Featured Artist"

                duration = int(best_entry.get('duration') or 0)
                play_url = best_entry.get('url', '')

                result = {
                    'found': True,
                    'track_name': clean_t,
                    'artist_name': artist,
                    'url': play_url,
                    'duration': duration
                }

                try:
                    with open(cache_meta, "w", encoding="utf-8") as mf:
                        json.dump(result, mf, ensure_ascii=False)
                except Exception:
                    pass

                return result
    except Exception:
        pass

    try:
        enc = urllib.parse.quote(clean_q)
        req = urllib.request.Request(
            f"https://itunes.apple.com/search?term={enc}&entity=song&limit=1",
            headers={'User-Agent': 'Mozilla/5.0'}
        )
        with urllib.request.urlopen(req, timeout=4) as response:
            data = json.loads(response.read().decode('utf-8'))
            if data.get('resultCount', 0) > 0:
                result = data['results'][0]
                return {
                    'found': True,
                    'track_name': result.get('trackName', clean_q),
                    'artist_name': result.get('artistName', 'Featured Artist'),
                    'url': result.get('previewUrl', ''),
                    'duration': 30
                }
    except Exception:
        pass

    return {'found': False}

if __name__ == '__main__':
    if len(sys.argv) > 1:
        q = sys.argv[1]
        out_file = sys.argv[2] if len(sys.argv) > 2 else "sphere_music_res.json"
        res = search_full_song(q)
        with open(out_file, "w", encoding="utf-8") as f:
            json.dump(res, f, ensure_ascii=False)
