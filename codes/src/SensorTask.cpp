#include "Config.h"
#include "NavState.h"

#include <Wire.h>
#include <MPU6050_light.h>
#include <QMC5883LCompass.h>

#if FEATURE_BMP
#include <Adafruit_BMP3XX.h>
#endif

static MPU6050 s_mpu(Wire);
static QMC5883LCompass s_compass;

#if FEATURE_BMP
static Adafruit_BMP3XX s_bmp;
#endif

bool sensor_init() {
    Wire.begin(17, 18);

    byte status = s_mpu.begin();
    if (status != 0) { log_e("MPU6050 not found"); return false; }

    log_i("MPU-6050 OK. Calibrating...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    s_mpu.calcOffsets();
    log_i("Calibration done.");

    s_compass.init();
    log_i("Compass OK");

#if FEATURE_BMP
    if (s_bmp.begin_I2C(0x77)) {
        s_bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
        s_bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
        s_bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
        s_bmp.setOutputDataRate(BMP3_ODR_50_HZ);
        log_i("BMP390 OK");
    }
#endif
    return true;
}

void SensorTask(void* pvParams) {
    const int VIB_WIN = 20;
    float vib_buf[VIB_WIN] = {0};
    int vib_idx = 0;
    float prev_amag = 0;

    // EM variance window
    const int EM_WIN = 20;
    float em_buf[EM_WIN] = {0};
    int em_idx = 0;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        s_mpu.update();
        s_compass.read();

        float ax = s_mpu.getAccX() * 9.81f;
        float ay = s_mpu.getAccY() * 9.81f;
        float az = s_mpu.getAccZ() * 9.81f;
        float gx = s_mpu.getGyroX();
        float gy = s_mpu.getGyroY();
        float gz = s_mpu.getGyroZ();
        int heading = s_compass.getAzimuth();

        // Vibration RMS
        float amag = sqrtf(ax*ax + ay*ay + az*az);
        float delta = fabsf(amag - prev_amag);
        prev_amag = amag;
        vib_buf[vib_idx++ % VIB_WIN] = delta * delta;
        float vib_sum = 0;
        for (int i = 0; i < VIB_WIN; i++) vib_sum += vib_buf[i];
        float vib_rms = sqrtf(vib_sum / VIB_WIN);

        // EM variance (magnitude of mag vector)
        float mx = s_compass.getX();
        float my = s_compass.getY();
        float mz = s_compass.getZ();
        float mmag = sqrtf(mx*mx + my*my + mz*mz);
        em_buf[em_idx++ % EM_WIN] = mmag;
        float em_mean = 0;
        for (int i = 0; i < EM_WIN; i++) em_mean += em_buf[i];
        em_mean /= EM_WIN;
        float em_var = 0;
        for (int i = 0; i < EM_WIN; i++) {
            float d = em_buf[i] - em_mean;
            em_var += d * d;
        }
        em_var /= EM_WIN;

        bool moving = fabsf(amag - 9.81f) > (MOVING_THRESHOLD * 9.81f);

        WITH_STATE([&]{
            g_state.raw = {ax, ay, az, gx, gy, gz, mx, my, mz, 0, 0};
            g_state.orient = {
                s_mpu.getAngleY(),
                s_mpu.getAngleX(),
                0.0f,
                (float)heading,
                0,0,0,0
            };
            g_state.motion.moving        = moving;
            g_state.motion.vibration_rms = vib_rms;
            g_state.motion.em_variance   = em_var;

            if (vib_rms > IMPACT_THRESHOLD) g_state.motion.impact_count++;
            if (em_var  > EM_SPIKE_THRESHOLD) g_state.motion.em_spike_count++;

            g_state.last_sensor_ms = millis();
        });

        if (vib_rms > IMPACT_THRESHOLD) nav_log_event(EventType::IMPACT, vib_rms);
        if (em_var  > EM_SPIKE_THRESHOLD) nav_log_event(EventType::EM_SPIKE, em_var);

#if FEATURE_BMP
        static uint8_t bmp_div = 0;
        if (++bmp_div >= 10) {
            bmp_div = 0;
            if (s_bmp.performReading()) {
                WITH_STATE([&]{
                    g_state.raw.temp_bmp = s_bmp.temperature;
                    g_state.raw.pressure = s_bmp.pressure / 100.0f;
                });
            }
        }
#endif
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}
