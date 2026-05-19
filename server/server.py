"""
Lokale proxy server — haalt Claude sessie-usage op via claude.ai en geeft
het als simpele JSON terug aan de ESP32.

Instellen:
  1. pip install curl-cffi
  2. Zorg dat claude_cookie.txt bestaat (zie README)
  3. python server.py

Databronnen (volgorde):
  1. claude.ai API  (automatisch, via claude_cookie.txt)
  2. usage_override.json  (handmatige fallback)
  3. MOCK_USAGE=1   (hardware test, toont 42%)
"""

import os
import json
import time
from http.server import HTTPServer, BaseHTTPRequestHandler

try:
    from curl_cffi import requests as cffi_requests
    HAS_CURL_CFFI = True
except ImportError:
    HAS_CURL_CFFI = False

# ── config ────────────────────────────────────────────────────────────────────

PORT        = 8765
MOCK_MODE   = os.environ.get("MOCK_USAGE", "0") == "1"

_dir = os.path.dirname(__file__)
COOKIE_FILE   = os.path.join(_dir, "claude_cookie.txt")
OVERRIDE_FILE = os.path.join(_dir, "usage_override.json")

CLAUDE_ORG_ID = "2fdab8ce-7fd0-40d6-a3eb-5fab0698fc01"

CACHE_TTL_SEC = 15
_cache = {"ts": 0.0, "data": None}

# ── databronnen ───────────────────────────────────────────────────────────────

def _parse_cookies(cookie_str: str) -> dict:
    cookies = {}
    for part in cookie_str.split(";"):
        part = part.strip()
        if "=" in part:
            k, v = part.split("=", 1)
            cookies[k.strip()] = v.strip()
    return cookies


def fetch_from_claude_ai() -> dict:
    if not HAS_CURL_CFFI:
        raise RuntimeError("curl_cffi niet geïnstalleerd — voer uit: pip install curl-cffi")

    with open(COOKIE_FILE) as f:
        cookie_str = f.read().strip()
    if not cookie_str:
        raise RuntimeError("claude_cookie.txt is leeg")

    url = f"https://claude.ai/api/organizations/{CLAUDE_ORG_ID}/usage"
    resp = cffi_requests.get(
        url,
        headers={
            "anthropic-client-platform": "web_claude_ai",
            "anthropic-client-version": "1.0.0",
            "Accept": "*/*",
            "Referer": "https://claude.ai/settings/usage",
        },
        cookies=_parse_cookies(cookie_str),
        impersonate="chrome120",
        timeout=10,
    )

    if resp.status_code == 403:
        raise RuntimeError("Cloudflare blokkade (403) — vernieuw claude_cookie.txt")
    if resp.status_code != 200:
        raise RuntimeError(f"claude.ai fout {resp.status_code}: {resp.text[:200]}")

    body = resp.json()
    pct  = float(body.get("five_hour", {}).get("utilization", 0.0))
    used = int(pct / 100.0 * 1_000_000)
    return {"percentage": round(pct, 1), "used": used, "limit": 1_000_000}


def read_override() -> dict | None:
    try:
        age = time.time() - os.path.getmtime(OVERRIDE_FILE)
        if age > 86400:
            print(f"  ⚠ usage_override.json is {age/3600:.0f}u oud")
        with open(OVERRIDE_FILE) as f:
            data = json.load(f)
        limit = int(data.get("limit", 1_000_000))
        if "percentage" in data:
            pct  = float(data["percentage"])
            used = int(pct / 100.0 * limit)
        else:
            used = int(data["used"])
            pct  = round(min((used / limit) * 100.0, 100.0), 1)
        return {"percentage": pct, "used": used, "limit": limit}
    except FileNotFoundError:
        return None
    except Exception as e:
        print(f"  ⚠ Fout bij lezen override: {e}")
        return None


def get_usage() -> dict:
    if MOCK_MODE:
        return {"percentage": 42.0, "used": 420000, "limit": 1_000_000}

    now = time.time()
    if now - _cache["ts"] < CACHE_TTL_SEC and _cache["data"]:
        return _cache["data"]

    if os.path.exists(COOKIE_FILE):
        try:
            data = fetch_from_claude_ai()
            _cache["ts"]   = now
            _cache["data"] = data
            return data
        except Exception as e:
            print(f"  ⚠ claude.ai API mislukt: {e}")
            print("    → Vernieuw claude_cookie.txt als het cookie verlopen is")

    data = read_override()
    if data:
        return data

    raise RuntimeError("Geen data. Maak claude_cookie.txt aan of start met MOCK_USAGE=1")

# ── HTTP handler ──────────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[{self.address_string()}] {fmt % args}")

    def do_GET(self):
        if self.path != "/usage":
            self.send_response(404)
            self.end_headers()
            return
        try:
            data = get_usage()
            body = json.dumps(data).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            print(f"  → {data['percentage']:.1f}%")
        except Exception as exc:
            print(f"  !! Fout: {exc}")
            body = json.dumps({"error": str(exc)}).encode()
            self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.wfile.write(body)

# ── main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print(f"Server gestart op poort {PORT}")
    print(f"Endpoint: http://0.0.0.0:{PORT}/usage")
    if MOCK_MODE:
        print("⚠  MOCK_USAGE=1 — testdata actief (42%)")
    elif not HAS_CURL_CFFI:
        print("⚠  curl_cffi niet gevonden — voer uit: pip install curl-cffi")
    elif os.path.exists(COOKIE_FILE):
        print("✓  claude_cookie.txt gevonden — automatische sync actief")
    else:
        print("⚠  Geen claude_cookie.txt — start met MOCK_USAGE=1 of voeg cookie toe")
    print()

    server = HTTPServer(("0.0.0.0", PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer gestopt.")
