#include <Arduino.h>
#include "Config.h"
#include "NavState.h"

// ─────────────────────────────────────────────────────────────────────────────
//  DeskMon — ESP32-S3 desk environment monitor
//  main.cpp — hardware init, mutex creation, task spawning
//
//  Task map:
//    Core 1 │ SensorTask  (100 Hz) — IMU + compass + AHRS
//    Core 1 │ DisplayTask  (30 Hz) — TFT sprite render + OLED HUD
//    Core 0 │ WebTask  (event)     — Wi-Fi, WebSocket, OTA
//    Core 0 │ DistractTask (10 Hz) — distraction score EMA
//    Core 0 │ SessionTask   (1 Hz) — pomodoro timer, NVS persistence
// ─────────────────────────────────────────────────────────────────────────────

// Forward declarations (defined in each task .cpp)
extern bool sensor_init();
extern bool display_init();
extern void SensorTask(void*);
extern void DisplayTask(void*);
extern void WebTask(void*);
extern void DistractTask(void*);
extern void SessionTask(void*);

// ── Optional DNP task forward declarations ───────────────────────────────────
#if FEATURE_BATT
static void BattTask(void* pv) {
    // ADC reads VBAT through 100k/100k divider on PIN_BATT_ADC
    // ESP32-S3 ADC is 12-bit, Vref ≈ 3.3 V
    const float ADC_SCALE = 3.3f / 4095.0f;
    while (true) {
        float raw = analogRead(PIN_BATT_ADC) * ADC_SCALE * BATT_DIVIDER_RATIO;
        float pct = (raw - BATT_EMPTY_V) / (BATT_FULL_V - BATT_EMPTY_V) * 100.0f;
        pct = fminf(fmaxf(pct, 0), 100);
        bool charging = (digitalRead(PIN_BOOT) == HIGH); // VBUS sense placeholder
        WITH_STATE([&]{
            g_state.power.batt_voltage = raw;
            g_state.power.batt_percent = (uint8_t)pct;
            g_state.power.charging     = charging;
        });
        vTaskDelay(pdMS_TO_TICKS(5000)); // every 5 s
    }
}
#endif

#if FEATURE_ENCODER
#include <ESP32Encoder.h>
static ESP32Encoder s_enc;
static void EncoderTask(void* pv) {
    // Encoder pulses map to session duration adjustment (±1 min per click)
    int64_t prev = s_enc.getCount();
    while (true) {
        int64_t curr = s_enc.getCount();
        int32_t delta = (int32_t)(curr - prev);
        if (delta != 0) {
            prev = curr;
            WITH_STATE([&]{
                int32_t dur = (int32_t)g_state.session.session_duration + delta * 60;
                if (dur < 60)    dur = 60;    // minimum 1 min
                if (dur > 90*60) dur = 90*60; // maximum 90 min
                g_state.session.session_duration = (uint32_t)dur;
            });
        }
        // Push button = start / stop session
        if (digitalRead(PIN_ENC_SW) == LOW) {
            WITH_STATE([&]{
                g_state.session.active = !g_state.session.active;
                if (g_state.session.active) g_state.session.elapsed_sec = 0;
            });
            vTaskDelay(pdMS_TO_TICKS(500)); // debounce
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif

// ── Startup self-test ────────────────────────────────────────────────────────
static void run_selftest() {
    log_i("=== DeskMon self-test ===");
    log_i("Chip: %s rev%d",
          ESP.getChipModel(), ESP.getChipRevision());
    log_i("Flash: %u MB  PSRAM: %u MB",
          ESP.getFlashChipSize() / (1024*1024),
          ESP.getPsramSize()     / (1024*1024));
    log_i("Free heap: %u bytes", esp_get_free_heap_size());
    log_i("CPU freq: %u MHz", getCpuFrequencyMhz());
    log_i("Features: SD=%d LORA=%d ENC=%d BATT=%d BMP=%d",
          FEATURE_SD, FEATURE_LORA, FEATURE_ENCODER,
          FEATURE_BATT, FEATURE_BMP);
}

void setup() {
    Serial.begin(115200);
    delay(500); // let serial settle

    run_selftest();

    // ── Create global mutex before any task starts ────────────────────────
    g_state_mutex = xSemaphoreCreateMutex();
    configASSERT(g_state_mutex);

    // ── Default session config ────────────────────────────────────────────
    g_state.session.session_duration = SESSION_DEFAULT_SEC;
    g_state.session.active           = false;

    // ── Sensor init (blocking — must succeed before tasks start) ─────────
    if (!sensor_init()) {
        log_e("FATAL: sensor init failed — halting");
        while (true) delay(1000);
    }

    // ── Display init ──────────────────────────────────────────────────────
    if (!display_init()) {
        log_w("Display init partial — continuing anyway");
    }

    // ── Optional hardware init ────────────────────────────────────────────
#if FEATURE_ENCODER
    s_enc.attachHalfQuad(PIN_ENC_A, PIN_ENC_B);
    s_enc.setCount(0);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
#endif

#if FEATURE_BATT
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db); // 0–3.3 V range
#endif

    // ── Spawn FreeRTOS tasks ──────────────────────────────────────────────
    xTaskCreatePinnedToCore(SensorTask,  "sensor",   STACK_SENSOR,   nullptr, PRI_SENSOR,   nullptr, CORE_SENSOR);
    xTaskCreatePinnedToCore(DisplayTask, "display",  STACK_DISPLAY,  nullptr, PRI_DISPLAY,  nullptr, CORE_DISPLAY);
    xTaskCreatePinnedToCore(WebTask,     "web",      STACK_WEB,      nullptr, PRI_WEB,      nullptr, CORE_WEB);
    xTaskCreatePinnedToCore(DistractTask,"distract", STACK_DISTRACT, nullptr, PRI_DISTRACT, nullptr, CORE_DISTRACT);
    xTaskCreatePinnedToCore(SessionTask, "session",  STACK_SESSION,  nullptr, PRI_SESSION,  nullptr, CORE_SESSION);

#if FEATURE_BATT
    xTaskCreatePinnedToCore(BattTask, "batt", 2048, nullptr, 1, nullptr, CORE_SESSION);
#endif
#if FEATURE_ENCODER
    xTaskCreatePinnedToCore(EncoderTask, "encoder", 2048, nullptr, 2, nullptr, CORE_SESSION);
#endif

    log_i("All tasks spawned — DeskMon running");
}

// loop() is unused — all work is in FreeRTOS tasks
// Give the idle task something clean to do
void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}
