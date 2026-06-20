// ─────────────────────────────────────────────────────────────────────────────
//  DistractTask.cpp  — computes distraction score from motion + EM data
// ─────────────────────────────────────────────────────────────────────────────
#include <cmath>
#include <algorithm> // Alternative for fmin/fmax
#include "Config.h"
#include "NavState.h"

// Distraction score: 0–100, exponential moving average of vibration + EM spikes
// Score decays toward 0 when the desk is calm, spikes toward 100 on events.
void DistractTask(void* pvParams) {
    const float DECAY  = 0.98f;   // per 100 ms tick → half-life ~3 s
    const float VIB_W  = 60.0f;   // vibration contribution weight
    const float EM_W   = 40.0f;   // EM contribution weight
    float score = 0;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        NavState st = state_snapshot();

        // Normalised contributions (clamp to 0–1)
        float vib_norm = fminf(st.motion.vibration_rms / IMPACT_THRESHOLD, 1.0f);
        float em_norm  = fminf(st.motion.em_variance   / EM_SPIKE_THRESHOLD, 1.0f);

        float raw = vib_norm * VIB_W + em_norm * EM_W;
        score = score * DECAY + raw * (1.0f - DECAY) * 100.0f;
        score = fminf(fmaxf(score, 0), 100);

        WITH_STATE([&]{
            g_state.session.distraction_score = (uint8_t)score;
        });

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100)); // 10 Hz
    }
}
