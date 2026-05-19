"""
Step-by-step test server:
  1. Green  50%  — resets in 3:45:00
  2. Yellow 85%  — resets in 45:00
  3. Red    95%  — resets in 05:30
  4. 100%        — live countdown from 90s

Press SPACE to jump to the next step immediately.
"""
import json, time, threading, msvcrt
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT     = 8765
STEP_SEC = 16   # 16s per step (ESP32 polls every 15s)

STEPS = [
    {"label": "Green  50%",       "pct": 50.0,  "resets_in": 3*3600+45*60},
    {"label": "Yellow 85%",       "pct": 85.0,  "resets_in": 45*60},
    {"label": "Red    95%",       "pct": 95.0,  "resets_in": 5*60+30},
    {"label": "100%  countdown",  "pct": 100.0, "resets_in": 90},
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
    if step < len(STEPS) - 1:
        return pct, STEPS[step]["resets_in"]
    # Last step: countdown counts down for real
    into_last = time.time() - step_start
    return 100.0, max(0, int(STEPS[-1]["resets_in"] - into_last))


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
        if pct >= 100:
            print(f"  Step {step+1}/{len(STEPS)}  {label}  — countdown: {fmt(rem)}    ", end="\r")
        else:
            into_step = time.time() - step_start
            auto_in   = max(0, STEP_SEC - into_step)
            print(f"  Step {step+1}/{len(STEPS)}  {label}  — auto-advance in: {auto_in:.0f}s  reset: {fmt(rem)}    ", end="\r")
        time.sleep(1)


def key_listener():
    print("  Press SPACE to jump to next step\n")
    while True:
        if msvcrt.kbhit():
            ch = msvcrt.getwch()
            if ch == ' ':
                advance_step()
        time.sleep(0.05)


def auto_advance_loop():
    """Automatically advance steps every STEP_SEC seconds."""
    while True:
        time.sleep(STEP_SEC)
        with g_lock:
            step = g_step
        if step < len(STEPS) - 1:
            advance_step()


threading.Thread(target=status_loop,       daemon=True).start()
threading.Thread(target=key_listener,      daemon=True).start()
threading.Thread(target=auto_advance_loop, daemon=True).start()

print(f"Test server on port {PORT}  ({STEP_SEC}s auto-advance per step)\n")
for i, s in enumerate(STEPS):
    h, m, sec = s['resets_in']//3600, (s['resets_in']%3600)//60, s['resets_in']%60
    t = f"{h}:{m:02}:{sec:02}" if h else f"{m:02}:{sec:02}"
    print(f"  Step {i+1}: {s['label']}  (reset in {t})")
print()

HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
