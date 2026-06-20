#include "NavState.h"
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  NavState global singleton + mutex
// ─────────────────────────────────────────────────────────────────────────────

NavState          g_state  = {};
SemaphoreHandle_t g_state_mutex = nullptr;

void nav_log_event(EventType type, float value) {
    if (!g_state_mutex) return;
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        Event& ev = g_state.events[g_state.event_head % EVENT_LOG_SIZE];
        ev.type         = type;
        ev.timestamp_ms = millis();
        ev.value        = value;
        g_state.event_head = (g_state.event_head + 1) % EVENT_LOG_SIZE;
        g_state.event_total++;
        xSemaphoreGive(g_state_mutex);
    }
}
