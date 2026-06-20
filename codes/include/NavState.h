#pragma once
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ─────────────────────────────────────────────────────────────────────────────
//  NavState  —  single shared struct for ALL sensor data and derived values.
//  SensorTask writes it under mutex. Every other task reads it.
//  Adding a new sensor = add a field here + populate in SensorTask.
//  Display / Web code never needs to change.
// ─────────────────────────────────────────────────────────────────────────────

struct SensorRaw {
    float ax, ay, az;       // accelerometer  (m/s²)
    float gx, gy, gz;       // gyroscope      (rad/s)
    float mx, my, mz;       // magnetometer   (µT)
    float temp_bmp;         // BMP390 temperature (°C)  — 0 if DNP
    float pressure;         // BMP390 pressure (hPa)    — 0 if DNP
};

struct Orientation {
    float roll, pitch, yaw; // Euler angles (degrees)
    float heading;          // tilt-compensated compass heading (0–360°)
    float q0, q1, q2, q3;  // Mahony quaternion (for WebSocket 3D view)
};

struct MotionState {
    bool     moving;            // true if RMS accel > threshold
    float    vibration_rms;     // RMS of accel magnitude delta (desk impact metric)
    float    em_variance;       // variance of mag vector magnitude (EM interference)
    uint32_t impact_count;      // cumulative desk impact events
    uint32_t em_spike_count;    // cumulative EM spike events
};

struct SessionState {
    bool     active;            // session running
    uint32_t elapsed_sec;       // seconds into current session
    uint32_t session_duration;  // target duration (default 25 min = 1500 s)
    uint8_t  pomodoro_count;    // completed pomodoros today
    uint8_t  distraction_score; // 0–100, computed by DistractTask
};

struct PowerState {
    float    batt_voltage;      // battery voltage (V) — 0 if DNP
    uint8_t  batt_percent;      // 0–100            — 0 if DNP
    bool     charging;          // true if VBUS present
};

struct WifiState {
    bool     connected;
    int8_t   rssi;
    char     ip[16];
    uint8_t  ws_clients;        // active WebSocket connections
};

// ── Event log ring buffer ───────────────────────────────────────────────────
#define EVENT_LOG_SIZE 32

enum class EventType : uint8_t {
    NONE = 0,
    IMPACT,        // desk impact detected
    EM_SPIKE,      // EM interference spike
    SESSION_START,
    SESSION_END,
    POMODORO_DONE,
    WIFI_CONNECTED,
    WIFI_LOST,
    OTA_START,
    OTA_DONE,
};

struct Event {
    EventType   type;
    uint32_t    timestamp_ms;
    float       value;          // optional numeric payload
};

// ── Master NavState ─────────────────────────────────────────────────────────
struct NavState {
    SensorRaw   raw;
    Orientation orient;
    MotionState motion;
    SessionState session;
    PowerState   power;
    WifiState    wifi;

    // Event ring buffer
    Event       events[EVENT_LOG_SIZE];
    uint8_t     event_head;     // next write index
    uint32_t    event_total;    // total events ever logged

    // Timing
    uint32_t    last_sensor_ms; // millis of last sensor update
    uint32_t    uptime_sec;
};

// ── Global singleton + mutex ─────────────────────────────────────────────────
extern NavState         g_state;
extern SemaphoreHandle_t g_state_mutex;

// Helper: acquire mutex, run lambda, release
// Usage:  WITH_STATE([&]{ g_state.session.active = true; });
// include/NavState.h

#define WITH_STATE(...) do { \
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) { \
        __VA_ARGS__(); \
        xSemaphoreGive(g_state_mutex); \
    } \
} while(0)

// Safe snapshot for tasks that need a consistent read
inline NavState state_snapshot() {
    NavState snap;
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        snap = g_state;
        xSemaphoreGive(g_state_mutex);
    }
    return snap;
}

void nav_log_event(EventType type, float value = 0.0f);
