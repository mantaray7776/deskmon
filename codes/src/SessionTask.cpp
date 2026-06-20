#include "Config.h"
#include "NavState.h"
#include <Preferences.h>

// ─────────────────────────────────────────────────────────────────────────────
//  SessionTask
//  Core 0, 1 Hz.
//  Ticks the session timer, manages pomodoro phases, persists stats to NVS.
// ─────────────────────────────────────────────────────────────────────────────

static Preferences s_prefs;

static void load_prefs() {
    s_prefs.begin("deskmon", true); // read-only
    WITH_STATE([&]{
        g_state.session.pomodoro_count   = s_prefs.getUChar("pomo_count", 0);
        g_state.session.session_duration = s_prefs.getUInt("sess_dur", SESSION_DEFAULT_SEC);
    });
    s_prefs.end();
}

static void save_prefs() {
    NavState st = state_snapshot();
    s_prefs.begin("deskmon", false); // read-write
    s_prefs.putUChar("pomo_count", st.session.pomodoro_count);
    s_prefs.putUInt("sess_dur",    st.session.session_duration);
    s_prefs.end();
}

void SessionTask(void* pvParams) {
    load_prefs();

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t   uptime    = 0;

    while (true) {
        WITH_STATE([&]{
            g_state.uptime_sec = uptime++;

            if (!g_state.session.active) goto done;

            g_state.session.elapsed_sec++;

            if (g_state.session.elapsed_sec >= g_state.session.session_duration) {
                // Session complete
                g_state.session.active      = false;
                g_state.session.elapsed_sec = 0;
                g_state.session.pomodoro_count++;
                nav_log_event(EventType::POMODORO_DONE,
                              (float)g_state.session.pomodoro_count);
            }
            done:;
        });

        // Persist every 60 seconds
        static uint8_t save_div = 0;
        if (++save_div >= 60) {
            save_div = 0;
            save_prefs();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000)); // 1 Hz
    }
}
