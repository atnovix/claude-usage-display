#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "config.h"

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

#define BTN1_PIN     35
#define BTN2_PIN     0

#define COLOR_BG     TFT_BLACK
#define COLOR_TITLE  TFT_WHITE
#define COLOR_BAR_BG 0x2104
#define COLOR_LOW    0x07E0   // groen
#define COLOR_MID    0xFD20   // geel
#define COLOR_HIGH   0xF800   // rood
#define COLOR_DIM    0x7BEF   // grijs

static float         g_session_pct       = 0.0f;
static float         g_weekly_pct        = 0.0f;
static float         g_design_pct        = 0.0f;
static float         g_credits_pct       = 0.0f;
static int           g_credits_used      = 0;
static int           g_credits_limit     = 0;
static char          g_credits_currency[8] = "";
static uint32_t      g_session_resets_in = 0;
static uint32_t      g_weekly_resets_in  = 0;
static unsigned long g_fetch_ms          = 0;
static bool          g_wifi_ok           = false;
static bool          g_fetch_ok          = false;
static int           g_view              = 0;   // 0=sessie, 1=overzicht
static unsigned long g_last_poll         = 0;
static unsigned long g_last_draw         = 0;

// ── helpers ───────────────────────────────────────────────────────────────────

uint16_t bar_color(float pct) {
    if (pct < 80.0f) return COLOR_LOW;
    if (pct < 90.0f) return COLOR_MID;
    return COLOR_HIGH;
}

uint32_t remaining_sec() {
    uint32_t total   = (g_view == 0) ? g_session_resets_in : g_weekly_resets_in;
    uint32_t elapsed = (millis() - g_fetch_ms) / 1000UL;
    return (total > elapsed) ? total - elapsed : 0;
}

void fmt_countdown(uint32_t sec, char* buf, size_t sz) {
    uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    if (h > 0) snprintf(buf, sz, "%u:%02u:%02u", h, m, s);  // font 6 supports digits + colon
    else        snprintf(buf, sz, "%02u:%02u", m, s);
}

// ── pagina 0: sessie ──────────────────────────────────────────────────────────

// Alleen de countdown-cijfers hertekenen zonder fillScreen (voorkomt flicker)
void update_countdown() {
    char countdown[16];
    fmt_countdown(remaining_sec(), countdown, sizeof(countdown));
    tft.fillRect(0, 48, tft.width(), 52, COLOR_BG);  // wis alleen het cijfergebied
    tft.setTextColor(COLOR_HIGH, COLOR_BG);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(countdown, tft.width() / 2, 48, 6);
}

void draw_session() {
    float pct  = g_session_pct;
    bool  full = (pct >= 99.5f);

    spr.fillScreen(COLOR_BG);
    spr.setTextDatum(TC_DATUM);

    spr.setTextColor(COLOR_TITLE, COLOR_BG);
    spr.drawString("Claude Session", tft.width() / 2, 5, 2);

    if (full) {
        spr.setTextColor(COLOR_HIGH, COLOR_BG);
        spr.drawString("LIMIT REACHED", tft.width() / 2, 26, 2);

        char countdown[16];
        fmt_countdown(remaining_sec(), countdown, sizeof(countdown));
        spr.drawString(countdown, tft.width() / 2, 48, 6);

        spr.setTextColor(COLOR_DIM, COLOR_BG);
        spr.drawString("until reset", tft.width() / 2, 100, 1);

        char sub[48];
        snprintf(sub, sizeof(sub), "Session %.0f%%   Week %.0f%%", g_session_pct, g_weekly_pct);
        spr.drawString(sub, tft.width() / 2, 112, 1);
    } else {
        char pct_str[8];
        snprintf(pct_str, sizeof(pct_str), "%.0f%%", pct);
        spr.setTextColor(bar_color(pct), COLOR_BG);
        spr.drawString(pct_str, tft.width() / 2, 32, 6);

        int bx = 10, by = 90, bw = tft.width() - 20, bh = 14;
        spr.fillRoundRect(bx, by, bw, bh, 4, COLOR_BAR_BG);
        int fw = (int)(pct / 100.0f * bw);
        if (fw > 0) spr.fillRoundRect(bx, by, fw, bh, 4, bar_color(pct));

        spr.setTextColor(COLOR_DIM, COLOR_BG);
        char sub[48];
        snprintf(sub, sizeof(sub), "Session %.0f%%   Week %.0f%%", g_session_pct, g_weekly_pct);
        spr.drawString(sub, tft.width() / 2, 112, 1);
    }

    spr.setTextDatum(BC_DATUM);
    if (!g_wifi_ok) {
        spr.setTextColor(COLOR_HIGH, COLOR_BG);
        spr.drawString("No WiFi", tft.width() / 2, tft.height() - 1, 1);
    } else if (!g_fetch_ok) {
        spr.setTextColor(COLOR_MID, COLOR_BG);
        spr.drawString("Server unavailable", tft.width() / 2, tft.height() - 1, 1);
    } else {
        spr.setTextColor(COLOR_DIM, COLOR_BG);
        spr.drawString("OK", tft.width() / 2, tft.height() - 1, 1);
    }

    spr.pushSprite(0, 0);
}

// ── pagina 1: overzicht ───────────────────────────────────────────────────────

void draw_overview() {
    spr.fillScreen(COLOR_BG);

    spr.setTextColor(COLOR_TITLE, COLOR_BG);
    spr.setTextDatum(TC_DATUM);
    spr.drawString("Overview", tft.width() / 2, 5, 2);

    struct { const char* label; float pct; } rows[4] = {
        {"Session", g_session_pct},
        {"Week",    g_weekly_pct},
        {"Design",  g_design_pct},
        {"Credits", g_credits_pct},
    };

    const int y0    = 26;
    const int row_h = 24;
    const int bar_x = 54;
    const int bar_w = 128;
    const int bar_h = 10;

    for (int i = 0; i < 4; i++) {
        int   ry   = y0 + i * row_h;
        int   cy   = ry + row_h / 2;
        float pct  = rows[i].pct;
        int   fill = (int)(pct / 100.0f * bar_w);

        spr.setTextColor(COLOR_DIM, COLOR_BG);
        spr.setTextDatum(ML_DATUM);
        spr.drawString(rows[i].label, 4, cy, 1);

        spr.fillRoundRect(bar_x, ry + (row_h - bar_h) / 2, bar_w, bar_h, 3, COLOR_BAR_BG);
        if (fill > 0)
            spr.fillRoundRect(bar_x, ry + (row_h - bar_h) / 2, fill, bar_h, 3, bar_color(pct));

        char pb[8];
        snprintf(pb, sizeof(pb), "%.0f%%", pct);
        spr.setTextColor(bar_color(pct), COLOR_BG);
        spr.setTextDatum(MR_DATUM);
        spr.drawString(pb, 226, cy, 1);
    }

    spr.setTextDatum(BC_DATUM);
    if (g_credits_limit > 0) {
        spr.setTextColor(COLOR_DIM, COLOR_BG);
        char cb[32];
        snprintf(cb, sizeof(cb), "%s %d / %d",
                 g_credits_currency, g_credits_used, g_credits_limit);
        spr.drawString(cb, tft.width() / 2, tft.height() - 1, 1);
    } else if (!g_wifi_ok) {
        spr.setTextColor(COLOR_HIGH, COLOR_BG);
        spr.drawString("No WiFi", tft.width() / 2, tft.height() - 1, 1);
    }

    spr.pushSprite(0, 0);
}

// ── router ────────────────────────────────────────────────────────────────────

void draw_screen() {
    if (g_view == 0) draw_session();
    else             draw_overview();
}

// ── wifi ──────────────────────────────────────────────────────────────────────

void connect_wifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500); tries++;
    }
    g_wifi_ok = (WiFi.status() == WL_CONNECTED);
}

// ── fetch ─────────────────────────────────────────────────────────────────────

void fetch_usage() {
    if (WiFi.status() != WL_CONNECTED) {
        g_wifi_ok = false; g_fetch_ok = false; return;
    }
    g_wifi_ok = true;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", SERVER_HOST, SERVER_PORT, SERVER_PATH);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);
    int code = http.GET();
    if (code != 200) { g_fetch_ok = false; http.end(); return; }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) { g_fetch_ok = false; return; }

    g_session_pct       = doc["percentage"].as<float>();
    g_weekly_pct        = doc["weekly"].as<float>();
    g_design_pct        = doc["design"].as<float>();
    g_credits_pct       = doc["credits_pct"].as<float>();
    g_credits_used      = doc["credits_used"].as<int>();
    g_credits_limit     = doc["credits_limit"].as<int>();
    strlcpy(g_credits_currency,
            doc["credits_currency"] | "",
            sizeof(g_credits_currency));
    g_session_resets_in = doc["session_resets_in"].as<uint32_t>();
    g_weekly_resets_in  = doc["weekly_resets_in"].as<uint32_t>();
    g_fetch_ms          = millis();
    g_fetch_ok          = true;

    Serial.printf("Sessie:%.1f%% Week:%.1f%% Design:%.1f%% Credits:%.1f%%\n",
                  g_session_pct, g_weekly_pct, g_design_pct, g_credits_pct);
}

// ── setup & loop ──────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    pinMode(BTN1_PIN, INPUT_PULLUP);
    pinMode(BTN2_PIN, INPUT_PULLUP);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(1);
    spr.createSprite(tft.width(), tft.height());
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COLOR_TITLE, COLOR_BG);
    tft.drawString("Connecting...", tft.width() / 2, 60, 2);

    connect_wifi();
    fetch_usage();
    draw_screen();
    g_last_poll = g_last_draw = millis();
}

void loop() {
    unsigned long now = millis();

    // Beide knoppen wisselen de pagina
    static bool btn1_prev = HIGH, btn2_prev = HIGH;
    bool btn1_now = digitalRead(BTN1_PIN);
    bool btn2_now = digitalRead(BTN2_PIN);
    if ((btn1_prev == HIGH && btn1_now == LOW) ||
        (btn2_prev == HIGH && btn2_now == LOW)) {
        delay(40);
        if (digitalRead(BTN1_PIN) == LOW || digitalRead(BTN2_PIN) == LOW) {
            g_view = 1 - g_view;
            draw_screen();
            g_last_draw = now;
        }
    }
    btn1_prev = btn1_now;
    btn2_prev = btn2_now;

    if (WiFi.status() != WL_CONNECTED) connect_wifi();

    if (now - g_last_poll >= POLL_INTERVAL_MS) {
        g_last_poll = now;
        fetch_usage();
        draw_screen();
        g_last_draw = now;
    }

    // Afteller: alleen de cijfers hertekenen (geen flicker)
    if (g_view == 0 && g_session_pct >= 99.5f && now - g_last_draw >= 1000) {
        update_countdown();
        g_last_draw = now;
    }

    delay(50);
}
