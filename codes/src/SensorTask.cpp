#include "Config.h"
#include "NavState.h"

#include <Wire.h>
#include <MPU6050_light.h>    // Matches your working Map project
#include <QMC5883LCompass.h>  // Matches your working Map project

#if FEATURE_BMP
#include <Adafruit_BMP3XX.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  SensorTask
//  Core 1, 100 Hz. Reads sensors and writes to NavState.
// ─────────────────────────────────────────────────────────────────────────────

static MPU6050 s_mpu(Wire);
static QMC5883LCompass s_compass;

#if FEATURE_BMP
static Adafruit_BMP3XX s_bmp;
#endif

// ── Init all I2C sensors ─────────────────────────────────────────────────────
bool sensor_init() {
    // 1. Start I2C with your Map Project Pins
    Wire.begin(17, 18); 

    // 2. Init MPU6050_light
    byte status = s_mpu.begin();
    if (status != 0) {
        log_e("MPU6050 not found at 0x68");
        return false;
    }

    // 3. Calibrate (Device must be still!)
    log_i("MPU-6050 OK. Calibrating... DO NOT MOVE.");
    vTaskDelay(pdMS_TO_TICKS(1000));
    s_mpu.calcOffsets(); 
    log_i("Calibration Done.");

    // 4. Init QMC5883L Compass
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

// ── Main sensor task ─────────────────────────────────────────────────────────
void SensorTask(void* pvParams) {
    // Sliding window for vibration RMS
    const int VIB_WIN = 20;
    float vib_buf[VIB_WIN] = {0};
    int vib_idx = 0;
    float prev_amag = 0;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        // 1. Update library states
        s_mpu.update();
        s_compass.read();

        // 2. Get Raw Data
        float ax = s_mpu.getAccX() * 9.81f; // Convert g to m/s^2
        float ay = s_mpu.getAccY() * 9.81f;
        float az = s_mpu.getAccZ() * 9.81f;
        float gx = s_mpu.getGyroX();
        float gy = s_mpu.getGyroY();
        float gz = s_mpu.getGyroZ();
        
        int heading = s_compass.getAzimuth();

        // 3. Vibration RMS (Desk impact detection)
        float amag = sqrtf(ax*ax + ay*ay + az*az);
        float delta = fabsf(amag - prev_amag);
        prev_amag = amag;
        vib_buf[vib_idx++ % VIB_WIN] = delta * delta;
        float vib_sum = 0;
        for (int i=0; i<VIB_WIN; i++) vib_sum += vib_buf[i];
        float vib_rms = sqrtf(vib_sum / VIB_WIN);

        // 4. Motion state logic
        bool moving = fabsf(amag - 9.81f) > (MOVING_THRESHOLD * 9.81f);

        // 5. Write to NavState under mutex
        WITH_STATE([&]{
            g_state.raw = {ax, ay, az, gx, gy, gz, 0, 0, 0, 0, 0};
            g_state.orient = {
                s_mpu.getAngleY(), // roll
                s_mpu.getAngleX(), // pitch
                0.0f,              // yaw (relative)
                (float)heading,    // compass heading
                0,0,0,0            // quats not used by light lib
            };
            g_state.motion.moving = moving;
            g_state.motion.vibration_rms = vib_rms;
            
            if (vib_rms > IMPACT_THRESHOLD)
                g_state.motion.impact_count++;
            
            g_state.last_sensor_ms = millis();
        });

        // Log impact events
        if (vib_rms > IMPACT_THRESHOLD)
            nav_log_event(EventType::IMPACT, vib_rms);

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

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10)); // 100 Hz
    }
}