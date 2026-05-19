# Claude Usage Checker — TTGO T-Display

ESP32 bureaudingetje dat je Claude API usage als percentage op een klein LCD-schermpje toont.

```
┌─────────────────┐
│  Claude Usage   │
│                 │
│      73%        │
│  ████████░░░░  │
│  730K / 1.0M    │
└─────────────────┘
```

## Hardware

- **LILYGO TTGO T-Display** (ESP32 + 1.14" IPS LCD, 135×240, ST7789V)

## Architectuur

```
[TTGO T-Display] --WiFi--> [server.py op je PC] --HTTPS--> [Anthropic API]
```

De API key staat alleen op je PC, nooit in de firmware.

---

## Stap 1 — VSCode instellen

1. Installeer de **PlatformIO IDE** extensie in VSCode
2. Open deze map als project in PlatformIO (`firmware/` map)
3. PlatformIO downloadt automatisch de benodigde libraries

## Stap 2 — Configureer WiFi & server

Pas `firmware/src/config.h` aan:

```c
#define WIFI_SSID     "jouw_wifi"
#define WIFI_PASSWORD "jouw_wachtwoord"
#define SERVER_HOST   "192.168.1.100"  // IP van je PC (ipconfig)
#define SERVER_PORT   8765
```

Vind je PC's IP: open PowerShell → `ipconfig` → zoek "IPv4-adres"

## Stap 3 — Flash de firmware

1. Sluit de TTGO aan via USB
2. In VSCode: klik op de **→ Upload** knop in de PlatformIO toolbar (of `Ctrl+Alt+U`)
3. Het scherm laat "Verbinden..." zien tijdens opstarten

## Stap 4 — Start de lokale server

```powershell
# Stel je API key in (eenmalig per sessie)
$env:ANTHROPIC_API_KEY = "sk-ant-..."

# Optioneel: eigen token limiet (standaard 1.000.000)
$env:MONTHLY_TOKEN_LIMIT = "500000"

# Start de server
cd server
python server.py
```

Laat dit draaien als je de TTGO op je bureau hebt staan. De ESP32 poll elke 5 minuten.

## Aanpassen

| Wat | Waar |
|---|---|
| Poll interval | `config.h` → `POLL_INTERVAL_MS` |
| Token limiet | `$env:MONTHLY_TOKEN_LIMIT` of hardcode in `server.py` |
| Drempelwaarden kleuren (groen/oranje/rood) | `main.cpp` → `bar_color()` |
| Schermrotatie | `main.cpp` → `tft.setRotation(1)` (0=portait, 1=landscape) |
