// tdisplay_display.h - LILYGO T-Display (classic ESP32) display helpers
#pragma once
#if defined(USE_LILYGO_T_DISPLAY)

#include <LovyanGFX.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>

#define TDISP_PIN_BL 4
#define TDISP_BTN_BRIGHT 0
#define TDISP_BTN_SCAN 35

class TDisplayGfx : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
public:
    TDisplayGfx() {
        auto bus = _bus.config();
        bus.spi_host = SPI2_HOST;
        bus.spi_mode = 0;
        bus.freq_write = 40000000;
        bus.freq_read = 16000000;
        bus.spi_3wire = true;
        bus.use_lock = true;
        bus.dma_channel = 1;
        bus.pin_sclk = 18;
        bus.pin_mosi = 19;
        bus.pin_miso = -1;
        bus.pin_dc = 16;
        _bus.config(bus);
        _panel.setBus(&_bus);

        auto panel = _panel.config();
        panel.pin_cs = 5;
        panel.pin_rst = 23;
        panel.pin_busy = -1;
        panel.memory_width = 240;
        panel.memory_height = 320;
        panel.panel_width = 135;
        panel.panel_height = 240;
        panel.offset_x = 52;
        panel.offset_y = 40;
        panel.offset_rotation = 1;
        panel.readable = false;
        panel.invert = true;
        panel.rgb_order = false;
        panel.dlen_16bit = false;
        panel.bus_shared = true;
        _panel.config(panel);

        auto light = _light.config();
        light.pin_bl = TDISP_PIN_BL;
        light.invert = false;
        light.freq = 44100;
        light.pwm_channel = 7;
        _light.config(light);
        _panel.setLight(&_light);
        setPanel(&_panel);
    }
};

static constexpr uint16_t TD_BLACK = 0x0000;
static constexpr uint16_t TD_WHITE = 0xFFFF;
static constexpr uint16_t TD_RED = 0xF800;
static constexpr uint16_t TD_GREEN = 0x07E0;
static constexpr uint16_t TD_YELLOW = 0xFFE0;
static constexpr uint16_t TD_CYAN = 0x07FF;
static constexpr uint16_t TD_ORANGE = 0xFD20;
static constexpr uint16_t TD_DK_RED = 0x8000;
static constexpr uint16_t TD_DK_AMB = 0x8280;
static constexpr uint16_t TD_DK_GRN = 0x0320;
static constexpr uint16_t TD_GREY = 0x8410;
static constexpr uint16_t TD_LT_GREY = 0xC618;
static constexpr uint16_t TD_DK_GREY = 0x2104;
static constexpr int TD_W = 240;
static constexpr int TD_H = 135;
static constexpr int TD_HDR = 20;
static constexpr int TD_FTR = 118;
static constexpr unsigned long TD_ALERT_HOLD_MS = 15000UL;

static TDisplayGfx tdTft;
static const uint8_t td_levels[5] = { 10, 25, 50, 75, 100 };
static uint8_t td_levelIdx = 3;
static unsigned long td_levelShownAtMs = 0;
static bool td_alertActive = false;
static unsigned long td_alertAtMs = 0;
static bool td_alertNeedsDraw = false;
static unsigned long td_alertLastPulseAtMs = 0;
static unsigned long td_alertLastCountdownSecond = 0xFFFFFFFFUL;
static bool td_scanScreenDrawn = false;
static bool td_channelDrawn = false;
static uint8_t td_drawnChannel = 0;
static bool td_modeDrawn = false;
static char td_drawnMode[12] = {0};
static bool td_statsDrawn = false;
static int td_drawnDetCount = 0;
static bool td_drawnSpiffsOk = false;
static bool td_drawnScanning = false;
static char td_scanHeaderStatus[16] = {0};
static char td_method[16] = {0};
static char td_mac[18] = {0};
static char td_ssid[33] = {0};
static char td_type[6] = {0};
static uint8_t td_conf = 0;
static int8_t td_rssi = 0;
static uint8_t td_ch = 0;

static void td_applyBrightness() {
    unsigned value = (unsigned)td_levels[td_levelIdx] * 255u / 100u;
    tdTft.setBrightness((uint8_t)(value > 254 ? 254 : value));
}

static void td_text(int x, int y, const char* text, uint16_t color,
                    const lgfx::IFont* font, uint8_t datum = top_left) {
    tdTft.setFont(font);
    tdTft.setTextColor(color);
    tdTft.setTextDatum(datum);
    tdTft.drawString(text, x, y);
}

static uint16_t td_rssiColor(int8_t rssi) {
    if (rssi > -55) return TD_GREEN;
    if (rssi > -65) return 0x37E0;
    if (rssi > -75) return TD_YELLOW;
    if (rssi > -85) return TD_ORANGE;
    return TD_RED;
}

static void td_header(const char* status, uint16_t color) {
    tdTft.fillRect(0, 0, TD_W, TD_HDR, color);
    td_text(5, 2, "FLOCK-YOU", TD_WHITE, &fonts::Font2);
    td_text(TD_W - 5, 2, status, TD_WHITE, &fonts::Font2, top_right);
}

static void td_updateAlertHeader(unsigned long now) {
    bool pulse = ((now / 400) & 1) != 0;
    uint16_t headerColor = td_conf >= 60 ? (pulse ? TD_RED : TD_DK_RED)
                          : td_conf >= 30 ? TD_DK_AMB : 0x0010;
    const char* status = td_conf >= 60 ? "FLOCK MATCH"
                        : td_conf >= 30 ? "PROBABLE" : "WEAK MATCH";
    td_header(status, headerColor);
}

static void td_updateAlertCountdown(unsigned long now) {
    unsigned long left = (TD_ALERT_HOLD_MS > now - td_alertAtMs)
                       ? (TD_ALERT_HOLD_MS - (now - td_alertAtMs)) / 1000 + 1 : 0;
    char line[40];
    snprintf(line, sizeof(line), "Scanning resumes in %lus", left);
    tdTft.fillRect(0, TD_FTR, TD_W, TD_H - TD_FTR, TD_DK_GREY);
    td_text(5, TD_FTR + 4, line, TD_LT_GREY, &fonts::Font0);
}

static void td_drawAlert(unsigned long now) {
    tdTft.fillScreen(TD_BLACK);
    td_updateAlertHeader(now);
    td_text(6, 25, td_method, TD_YELLOW, &fonts::Font2);
    const char* detail = strcmp(td_type, "SSID") == 0 ? "Wi-Fi name pattern"
                       : strcmp(td_type, "BLE") == 0 ? "Bluetooth LE signature"
                       : "Hardware vendor signature";
    td_text(6, 43, detail, TD_WHITE, &fonts::Font2);

    char line[48];
    snprintf(line, sizeof(line), "MAC %s", td_mac);
    td_text(6, 62, line, TD_LT_GREY, &fonts::Font0);
    if (td_ssid[0]) snprintf(line, sizeof(line), "SSID %.24s", td_ssid);
    else snprintf(line, sizeof(line), "Channel %u", (unsigned)td_ch);
    td_text(6, 76, line, TD_CYAN, &fonts::Font2);

    int bars = td_rssi > -55 ? 5 : td_rssi > -65 ? 4 : td_rssi > -75 ? 3
             : td_rssi > -85 ? 2 : 1;
    uint16_t signalColor = td_rssiColor(td_rssi);
    for (int i = 0; i < 5; i++) {
        int height = 4 + i * 3;
        tdTft.fillRect(174 + i * 10, 105 - height, 7, height,
                       i < bars ? signalColor : TD_DK_GREY);
    }
    snprintf(line, sizeof(line), "RSSI %d dBm", (int)td_rssi);
    td_text(TD_W - 5, 78, line, TD_LT_GREY, &fonts::Font0, top_right);

    tdTft.fillRect(6, 101, 150, 7, TD_DK_GREY);
    tdTft.fillRect(6, 101, (int)(150L * td_conf / 100), 7, signalColor);
    snprintf(line, sizeof(line), "%u%% confidence", (unsigned)td_conf);
    td_text(164, 98, line, TD_WHITE, &fonts::Font0);
    td_updateAlertCountdown(now);
    td_alertLastCountdownSecond = (now - td_alertAtMs) / 1000;
    td_alertLastPulseAtMs = now;
}

static void td_drawRadar(unsigned long now, bool scanning) {
    const int cx = 49, cy = 72, radius = 38;
    tdTft.fillRect(cx - radius - 1, cy - radius - 1,
                   2 * radius + 3, 2 * radius + 3, TD_BLACK);
    for (int r = 12; r <= radius; r += 12) tdTft.drawCircle(cx, cy, r, TD_DK_GREY);
    tdTft.drawFastHLine(cx - radius, cy, radius * 2 + 1, TD_DK_GREY);
    tdTft.drawFastVLine(cx, cy - radius, radius * 2 + 1, TD_DK_GREY);
    if (scanning) {
        float angle = (float)((now / 12) % 360) * (float)M_PI / 180.0f;
        tdTft.drawLine(cx, cy, cx + (int)(radius * cosf(angle)),
                       cy + (int)(radius * sinf(angle)), TD_GREEN);
        tdTft.fillCircle(cx, cy, 2, TD_GREEN);
    } else {
        tdTft.fillRect(cx - 7, cy - 8, 5, 16, TD_ORANGE);
        tdTft.fillRect(cx + 2, cy - 8, 5, 16, TD_ORANGE);
    }
}

static void td_drawScanBase(unsigned long now, bool scanning) {
    tdTft.fillScreen(TD_BLACK);
    const char* status = scanning ? "SCANNING" : "PAUSED";
    uint16_t headerColor = scanning ? TD_DK_GRN : TD_DK_AMB;
    if (td_levelShownAtMs && now - td_levelShownAtMs < 1500) {
        snprintf(td_scanHeaderStatus, sizeof(td_scanHeaderStatus), "%u%%",
                 (unsigned)td_levels[td_levelIdx]);
        status = td_scanHeaderStatus;
    } else {
        strncpy(td_scanHeaderStatus, status, sizeof(td_scanHeaderStatus) - 1);
        td_scanHeaderStatus[sizeof(td_scanHeaderStatus) - 1] = '\0';
    }
    td_header(status, headerColor);

    if (scanning) {
#if defined(ENABLE_BLE_SCAN) && ENABLE_BLE_SCAN
        td_text(101, 82, "Capture Wi-Fi + BLE", TD_GREY, &fonts::Font0);
#else
        td_text(101, 82, "Capture nearby Wi-Fi", TD_GREY, &fonts::Font0);
#endif
        td_text(101, 96, "Compare camera signatures", TD_GREY, &fonts::Font0);
    } else {
        td_text(101, 31, "Scanning paused", TD_ORANGE, &fonts::Font2);
        td_text(101, 59, "Radios are idle.", TD_WHITE, &fonts::Font0);
        td_text(101, 74, "Press GPIO35 to", TD_WHITE, &fonts::Font0);
        td_text(101, 88, "resume scanning.", TD_WHITE, &fonts::Font0);
    }
        tdTft.fillRect(0, TD_FTR, TD_W, TD_H - TD_FTR, TD_DK_GREY);
        td_text(TD_W - 5, TD_FTR + 4, "GPIO35:scan GPIO0:light",
            TD_LT_GREY, &fonts::Font0, top_right);
}

static void td_updateChannel(uint8_t channel) {
    tdTft.fillRect(98, 23, 140, 36, TD_BLACK);
    char line[36];
    snprintf(line, sizeof(line), "Listening: ch %u", (unsigned)channel);
    td_text(101, 27, line, TD_WHITE, &fonts::Font2);
    snprintf(line, sizeof(line), "%u MHz  2.4 GHz", 2407u + 5u * channel);
    td_text(101, 48, line, TD_LT_GREY, &fonts::Font0);
}

static void td_updateMode(const char* mode) {
    tdTft.fillRect(98, 61, 140, 17, TD_BLACK);
    const char* modeText = mode && strcmp(mode, "SINGLE") == 0 ? "Single channel"
                         : mode && strcmp(mode, "CUSTOM") == 0 ? "Chosen channel list"
                         : "Channel hopping";
    td_text(101, 64, modeText, TD_CYAN, &fonts::Font0);
}

static void td_updateStats(int detCount, bool spiffsOk) {
    tdTft.fillRect(0, TD_FTR, 105, TD_H - TD_FTR, TD_DK_GREY);
    char line[40];
    snprintf(line, sizeof(line), "Det:%d  Log:%s", detCount, spiffsOk ? "OK" : "ERR");
    td_text(5, TD_FTR + 4, line, TD_LT_GREY, &fonts::Font0);
}

static void td_updateScanHeader(unsigned long now, bool scanning) {
    char status[16];
    uint16_t color = scanning ? TD_DK_GRN : TD_DK_AMB;
    if (td_levelShownAtMs && now - td_levelShownAtMs < 1500) {
        snprintf(status, sizeof(status), "%u%%", (unsigned)td_levels[td_levelIdx]);
    } else {
        strncpy(status, scanning ? "SCANNING" : "PAUSED", sizeof(status) - 1);
        status[sizeof(status) - 1] = '\0';
    }
    if (strcmp(status, td_scanHeaderStatus) != 0) {
        td_header(status, color);
        strncpy(td_scanHeaderStatus, status, sizeof(td_scanHeaderStatus) - 1);
        td_scanHeaderStatus[sizeof(td_scanHeaderStatus) - 1] = '\0';
    }
}

static void td_drawScan(uint8_t channel, const char* mode, int detCount,
                        unsigned long now, bool spiffsOk, bool scanning) {
    bool redraw = !td_scanScreenDrawn || td_drawnScanning != scanning;
    if (redraw) {
        td_drawScanBase(now, scanning);
        td_scanScreenDrawn = true;
        td_drawnScanning = scanning;
        td_channelDrawn = false;
        td_modeDrawn = false;
        td_statsDrawn = false;
    } else {
        td_updateScanHeader(now, scanning);
    }

    if (scanning && (!td_channelDrawn || td_drawnChannel != channel)) {
        td_updateChannel(channel);
        td_drawnChannel = channel;
        td_channelDrawn = true;
    }
    const char* currentMode = mode ? mode : "";
    if (scanning && (!td_modeDrawn || strcmp(td_drawnMode, currentMode) != 0)) {
        td_updateMode(currentMode);
        strncpy(td_drawnMode, currentMode, sizeof(td_drawnMode) - 1);
        td_drawnMode[sizeof(td_drawnMode) - 1] = '\0';
        td_modeDrawn = true;
    }
    if (!td_statsDrawn || td_drawnDetCount != detCount || td_drawnSpiffsOk != spiffsOk) {
        td_updateStats(detCount, spiffsOk);
        td_drawnDetCount = detCount;
        td_drawnSpiffsOk = spiffsOk;
        td_statsDrawn = true;
    }
    if (scanning || redraw) td_drawRadar(now, scanning);
}

static void tdisplayInit() {
    tdTft.init();
    tdTft.setRotation(0);
    td_applyBrightness();
    tdTft.fillScreen(TD_BLACK);
    pinMode(TDISP_BTN_BRIGHT, INPUT_PULLUP);
    pinMode(TDISP_BTN_SCAN, INPUT);
    td_text(TD_W / 2, 31, "FLOCK-YOU", TD_CYAN, &fonts::Font4, middle_center);
    td_text(TD_W / 2, 68, "Camera signal detector", TD_LT_GREY, &fonts::Font2, middle_center);
    td_text(TD_W / 2, 93, "Starting Wi-Fi radio...", TD_GREY, &fonts::Font0, middle_center);
}

static void tdisplayDetection(const char* method, const char* mac, uint8_t confidence,
                              int8_t rssi, uint8_t channel, const char* ssid,
                              const char* type) {
    strncpy(td_method, method ? method : "?", sizeof(td_method) - 1);
    td_method[sizeof(td_method) - 1] = '\0';
    strncpy(td_mac, mac ? mac : "", sizeof(td_mac) - 1);
    td_mac[sizeof(td_mac) - 1] = '\0';
    strncpy(td_ssid, ssid ? ssid : "", sizeof(td_ssid) - 1);
    td_ssid[sizeof(td_ssid) - 1] = '\0';
    strncpy(td_type, type ? type : "", sizeof(td_type) - 1);
    td_type[sizeof(td_type) - 1] = '\0';
    td_conf = confidence;
    td_rssi = rssi;
    td_ch = channel;
    td_alertAtMs = millis();
    td_alertActive = true;
    td_alertNeedsDraw = true;
    td_alertLastCountdownSecond = 0xFFFFFFFFUL;
    td_scanScreenDrawn = false;
}

static void tdisplayScanning(uint8_t channel, const char* mode, int detCount,
                             unsigned long now, bool spiffsOk, bool scanning) {
    if (td_alertActive && now - td_alertAtMs >= TD_ALERT_HOLD_MS) {
        td_alertActive = false;
        td_scanScreenDrawn = false;
    }
    if (td_alertActive) {
        if (td_alertNeedsDraw) {
            td_drawAlert(now);
            td_alertNeedsDraw = false;
        } else {
            if (now - td_alertLastPulseAtMs >= 400UL) {
                td_updateAlertHeader(now);
                td_alertLastPulseAtMs = now;
            }
            unsigned long elapsedSecond = (now - td_alertAtMs) / 1000;
            if (elapsedSecond != td_alertLastCountdownSecond) {
                td_updateAlertCountdown(now);
                td_alertLastCountdownSecond = elapsedSecond;
            }
        }
        return;
    }
    td_drawScan(channel, mode, detCount, now, spiffsOk, scanning);
}

static bool td_edge(uint8_t pin, bool& last, unsigned long& changedAt, unsigned long now) {
    bool current = digitalRead(pin);
    if (current != last && now - changedAt > 50) {
        last = current;
        changedAt = now;
        return !current;
    }
    return false;
}

static int tdisplayButtonTick(unsigned long now) {
    static bool brightnessLast = true, scanLast = true;
    static unsigned long brightnessAt = 0, scanAt = 0;
    if (td_edge(TDISP_BTN_BRIGHT, brightnessLast, brightnessAt, now)) {
        td_levelIdx = (td_levelIdx + 1) % 5;
        td_applyBrightness();
        td_levelShownAtMs = now ? now : 1;
    }
    return td_edge(TDISP_BTN_SCAN, scanLast, scanAt, now) ? 4 : 0;
}

#endif // USE_LILYGO_T_DISPLAY