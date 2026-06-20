#include "Config.h"
#include "NavState.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_SSD1306.h>

// ── PINS (Matching your working Map Project) ─────────────────────────────────────
#define TFT_CS    10
#define TFT_DC    14
#define TFT_RST   21
#define TFT_MOSI  11
#define TFT_CLK   12

// ── OBJECTS ──────────────────────────────────────────────────────────────────
// Initialize display using the Software SPI constructor (same as Map project)
static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST);

// Double-buffer (Canvas) to prevent flickering
static GFXcanvas16 canvas(240, 320);

// OLED 128×32 SSD1306
static Adafruit_SSD1306 s_oled(128, 32, &Wire, -1);

// ── COLOUR PALETTE (ST77XX_ prefixes) ──────────────────────────────────────────
#define C_BG        ST77XX_BLACK
#define C_GRID      0x1082     
#define C_NORTH     ST77XX_GREEN
#define C_COMPASS   0x4B6D     
#define C_NEEDLE    ST77XX_GREEN
#define C_TEXT      0xC618     
#define C_DIM       0x4A49     
#define C_ACCENT    0x051D     
#define C_WARN      ST77XX_ORANGE
#define C_RED       ST77XX_RED

#define CX          120        
#define CY          150        
#define R_OUTER     90         
#define R_INNER     70         
#define R_NEEDLE    82         

// ── Helper: Cardinal Directions ──────────────────────────────────────────────
static const char* heading_to_cardinal(float h) {
    const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW","N"};
    return dirs[(int)((h + 22.5f) / 45.0f)];
}

// ── Helper: Draw Centered Text ────────────────────────────────────────────────
void drawCenterString(const char* buf, int x, int y) {
    int16_t x1, y1;
    uint16_t w, h;
    canvas.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor(x - w / 2, y - h / 2);
    canvas.print(buf);
}

// ── Rendering logic ──────────────────────────────────────────────────────────
static void draw_compass(float heading) {
    canvas.drawCircle(CX, CY, R_OUTER, C_COMPASS);
    canvas.drawCircle(CX, CY, R_INNER, C_DIM);

    for (int a = 0; a < 360; a += 30) {
        float rad = (a - heading) * M_PI / 180.0f;
        int x0 = CX + (int)(R_OUTER * sinf(rad));
        int y0 = CY - (int)(R_OUTER * cosf(rad));
        int x1 = CX + (int)((R_OUTER - 8) * sinf(rad));
        int y1 = CY - (int)((R_OUTER - 8) * cosf(rad));
        canvas.drawLine(x0, y0, x1, y1, (a == 0) ? C_NORTH : C_DIM);
    }

    const struct { int angle; const char* lbl; } cards[] = {
        {0,"N"},{90,"E"},{180,"S"},{270,"W"}
    };
    canvas.setTextSize(2);
    for (auto& c : cards) {
        float rad = (c.angle - heading) * M_PI / 180.0f;
        int lx = CX + (int)((R_INNER - 15) * sinf(rad));
        int ly = CY - (int)((R_INNER - 15) * cosf(rad));
        canvas.setTextColor((c.angle == 0) ? C_NORTH : C_TEXT);
        drawCenterString(c.lbl, lx, ly);
    }

    canvas.fillTriangle(CX, CY - R_NEEDLE, CX - 8, CY + 10, CX + 8, CY + 10, C_NORTH);
    canvas.fillTriangle(CX, CY + R_NEEDLE, CX - 8, CY - 10, CX + 8, CY - 10, C_DIM);
    canvas.fillCircle(CX, CY, 4, ST77XX_WHITE);
}

static void draw_topbar(const NavState& st) {
    canvas.fillRect(0, 0, 240, 22, 0x0841);
    canvas.setTextSize(1);
    canvas.setTextColor(C_NORTH);
    canvas.setCursor(4, 4);
    canvas.printf("%.0f deg", st.orient.heading);
    canvas.setTextColor(C_TEXT);
    canvas.setCursor(90, 4);
    canvas.print(st.motion.moving ? "MOVING" : "STILL");
}

static void draw_oled(const NavState& st) {
    s_oled.clearDisplay();
    s_oled.setTextColor(SSD1306_WHITE);
    s_oled.setCursor(0, 0);
    s_oled.setTextSize(1);
    s_oled.printf("HDG: %.0f  %s", st.orient.heading, heading_to_cardinal(st.orient.heading));
    s_oled.display();
}

// ── display_init: Called by main.cpp ─────────────────────────────────────────
bool display_init() {
    // Initialize TFT
    tft.init(240, 320);
    tft.setRotation(0); 
    tft.fillScreen(ST77XX_BLACK);
    tft.invertDisplay(false); // Needed for many ST7789 displays

    // Initialize OLED
    if (!s_oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        log_e("OLED Failed");
        // Non-fatal, return true anyway to let TFT work
    }
    
    log_i("Displays initialized");
    return true;
}

// ── DisplayTask: The FreeRTOS Loop ───────────────────────────────────────────
void DisplayTask(void* pvParams) {
    // Note: display_init() is already called in main.cpp, so we just start the loop
    for (;;) {
        NavState st = state_snapshot();

        // Draw everything to the invisible canvas first (double buffering)
        canvas.fillScreen(C_BG);
        draw_compass(st.orient.heading);
        draw_topbar(st);
        
        // Push the entire canvas to the screen at once (no flicker)
        tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 320);

        draw_oled(st);

        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 FPS
    }
}