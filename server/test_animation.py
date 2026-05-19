"""
Step-by-step test server.

Non-100% steps auto-advance every STEP_SEC seconds.
100% steps stay until you press SPACE — each one starts a live countdown.

Step sequence:
  1. Green  50%  — resets in 3:45:00
  2. Yellow 85%  — resets in 45:00
  3. Red    95%  — resets in 05:30
  4. 100%        — live from 5:00:00
  5. 100%        — live from 2:00:00
  6. 100%        — live from 1:00:00
  7. 100%        — live from 02:00
  8. 100%        — live from 01:00

Press SPACE to jump to the next step.
"""
import json, time, threading, msvcrt
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT     = 8765
STEP_SEC = 16   # auto-advance interval for non-100% steps

STEPS = [
    {"label": "Green  50%",    "pct":  50.0, "resets_in": 3*3600+45*60},
    {"label": "Yellow 85%",    "pct":  85.0, "resets_in": 45*60},
    {"label": "Red    95%",    "pct":  95.0, "resets_in": 5*60+30},
    {"label": "100%  5:00:00", "pct": 100.0, "resets_in": 5*3600},
    {"label": "100%  2:00:00", "pct": 100.0, "resets_in": 2*3600},
    {"label": "100%  1:00:00", "pct": 100.0, "resets_in": 3600},
    {"label": "100%  02:00",   "pct": 100.0, "resets_in": 120},
    {"label": "100%  01:00",   "pct": 100.0, "resets_in": 60},
]

g_step       = 0
g_step_start = time.time()
g_lock       = threading.Lock()


def advance_step():
    global g_step, g_step_start
    with g_lock:
        if g_step < len(STEPS) - 1:
            g_step += 1
            g_step_start = time.time()
            print(f"\n  → Step {g_step+1}/{len(STEPS)}: {STEPS[g_step]['label']}    ")
        else:
            print(f"\n  Already on last step    ")


def current_state():
    with g_lock:
        step       = g_step
        step_start = g_step_start
    pct = STEPS[step]["pct"]
    if pct < 100.0:
        return pct, STEPS[step]["resets_in"]
    # All 100% steps: count down live from when we entered this step
    into_step = time.time() - step_start
    return 100.0, max(0, int(STEPS[step]["resets_in"] - into_step))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args): pass
    def do_GET(self):
        if self.path != "/usage":
            self.send_response(404); self.end_headers(); return
        pct, resets_in = current_state()
        data = {
            "percentage":        pct,
            "weekly":            round(pct * 0.6, 1),
            "design":            round(pct * 0.1, 1),
            "credits_pct":       round(pct * 0.85, 1),
            "credits_used":      int(pct * 25),
            "credits_limit":     3000,
            "credits_currency":  "EUR",
            "session_resets_in": resets_in,
            "weekly_resets_in":  7200,
        }
        body = json.dumps(data).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def fmt(sec):
    h, m, s = sec//3600, (sec%3600)//60, sec%60
    if h: return f"{h}:{m:02}:{s:02}"
    return f"{m:02}:{s:02}"


def status_loop():
    while True:
        with g_lock:
            step       = g_step
            step_start = g_step_start
        pct, rem = current_state()
        label    = STEPS[step]["label"]
        if pct >= 100.0:
            print(f"  Step {step+1}/{len(STEPS)}  {label}  — countdown: {fmt(rem)}  [SPACE=next]    ", end="\r")
        else:
            into_step = time.time() - step_start
            auto_in   = max(0, STEP_SEC - into_step)
            print(f"  Step {step+1}/{len(STEPS)}  {label}  — auto in: {auto_in:.0f}s  reset: {fmt(rem)}  [SPACE=skip]    ", end="\r")
        time.sleep(1)


def key_listener():
    while True:
        if msvcrt.kbhit():
            ch = msvcrt.getwch()
            if ch == ' ':
                advance_step()
        time.sleep(0.05)


def auto_advance_loop():
    """Auto-advance non-100% steps every STEP_SEC seconds."""
    while True:
        time.sleep(1)
        with g_lock:
            step       = g_step
            step_start = g_step_start
        if STEPS[step]["pct"] < 100.0:
            if time.time() - step_start >= STEP_SEC:
                advance_step()


threading.Thread(target=status_loop,       daemon=True).start()
threading.Thread(target=key_listener,      daemon=True).start()
threading.Thread(target=auto_advance_loop, daemon=True).start()

print(f"Test server on port {PORT}\n")
print(f"  Steps 1-3 auto-advance every {STEP_SEC}s — press SPACE to skip")
print(f"  Steps 4-8 are manual (SPACE) — each shows a live countdown\n")
for i, s in enumerate(STEPS):
    h, m, sec = s['resets_in']//3600, (s['resets_in']%3600)//60, s['resets_in']%60
    t = f"{h}:{m:02}:{sec:02}" if h else f"{m:02}:{sec:02}"
    print(f"  Step {i+1}: {s['label']:20s}  (starts at {t})")
print()

HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
