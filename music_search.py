import sys
import json
import urllib.parse
import urllib.request
import re
import os
import hashlib
import tempfile

def clean_song_title(title):
    if not title:
        return "Unknown Track"
    patterns = [
        r'\s*[\(\[\{]?(?:official\s+)?(?:music\s+)?(?:video|audio|visualizer|lyric\s+video|lyrics)[\)\]\}]?',
        r'\s*[\(\[\{]?(?:hd|4k|remastered|explicit)[\)\]\}]?',
        r'\s*[\(\[\{]audio[\)\]\}]?',
    ]
    cleaned = title
    for p in patterns:
        cleaned = re.sub(p, '', cleaned, flags=re.IGNORECASE)
    cleaned = cleaned.strip(" -|[]()")
    return cleaned if cleaned else title

def search_full_song(query):
    # Check local cache first for instant playback
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

    # Try yt_dlp for the direct full-length audio stream
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
            search_term = f"ytsearch1:{query} audio"
            info = ydl.extract_info(search_term, download=False)
            entry = None
            if info:
                if 'entries' in info and len(info['entries']) > 0:
                    entry = info['entries'][0]
                else:
                    entry = info

            if entry and entry.get('url'):
                raw_title = entry.get('title', query)
                clean_t = clean_song_title(raw_title)
                artist = entry.get('artist')
                if not artist or artist == "Unknown":
                    if " - " in clean_t:
                        parts = clean_t.split(" - ", 1)
                        artist = parts[0].strip()
                        clean_t = parts[1].strip()
                    else:
                        uploader = entry.get('uploader') or ""
                        uploader = re.sub(r'\s*-\s*Topic$', '', uploader, flags=re.IGNORECASE).strip()
                        artist = uploader if uploader else "Featured Artist"

                duration = int(entry.get('duration') or 0)
                play_url = entry.get('url', '')

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
    except Exception as e:
        pass

    # Fallback: iTunes API preview
    try:
        enc = urllib.parse.quote(query)
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
                    'track_name': result.get('trackName', query),
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
