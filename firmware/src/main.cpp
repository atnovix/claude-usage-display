#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "config.h"

TFT_eSPI tft = TFT_eSPI();

// Kleuren
#define COLOR_BG        TFT_BLACK
#define COLOR_TITLE     TFT_WHITE
#define COLOR_BAR_BG    0x2104  // donkergrijs
#define COLOR_BAR_LOW   0x07E0  // groen
#define COLOR_BAR_MID   0xFD20  // oranje
#define COLOR_BAR_HIGH  0xF800  // rood
#define COLOR_TEXT      TFT_WHITE
#define COLOR_DIM       0x7BEF  // lichtgrijs

static float  g_usage_pct    = 0.0f;
static bool   g_wifi_ok       = false;
static bool   g_fetch_ok      = false;
static char   g_limit_str[32] = "--";
static char   g_used_str[32]  = "--";
static unsigned long g_last_poll = 0;

// ── display helpers ──────────────────────────────────────────────────────────

uint16_t bar_color(float pct) {
    if (pct < 60.0f) return COLOR_BAR_LOW;
    if (pct < 85.0f) return COLOR_BAR_MID;
    return COLOR_BAR_HIGH;
}

void draw_screen() {
    tft.fillScreen(COLOR_BG);

    // Titel
    tft.setTextColor(COLOR_TITLE, COLOR_BG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("Claude Usage", tft.width() / 2, 8, 2);

    // Percentage groot
    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%.0f%%", g_usage_pct);
    tft.setTextColor(bar_color(g_usage_pct), COLOR_BG);
    tft.drawString(pct_str, tft.width() / 2, 38, 6);

    // Balk
    int bar_x = 10, bar_y = 110;
    int bar_w = tft.width() - 20, bar_h = 20;
    int fill_w = (int)((g_usage_pct / 100.0f) * bar_w);
    tft.fillRoundRect(bar_x, bar_y, bar_w, bar_h, 4, COLOR_BAR_BG);
    if (fill_w > 0)
        tft.fillRoundRect(bar_x, bar_y, fill_w, bar_h, 4, bar_color(g_usage_pct));

    // Subtekst
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    char sub[48];
    snprintf(sub, sizeof(sub), "%s / %s tokens", g_used_str, g_limit_str);
    tft.drawString(sub, tft.width() / 2, 140, 1);

    // Status onderin
    tft.setTextDatum(BC_DATUM);
    if (!g_wifi_ok) {
        tft.setTextColor(COLOR_BAR_HIGH, COLOR_BG);
        tft.drawString("Geen WiFi", tft.width() / 2, tft.height() - 4, 1);
    } else if (!g_fetch_ok) {
        tft.setTextColor(COLOR_BAR_MID, COLOR_BG);
        tft.drawString("Server niet bereikbaar", tft.width() / 2, tft.height() - 4, 1);
    } else {
        tft.setTextColor(COLOR_DIM, COLOR_BG);
        tft.drawString("OK", tft.width() / 2, tft.height() - 4, 1);
    }
}

// ── wifi ─────────────────────────────────────────────────────────────────────

void connect_wifi() {
    Serial.printf("WiFi verbinden met %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500);
        Serial.print(".");
        tries++;
    }

    g_wifi_ok = (WiFi.status() == WL_CONNECTED);
    if (g_wifi_ok) {
        Serial.printf("\nVerbonden! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi mislukt.");
    }
}

// ── fetch usage ───────────────────────────────────────────────────────────────

void fetch_usage() {
    if (WiFi.status() != WL_CONNECTED) {
        g_wifi_ok = false;
        g_fetch_ok = false;
        return;
    }
    g_wifi_ok = true;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d%s", SERVER_HOST, SERVER_PORT, SERVER_PATH);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("HTTP fout: %d\n", code);
        g_fetch_ok = false;
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    // Verwacht: {"percentage": 42.5, "used": 125000, "limit": 300000}
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("JSON fout: %s\n", err.c_str());
        g_fetch_ok = false;
        return;
    }

    g_usage_pct = doc["percentage"].as<float>();
    long used    = doc["used"].as<long>();
    long limit   = doc["limit"].as<long>();

    auto fmt_tokens = [](long n, char* buf, size_t sz) {
        if (n >= 1000000) snprintf(buf, sz, "%.1fM", n / 1000000.0f);
        else if (n >= 1000) snprintf(buf, sz, "%.1fK", n / 1000.0f);
        else snprintf(buf, sz, "%ld", n);
    };
    fmt_tokens(used,  g_used_str,  sizeof(g_used_str));
    fmt_tokens(limit, g_limit_str, sizeof(g_limit_str));

    g_fetch_ok = true;
    Serial.printf("Usage: %.1f%% (%s / %s)\n", g_usage_pct, g_used_str, g_limit_str);
}

// ── setup & loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // Backlight aan
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(1);  // landscape
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(TC_DATUM);

    tft.setTextColor(COLOR_TITLE, COLOR_BG);
    tft.drawString("Verbinden...", tft.width() / 2, 80, 2);

    connect_wifi();
    fetch_usage();
    draw_screen();

    g_last_poll = millis();
}

void loop() {
    unsigned long now = millis();

    // Herverbind wifi als verbinding weg is
    if (WiFi.status() != WL_CONNECTED) {
        connect_wifi();
    }

    if (now - g_last_poll >= POLL_INTERVAL_MS) {
        g_last_poll = now;
        fetch_usage();
        draw_screen();
    }

    delay(100);
}
