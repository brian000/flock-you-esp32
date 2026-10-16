// tdisplays3_display.h — LILYGO T-Display S3 (ESP32-S3R8) display helpers
//
// Pin map verified against LilyGO's examples/factory/pin_config.h and
// TFT_eSPI Setup206 (github.com/Xinyuan-LilyGO/T-Display-S3). Panel is a
// 170x320 window in the ST7789V's 240x320 RAM: offset_x = (240-170)/2 = 35.
// Hardware: ST7789 170x320 IPS on an 8-bit parallel bus, driven by LovyanGFX.
//   D0..D7 = 39,40,41,42,45,46,47,48   WR=8  RD=9  DC=7  CS=6  RST=5
//   Backlight=38 (PWM)   PWR_ON=15 (must be HIGH for battery operation)
//   Buttons: BOOT=GPIO0 (bottom) cycles brightness, KEY=GPIO14 (top) toggles scanning
//
// Rendering: landscape 320x170, drawn into a full-screen sprite and pushed
// once per frame so the animated screens do not flicker. Only the UI task
// (ui_task.h) may call anything in here after setup().
//
// Two screens, both written to explain what the device is doing:
//   - Scanning: radar animation, current channel/band, hop mode, and the
//     capture -> read -> compare -> alert pipeline with the active step lit.
//   - Alert:    which signature matched and why, signal strength, rough range,
//     confidence, and how long until the display returns to scanning.
#pragma once
#if defined(USE_LILYGO_T_DISPLAY_S3)

#include <LovyanGFX.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>

#define TDS3_PIN_PWR_ON 15
#define TDS3_PIN_BL     38
#define TDS3_BTN_PIN    0    // BOOT, bottom
#define TDS3_KEY_PIN    14   // top

class TDS3Gfx : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel;
    lgfx::Bus_Parallel8 _bus;
    lgfx::Light_PWM     _light;
public:
    TDS3Gfx() {
        {
            auto cfg = _bus.config();
            cfg.freq_write = 16000000;   // LilyGO factory example value; higher can cause mosaic
            cfg.pin_wr = 8;  cfg.pin_rd = 9;  cfg.pin_rs = 7;
            cfg.pin_d0 = 39; cfg.pin_d1 = 40; cfg.pin_d2 = 41; cfg.pin_d3 = 42;
            cfg.pin_d4 = 45; cfg.pin_d5 = 46; cfg.pin_d6 = 47; cfg.pin_d7 = 48;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 6;  cfg.pin_rst = 5;  cfg.pin_busy = -1;
            cfg.memory_width  = 240; cfg.memory_height = 320;
            cfg.panel_width   = 170; cfg.panel_height  = 320;
            cfg.offset_x = 35;       cfg.offset_y = 0;
            cfg.readable = false;    cfg.invert = true;
            cfg.rgb_order = false;   cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = TDS3_PIN_BL;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

// ── Palette (RGB565) ──────────────────────────────────────────────────────────
static constexpr uint16_t TD_BLACK   = 0x0000;
static constexpr uint16_t TD_WHITE   = 0xFFFF;
static constexpr uint16_t TD_RED     = 0xF800;
static constexpr uint16_t TD_GREEN   = 0x07E0;
static constexpr uint16_t TD_YELLOW  = 0xFFE0;
static constexpr uint16_t TD_CYAN    = 0x07FF;
static constexpr uint16_t TD_ORANGE  = 0xFD20;
static constexpr uint16_t TD_DK_RED  = 0x8000;
static constexpr uint16_t TD_DK_AMB  = 0x8280;
static constexpr uint16_t TD_DK_GRN  = 0x0320;
static constexpr uint16_t TD_GREY    = 0x8410;
static constexpr uint16_t TD_LT_GREY = 0xC618;
static constexpr uint16_t TD_DK_GREY = 0x2104;

static constexpr int TD_W   = 320;
static constexpr int TD_H   = 170;
static constexpr int TD_HDR = 22;
static constexpr int TD_FTR = 152;

// Must match UI_ALERT_HOLD_MS in ui_task.h (that header is included later).
static constexpr unsigned long TD_ALERT_HOLD_MS = 15000UL;

static TDS3Gfx          tdTft;
static lgfx::LGFX_Sprite tdSpr(&tdTft);
static bool             tdSprOk = false;

static const uint8_t    td_levels[5] = { 10, 25, 50, 75, 100 };   // brightness %; never 0 so the screen can't go dark
static uint8_t          td_levelIdx  = 3;
static unsigned long    td_levelShownAtMs = 0;

static void td_applyBrightness() {
    // 255 makes LovyanGFX's 9-bit LEDC duty come out as 512 (overflow -> dark), so cap at 254.
    unsigned v = (unsigned)td_levels[td_levelIdx] * 255u / 100u;
    tdTft.setBrightness((uint8_t)(v > 254 ? 254 : v));
}

// Alert state cached by tds3Detection()
static bool          td_alertActive = false;
static unsigned long td_alertAtMs   = 0;
static char          td_method[16]  = {0};
static char          td_mac[18]     = {0};
static char          td_ssid[33]    = {0};
static char          td_type[6]     = {0};
static uint8_t       td_conf        = 0;
static int8_t        td_rssi        = 0;
static uint8_t       td_ch          = 0;

// Draw target: the sprite when allocated, otherwise the panel directly.
static inline lgfx::LovyanGFX& tdG() {
    return tdSprOk ? (lgfx::LovyanGFX&)tdSpr : (lgfx::LovyanGFX&)tdTft;
}

static void td_fmtMs(unsigned long ms, char* buf, size_t len) {
    unsigned long s = ms / 1000, m = s / 60; s %= 60;
    unsigned long h = m / 60; m %= 60;
    if (h > 0) snprintf(buf, len, "%lu:%02lu:%02lu", h, m, s);
    else       snprintf(buf, len, "%lu:%02lu", m, s);
}

static void td_text(int x, int y, const char* s, uint16_t fg, const lgfx::IFont* f,
                    uint8_t datum = top_left) {
    auto& g = tdG();
    g.setFont(f);
    g.setTextColor(fg);
    g.setTextDatum(datum);
    g.drawString(s, x, y);
}

static void td_header(const char* title, const char* status, uint16_t bg) {
    auto& g = tdG();
    g.fillRect(0, 0, TD_W, TD_HDR, bg);
    td_text(6, 3, title, TD_WHITE, &fonts::Font2);
    td_text(TD_W - 6, 3, status, TD_WHITE, &fonts::Font2, top_right);
}

static void td_footer(const char* left, const char* right) {
    auto& g = tdG();
    g.fillRect(0, TD_FTR, TD_W, TD_H - TD_FTR, TD_DK_GREY);
    g.drawFastHLine(0, TD_FTR, TD_W, TD_GREY);
    td_text(6, TD_FTR + 5, left, TD_LT_GREY, &fonts::Font0);
    td_text(TD_W - 6, TD_FTR + 5, right, TD_LT_GREY, &fonts::Font0, top_right);
}

static uint16_t td_rssiColor(int8_t r) {
    if (r > -55) return TD_GREEN;
    if (r > -65) return 0x37E0;
    if (r > -75) return TD_YELLOW;
    if (r > -85) return TD_ORANGE;
    return TD_RED;
}
static int td_rssiBars(int8_t r) {
    if (r > -55) return 5;
    if (r > -65) return 4;
    if (r > -75) return 3;
    if (r > -85) return 2;
    return 1;
}
// Free-space path-loss estimate; a single receiver cannot truly triangulate.
static float td_estimateDistanceM(int8_t rssi) {
    return powf(10.0f, (-40.0f - (float)rssi) / 20.0f);
}

// ── Public API ────────────────────────────────────────────────────────────────

static void tds3Init() {
    pinMode(TDS3_PIN_PWR_ON, OUTPUT);
    digitalWrite(TDS3_PIN_PWR_ON, HIGH);

    tdTft.init();
    tdTft.setRotation(1);   // landscape 320x170
    td_applyBrightness();
    tdTft.fillScreen(TD_BLACK);
    pinMode(TDS3_BTN_PIN, INPUT_PULLUP);
    pinMode(TDS3_KEY_PIN, INPUT_PULLUP);

    tdSpr.setColorDepth(16);
    tdSpr.setPsram(true);
    tdSprOk = tdSpr.createSprite(TD_W, TD_H) != nullptr;

    auto& g = tdG();
    g.fillScreen(TD_BLACK);
    td_text(TD_W / 2, 40, "FLOCK-YOU", TD_CYAN, &fonts::Font4, middle_center);
    td_text(TD_W / 2, 74, "Surveillance-camera signal detector", TD_LT_GREY,
            &fonts::Font2, middle_center);
    td_text(TD_W / 2, 100, "Starting Wi-Fi radio...", TD_GREY, &fonts::Font2, middle_center);
    if (tdSprOk) tdSpr.pushSprite(0, 0);
}

// Cache an alert; tds3Scanning() renders it for TD_ALERT_HOLD_MS.
static void tds3Detection(const char* method, const char* mac, uint8_t conf,
                          int8_t rssi, uint8_t ch, const char* ssid, const char* type) {
    strncpy(td_method, method ? method : "?", sizeof(td_method) - 1);
    td_method[sizeof(td_method) - 1] = '\0';
    strncpy(td_mac, mac ? mac : "", sizeof(td_mac) - 1);
    td_mac[sizeof(td_mac) - 1] = '\0';
    strncpy(td_ssid, ssid ? ssid : "", sizeof(td_ssid) - 1);
    td_ssid[sizeof(td_ssid) - 1] = '\0';
    strncpy(td_type, type ? type : "", sizeof(td_type) - 1);
    td_type[sizeof(td_type) - 1] = '\0';
    td_conf = conf; td_rssi = rssi; td_ch = ch;
    td_alertAtMs   = millis();
    td_alertActive = true;
}

static void td_drawAlert(unsigned long now) {
    auto& g = tdG();
    bool pulse = ((now / 400) & 1) != 0;
    uint16_t hdrBg = (td_conf >= 60) ? (pulse ? TD_RED : TD_DK_RED)
                   : (td_conf >= 30) ? TD_DK_AMB : 0x0010;
    const char* title = (td_conf >= 60) ? "!! FLOCK CAMERA SIGNAL !!"
                      : (td_conf >= 30) ? "PROBABLE FLOCK DEVICE" : "WEAK MATCH";
    g.fillScreen(TD_BLACK);
    td_header(title, "", hdrBg);

    // Warning triangle
    uint16_t tri = (td_conf >= 60) ? TD_RED : (td_conf >= 30) ? TD_ORANGE : TD_YELLOW;
    g.fillTriangle(40, 34, 8, 92, 72, 92, tri);
    td_text(40, 76, "!", TD_BLACK, &fonts::Font4, middle_center);

    // What matched and why
    td_text(88, 28, td_method, TD_YELLOW, &fonts::Font4);
    const char *l1, *l2;
    if (strcmp(td_type, "SSID") == 0) {
        l1 = "Wi-Fi network name matches"; l2 = "a known Flock camera pattern.";
    } else if (strcmp(td_type, "BLE") == 0) {
        l1 = "Bluetooth LE advertisement"; l2 = "matches a Flock/Raven signature.";
    } else {
        l1 = "Device MAC vendor prefix"; l2 = "matches known Flock hardware.";
    }
    td_text(88, 58, l1, TD_WHITE, &fonts::Font2);
    td_text(88, 74, l2, TD_WHITE, &fonts::Font2);

    char buf[48];
    snprintf(buf, sizeof(buf), "MAC %s", td_mac);
    td_text(8, 100, buf, TD_LT_GREY, &fonts::Font2);
    if (td_ssid[0]) {
        snprintf(buf, sizeof(buf), "SSID \"%.24s\"", td_ssid);
        td_text(8, 116, buf, TD_CYAN, &fonts::Font2);
    } else {
        snprintf(buf, sizeof(buf), "Heard on channel %u", (unsigned)td_ch);
        td_text(8, 116, buf, TD_GREY, &fonts::Font2);
    }

    // Signal bars + range
    int nb = td_rssiBars(td_rssi);
    uint16_t sc = td_rssiColor(td_rssi);
    for (int i = 0; i < 5; i++) {
        int bh = 5 + i * 4;
        g.fillRect(232 + i * 12, 112 - bh, 9, bh, i < nb ? sc : TD_DK_GREY);
    }
    float d = td_estimateDistanceM(td_rssi);
    if (d >= 1000.0f)    snprintf(buf, sizeof(buf), "%ddBm  ~%.1fkm", (int)td_rssi, d / 1000.0f);
    else if (d >= 10.0f) snprintf(buf, sizeof(buf), "%ddBm  ~%.0fm", (int)td_rssi, d);
    else                 snprintf(buf, sizeof(buf), "%ddBm  ~%.1fm", (int)td_rssi, d);
    td_text(232, 116, buf, TD_LT_GREY, &fonts::Font2);

    // Confidence
    const char* cl = (td_conf >= 60) ? "High confidence"
                   : (td_conf >= 30) ? "Probable" : "Low - keep watching";
    int bw = (int)((long)200 * td_conf / 100);
    g.fillRect(8, 136, 200, 10, TD_DK_GREY);
    g.fillRect(8, 136, bw, 10, tri);
    snprintf(buf, sizeof(buf), "%u%%  %s", (unsigned)td_conf, cl);
    td_text(214, 134, buf, TD_WHITE, &fonts::Font2);

    unsigned long left = (TD_ALERT_HOLD_MS > now - td_alertAtMs)
                         ? (TD_ALERT_HOLD_MS - (now - td_alertAtMs)) / 1000 + 1 : 0;
    snprintf(buf, sizeof(buf), "Back to scanning in %lus", left);
    td_footer(buf, "range is a rough estimate");
}

static void td_drawScan(uint8_t ch, const char* mode, int detCount,
                        unsigned long now, bool spiffsOk, bool scanning) {
    auto& g = tdG();
    g.fillScreen(TD_BLACK);
    if (scanning) {
        td_header("FLOCK-YOU", "SCANNING", TD_DK_GRN);
        if ((now / 500) & 1) g.fillCircle(TD_W - 78, 11, 3, TD_GREEN);
    } else {
        td_header("FLOCK-YOU", "PAUSED", TD_DK_AMB);
    }

    // Radar: ring grid + rotating sweep with a fading trail
    const int cx = 62, cy = 90, R = 56;
    for (int r = R / 3; r <= R; r += R / 3) g.drawCircle(cx, cy, r, TD_DK_GREY);
    g.drawFastHLine(cx - R, cy, 2 * R + 1, TD_DK_GREY);
    g.drawFastVLine(cx, cy - R, 2 * R + 1, TD_DK_GREY);
    if (scanning) {
        float base = (float)((now / 12) % 360);
        for (int i = 24; i >= 0; i--) {
            float a = (base - i * 3.0f) * (float)M_PI / 180.0f;
            g.drawLine(cx, cy, cx + (int)(R * cosf(a)), cy + (int)(R * sinf(a)),
                       g.color565(0, 255 - i * 10, 0));
        }
        g.fillCircle(cx, cy, 2, TD_GREEN);
    } else {
        g.fillRect(cx - 9, cy - 10, 6, 20, TD_ORANGE);
        g.fillRect(cx + 3, cy - 10, 6, 20, TD_ORANGE);
    }

    if (!scanning) {
        td_text(132, 28, "Scanning paused", TD_ORANGE, &fonts::Font4);
        td_text(132, 62, "Radios are idle: no Wi-Fi or", TD_WHITE, &fonts::Font2);
        td_text(132, 78, "Bluetooth traffic is being", TD_WHITE, &fonts::Font2);
        td_text(132, 94, "checked, so nothing can be", TD_WHITE, &fonts::Font2);
        td_text(132, 110, "detected.", TD_WHITE, &fonts::Font2);
        td_text(132, 132, "Press TOP button to resume", TD_YELLOW, &fonts::Font2);
        char l[40];
        snprintf(l, sizeof(l), "Det:%d  Log:%s", detCount, spiffsOk ? "OK" : "ERR");
        td_footer(l, "TOP:resume BOOT:bright");
        return;
    }

    // Channel + band
    char buf[56];
    unsigned freq = (ch <= 14) ? 2407u + 5u * ch : 5000u + 5u * ch;
    snprintf(buf, sizeof(buf), "Listening: ch %u", (unsigned)ch);
    td_text(132, 28, buf, TD_WHITE, &fonts::Font4);
    snprintf(buf, sizeof(buf), "%u MHz (%s)", freq, ch <= 14 ? "2.4 GHz" : "5 GHz");
    td_text(132, 56, buf, TD_LT_GREY, &fonts::Font2);

    // 2.4 GHz channel strip with the current channel lit
    if (ch >= 1 && ch <= 14) {
        for (int c = 1; c <= 13; c++) {
            g.fillRect(132 + (c - 1) * 14, 76, 12, 6, c == ch ? TD_GREEN : TD_DK_GREY);
        }
    }

    // Hop mode in plain language
    const char* modeTxt = "Hopping through channels";
    if (mode) {
        if (strcmp(mode, "SINGLE") == 0)      modeTxt = "Parked on a single channel";
        else if (strcmp(mode, "CUSTOM") == 0) modeTxt = "Hopping a chosen channel list";
        else if (strcmp(mode, "FULL_HOP") == 0) modeTxt = "Hopping through every channel";
    }
    td_text(132, 86, modeTxt, TD_CYAN, &fonts::Font2);

    // Pipeline, active step cycles so it is clear the device is working
    static const char* const steps[4] = {
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
        "1 Capture Wi-Fi + Bluetooth LE",
#else
        "1 Capture nearby Wi-Fi frames",
#endif
        "2 Read sender MAC + network name",
        "3 Compare with Flock signatures",
        "4 Alert on a match",
    };
    int active = (int)((now / 700) % 4);
    for (int i = 0; i < 4; i++) {
        td_text(132, 104 + i * 12, steps[i], i == active ? TD_YELLOW : TD_GREY, &fonts::Font0);
    }

    char rt[12]; td_fmtMs(now, rt, sizeof(rt));
    char l[56];
    snprintf(l, sizeof(l), "Det:%d  Up:%s  Log:%s", detCount, rt, spiffsOk ? "OK" : "ERR");
    td_footer(l, "TOP:pause BOOT:bright");
}

// Called every UI tick (~20 Hz); renders the alert screen while it is held,
// otherwise the scanning screen.
static void tds3Scanning(uint8_t ch, const char* mode, int detCount,
                         unsigned long now, bool spiffsOk, bool scanning) {
    if (td_alertActive && (now - td_alertAtMs) >= TD_ALERT_HOLD_MS) td_alertActive = false;
    if (td_alertActive) td_drawAlert(now);
    else                td_drawScan(ch, mode, detCount, now, spiffsOk, scanning);

    if (td_levelShownAtMs && now - td_levelShownAtMs < 1500) {
        char b[24];
        snprintf(b, sizeof(b), "Brightness %u%%", (unsigned)td_levels[td_levelIdx]);
        auto& g = tdG();
        g.fillRoundRect(90, 70, 140, 30, 6, TD_BLACK);
        g.drawRoundRect(90, 70, 140, 30, 6, TD_CYAN);
        td_text(TD_W / 2, 85, b, TD_CYAN, &fonts::Font2, middle_center);
    }
    if (tdSprOk) tdSpr.pushSprite(0, 0);
}

static bool td_edge(uint8_t pin, bool& last, unsigned long& changedAt, unsigned long now) {
    bool cur = digitalRead(pin);
    if (cur != last && now - changedAt > 50) {
        last = cur; changedAt = now;
        return !cur;   // active low
    }
    return false;
}

// Polls both buttons. BOOT steps brightness here; returns 4 when the top
// button asks to toggle scanning (acted on by loop()), else 0.
static int tds3ButtonTick(unsigned long now) {
    static bool bootLast = true, keyLast = true;
    static unsigned long bootAt = 0, keyAt = 0;
    if (td_edge(TDS3_BTN_PIN, bootLast, bootAt, now)) {
        td_levelIdx = (td_levelIdx + 1) % 5;
        td_applyBrightness();
        td_levelShownAtMs = now ? now : 1;
    }
    return td_edge(TDS3_KEY_PIN, keyLast, keyAt, now) ? 4 : 0;
}

#endif // USE_LILYGO_T_DISPLAY_S3
