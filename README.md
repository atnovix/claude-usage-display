# Claude Usage Monitor — TTGO T-Display

A small ESP32 desk widget that shows your Claude.ai session usage on a 1.14" LCD display, updating every 15 seconds.

```
┌──────────────────────────┐
│     Claude Session       │
│                          │
│          24%             │
│  ██████░░░░░░░░░░░░░░░  │
│  Session 24%   Week 9%   │
└──────────────────────────┘
```

Press the physical button to switch to the **Overview** page:

```
┌──────────────────────────┐
│        Overview          │
│ Session  [████░░░]  24% │
│ Week     [█░░░░░░]   9% │
│ Design   [░░░░░░░]   0% │
│ Credits  [███████]  85% │
│       EUR 2555 / 3000    │
└──────────────────────────┘
```

Color thresholds: green < 80%, yellow 80–90%, red ≥ 90%. At 100% the display shows a live countdown to reset.

## Hardware

- **LILYGO TTGO T-Display** (ESP32 + 1.14" IPS LCD, 135×240px, ST7789V)
- USB-C for power

## Architecture

```
[TTGO T-Display] --WiFi (every 15s)--> [server.py on PC] --HTTPS--> [claude.ai internal API]
```

The server uses your browser session cookie to fetch usage data from claude.ai. Your credentials never leave your local network.

---

## Setup

### 1 — Install PlatformIO

Install the **PlatformIO IDE** extension in VSCode. Open the `firmware/` folder as a PlatformIO project — libraries are downloaded automatically.

### 2 — Configure WiFi & server

Copy `firmware/src/config.h.example` to `firmware/src/config.h` and fill in your details:

```c
#define WIFI_SSID     "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"
#define SERVER_HOST   "192.168.1.100"  // your PC's local IP (run: ipconfig)
#define SERVER_PORT   8765
```

### 3 — Set up the server

Install the required Python library:

```powershell
pip install curl-cffi
```

Get your session cookie:
1. Open [claude.ai/settings/usage](https://claude.ai/settings/usage) in Chrome
2. Open DevTools (F12) → Network → filter on Fetch/XHR → refresh the page
3. Right-click the `usage` request → **Copy → Copy as cURL (cmd)**
4. Extract the cookie string (the `-b "..."` part) and save it to `server/claude_cookie.txt`

Start the server:

```powershell
cd server
python server.py
```

Keep this running while the device is on your desk.

### 4 — Flash the firmware

1. Connect the TTGO via USB
2. In VSCode: click **→ Upload** in the PlatformIO toolbar (or `Ctrl+Alt+U`)

## Configuration

| Setting | Location |
|---|---|
| Poll interval | `config.h` → `POLL_INTERVAL_MS` |
| Color thresholds | `main.cpp` → `bar_color()` |
| Screen rotation | `main.cpp` → `tft.setRotation(1)` |

## Notes

- The session cookie expires when you log out of claude.ai. If the display shows "Server unavailable", refresh the cookie by repeating step 3.
- No Anthropic API key required — this reads usage stats from the claude.ai web interface.
