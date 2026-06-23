// ─────────────────────────────────────────────────────────────────────────────
//  DisplayTask.cpp  —  TFT (240×320) + OLED (128×32)
//  Layout: Session Timer (hero) + Distraction Score + stats grid
//  No compass. Focus on productivity metrics.
// ─────────────────────────────────────────────────────────────────────────────
#include "Config.h"
#include "NavState.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ── Pin assignments (matching Map project) ────────────────────────────────────
#define TFT_CS    10
#define TFT_DC    14
#define TFT_RST   21
#define TFT_MOSI  11
#define TFT_CLK   12

// ── Display objects ───────────────────────────────────────────────────────────
static Adafruit_ST7789  tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST);
static GFXcanvas16      canvas(240, 320);
static Adafruit_SSD1306 oled(128, 32, &Wire, -1);

// ── Colour palette ────────────────────────────────────────────────────────────
//  Dark background, single teal/green accent, muted secondary text
#define C_BG        0x0041       // #070912 — near-black blue-tinted bg
#define C_SURFACE   0x08A4       // #0d1520 — card surface
#define C_ACCENT    0x1CEE       // #1D9E75 — teal green (brand accent)
#define C_TEXT      0xB61A       // #B4C0D0 — primary text
#define C_DIM       0x42AC       // #334466 — secondary / label text
#define C_WHITE     0xEF7E
#define C_WARN      0xD506       // amber for score > 60
#define C_RED       0xC208       // red for score > 80
#define C_DARK_GRN  0x08C2       // darker accent for filled progress bg

// ── Layout constants ──────────────────────────────────────────────────────────
#define W   240
#define H   320
#define PAD 6

// ── Helpers ───────────────────────────────────────────────────────────────────
// Format seconds → "MM:SS"
static void fmt_mmss(uint32_t secs, char* buf, size_t len) {
    uint32_t m = secs / 60;
    uint32_t s = secs % 60;
    snprintf(buf, len, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
}

// Draw a filled rounded-rect card
static void card(int16_t x, int16_t y, int16_t w, int16_t h) {
    canvas.fillRoundRect(x, y, w, h, 4, C_SURFACE);
}

// Draw a thin horizontal progress bar
static void progress_bar(int16_t x, int16_t y, int16_t w, int16_t h,
                         float pct, uint16_t col) {
    canvas.fillRoundRect(x, y, w, h, 2, C_DARK_GRN);
    int16_t filled = (int16_t)(pct * w);
    if (filled > 0)
        canvas.fillRoundRect(x, y, filled, h, 2, col);
}

// Small label (font size 1, dim colour)
static void label(const char* txt, int16_t x, int16_t y) {
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(x, y);
    canvas.print(txt);
}

// Pomo dots e.g. "●●●○" up to 4
static void pomo_dots(int16_t x, int16_t y, uint8_t done) {
    for (int i = 0; i < 4; i++) {
        uint16_t col = (i < done) ? C_ACCENT : C_DIM;
        canvas.fillCircle(x + i * 9, y, 3, col);
    }
}

// Score colour based on value
static uint16_t score_colour(uint8_t s) {
    if (s > 80) return C_RED;
    if (s > 60) return C_WARN;
    return C_ACCENT;
}

// ── TFT sections ──────────────────────────────────────────────────────────────

// 1. Status bar (top, 20 px)
static void draw_statusbar(const NavState& st) {
    canvas.fillRect(0, 0, W, 20, C_SURFACE);

    // "DeskMon" label
    canvas.setTextSize(1);
    canvas.setTextColor(C_ACCENT);
    canvas.setCursor(PAD, 6);
    canvas.print("DeskMon");

    // uptime hh:mm
    char up[8];
    uint32_t h_u = st.uptime_sec / 3600;
    uint32_t m_u = (st.uptime_sec % 3600) / 60;
    snprintf(up, sizeof(up), "%02lu:%02lu", (unsigned long)h_u, (unsigned long)m_u);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(W / 2 - 12, 6);
    canvas.print(up);

    // WiFi indicator
    canvas.setTextColor(st.wifi.connected ? C_ACCENT : C_RED);
    canvas.setCursor(W - 24, 6);
    canvas.print(st.wifi.connected ? "WiFi" : "----");
}

// 2. Session hero (y=24..130, 106 px tall)
static void draw_session_hero(const NavState& st) {
    const int16_t y0 = 24, ht = 106;
    card(PAD, y0, W - PAD*2, ht);

    bool active = st.session.active;
    uint32_t elapsed = st.session.elapsed_sec;
    uint32_t dur     = st.session.session_duration;
    uint32_t remain  = (elapsed < dur) ? (dur - elapsed) : 0;

    // Phase label
    label(active ? "focus session" : "session paused", PAD+6, y0+6);

    // Big timer
    char tbuf[8];
    fmt_mmss(remain, tbuf, sizeof(tbuf));
    canvas.setTextSize(3);           // ~18×21 px per char
    canvas.setTextColor(C_WHITE);
    canvas.setCursor(PAD+6, y0+18);
    canvas.print(tbuf);

    // Pomodoro count + dots
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(PAD+6, y0+54);
    canvas.printf("pomo %d", st.session.pomodoro_count);
    pomo_dots(PAD+54, y0+59, st.session.pomodoro_count % 4);

    // Progress bar
    float pct = (dur > 0) ? (float)elapsed / dur : 0;
    progress_bar(PAD+6, y0+70, W - PAD*2 - 12, 4, pct, C_ACCENT);

    // Elapsed / target
    char el[8], tg[8];
    fmt_mmss(elapsed, el, sizeof(el));
    fmt_mmss(dur,     tg, sizeof(tg));
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(PAD+6, y0+80);
    canvas.printf("%s / %s", el, tg);
}

// 3. Distraction score (y=136..196, 60 px)
static void draw_distraction(const NavState& st) {
    const int16_t y0 = 136, ht = 60;
    card(PAD, y0, W - PAD*2, ht);

    uint8_t score = st.session.distraction_score;
    uint16_t col  = score_colour(score);

    label("distraction score", PAD+6, y0+6);

    // Number (large)
    char sbuf[5];
    snprintf(sbuf, sizeof(sbuf), "%d", score);
    canvas.setTextSize(3);
    canvas.setTextColor(col);
    canvas.setCursor(PAD+6, y0+18);
    canvas.print(sbuf);

    // Qualifier text
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(PAD+42, y0+26);
    if (score < 30)      canvas.print("calm");
    else if (score < 60) canvas.print("mild distraction");
    else if (score < 80) canvas.print("noisy");
    else                 canvas.print("high interference");

    // Bar
    progress_bar(PAD+6, y0+48, W - PAD*2 - 12, 4,
                 score / 100.0f, col);
}

// 4. Stats grid (y=202..300, 4 cells 2×2)
static void draw_stats_grid(const NavState& st) {
    const int16_t y0 = 202;
    const int16_t cw = (W - PAD*2 - 4) / 2;   // cell width
    const int16_t ch = 46;                       // cell height

    // positions: [col][row]
    int16_t xs[2] = { PAD,        PAD + cw + 4 };
    int16_t ys[2] = { y0,         y0 + ch + 4  };

    // ── cell 0: Motion + Vibration ────────────────────────────────────────
    card(xs[0], ys[0], cw, ch);
    label("motion", xs[0]+5, ys[0]+5);
    canvas.setTextSize(1);
    canvas.setTextColor(st.motion.moving ? C_WARN : C_ACCENT);
    canvas.setCursor(xs[0]+5, ys[0]+16);
    canvas.print(st.motion.moving ? "moving" : "still");
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(xs[0]+5, ys[0]+30);
    canvas.printf("vib %.3f", st.motion.vibration_rms);

    // ── cell 1: EM Field ──────────────────────────────────────────────────
    card(xs[1], ys[0], cw, ch);
    label("EM field", xs[1]+5, ys[0]+5);
    bool em_noisy = st.motion.em_variance > EM_SPIKE_THRESHOLD;
    canvas.setTextSize(1);
    canvas.setTextColor(em_noisy ? C_WARN : C_ACCENT);
    canvas.setCursor(xs[1]+5, ys[0]+16);
    canvas.print(em_noisy ? "noisy" : "stable");
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(xs[1]+5, ys[0]+30);
    canvas.printf("var %.1f", st.motion.em_variance);

    // ── cell 2: Tilt ──────────────────────────────────────────────────────
    card(xs[0], ys[1], cw, ch);
    label("tilt", xs[0]+5, ys[1]+5);
    canvas.setTextSize(2);
    canvas.setTextColor(C_TEXT);
    canvas.setCursor(xs[0]+5, ys[1]+16);
    canvas.printf("%.1f", fabsf(st.orient.pitch));
    canvas.setTextSize(1);
    canvas.print(" deg");
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(xs[0]+5, ys[1]+34);
    canvas.printf("roll %.1f", st.orient.roll);

    // ── cell 3: Impacts today ─────────────────────────────────────────────
    card(xs[1], ys[1], cw, ch);
    label("impacts", xs[1]+5, ys[1]+5);
    canvas.setTextSize(2);
    canvas.setTextColor(
        st.motion.impact_count > 10 ? C_WARN : C_TEXT);
    canvas.setCursor(xs[1]+5, ys[1]+16);
    canvas.printf("%lu", (unsigned long)st.motion.impact_count);
    canvas.setTextSize(1);
    canvas.setTextColor(C_DIM);
    canvas.setCursor(xs[1]+5, ys[1]+34);
    canvas.printf("EM spk %lu", (unsigned long)st.motion.em_spike_count);
}

// ─────────────────────────────────────────────────────────────────────────────
//  OLED  128×32 —  session time (left) | score + motion (right)
// ─────────────────────────────────────────────────────────────────────────────
static void draw_oled(const NavState& st) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    uint32_t remain = 0;
    if (st.session.active && st.session.elapsed_sec < st.session.session_duration)
        remain = st.session.session_duration - st.session.elapsed_sec;

    // Left: big timer
    char tbuf[8];
    fmt_mmss(remain, tbuf, sizeof(tbuf));
    oled.setTextSize(2);
    oled.setCursor(0, 0);
    oled.print(tbuf);

    // Sub-label
    oled.setTextSize(1);
    oled.setCursor(0, 24);
    oled.printf("P%d %s", st.session.pomodoro_count,
                st.session.active ? "on" : "off");

    // Divider
    oled.drawFastVLine(72, 2, 28, SSD1306_WHITE);

    // Right: score
    oled.setTextSize(1);
    oled.setCursor(76, 2);
    oled.printf("scr %d", st.session.distraction_score);

    // Motion state
    oled.setCursor(76, 20);
    oled.print(st.motion.moving ? "moving" : "still ");

    oled.display();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────
bool display_init() {
    tft.init(240, 320, SPI_MODE3);
    tft.setSPISpeed(40000000);
    tft.setRotation(0);
    tft.fillScreen(ST77XX_BLACK);
    tft.invertDisplay(false);

    if (!oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED)) {
        log_e("OLED init failed — continuing without OLED");
    }

    log_i("Displays initialised");
    return true;
}

void DisplayTask(void* pvParams) {
    for (;;) {
        NavState st = state_snapshot();

        canvas.fillScreen(C_BG);
        draw_statusbar(st);
        draw_session_hero(st);
        draw_distraction(st);
        draw_stats_grid(st);

        tft.drawRGBBitmap(0, 0, canvas.getBuffer(), W, H);

        draw_oled(st);

        vTaskDelay(pdMS_TO_TICKS(33));  // ~30 FPS
    }
}
