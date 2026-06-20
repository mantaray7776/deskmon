#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Config.h  —  all pin assignments and compile-time feature flags
//  Mirror of platformio.ini -D flags, with fallback defaults.
// ─────────────────────────────────────────────────────────────────────────────

// ── I2C ──────────────────────────────────────────────────────────────────────
#ifndef PIN_SDA
#define PIN_SDA             4
#endif
#ifndef PIN_SCL
#define PIN_SCL             5
#endif
#define I2C_FREQ_HZ         400000   // 400 kHz fast mode

// ── SPI (shared bus) ─────────────────────────────────────────────────────────
#ifndef PIN_MOSI
#define PIN_MOSI            18
#endif
#ifndef PIN_MISO
#define PIN_MISO            19
#endif
#ifndef PIN_SCLK
#define PIN_SCLK            20
#endif

// ── TFT ILI9341 ──────────────────────────────────────────────────────────────
#ifndef PIN_TFT_CS
#define PIN_TFT_CS          21
#endif
#ifndef PIN_TFT_DC
#define PIN_TFT_DC          22
#endif
#ifndef PIN_TFT_RST
#define PIN_TFT_RST         23
#endif
#ifndef PIN_TFT_BL
#define PIN_TFT_BL          24
#endif
#define TFT_BL_FREQ         5000     // backlight PWM frequency
#define TFT_BL_DUTY         200      // 0–255

// ── I2C addresses ────────────────────────────────────────────────────────────
#define ADDR_OLED           0x3C
#define ADDR_MPU6050        0x68     // AD0 = GND
#define ADDR_HMC5883L       0x1E
#define ADDR_BMP390         0x76

// ── Interrupt / GPIO lines ───────────────────────────────────────────────────
#ifndef PIN_MPU_INT
#define PIN_MPU_INT         6
#endif
#ifndef PIN_HMC_DRDY
#define PIN_HMC_DRDY        7
#endif

// ── DNP: microSD ─────────────────────────────────────────────────────────────
#ifndef PIN_SD_CS
#define PIN_SD_CS           15
#endif

// ── DNP: LoRa RA-02 SX1278 ───────────────────────────────────────────────────
#ifndef PIN_LORA_CS
#define PIN_LORA_CS         16
#endif
#ifndef PIN_LORA_RST
#define PIN_LORA_RST        17
#endif
#ifndef PIN_LORA_DIO0
#define PIN_LORA_DIO0       11
#endif
#define LORA_FREQUENCY      433E6

// ── DNP: Rotary encoder ──────────────────────────────────────────────────────
#ifndef PIN_ENC_A
#define PIN_ENC_A           8
#endif
#ifndef PIN_ENC_B
#define PIN_ENC_B           9
#endif
#ifndef PIN_ENC_SW
#define PIN_ENC_SW          12
#endif

// ── DNP: Battery ADC ─────────────────────────────────────────────────────────
#ifndef PIN_BATT_ADC
#define PIN_BATT_ADC        1
#endif
#define BATT_DIVIDER_RATIO  2.0f    // 100k/100k divider
#define BATT_FULL_V         4.2f
#define BATT_EMPTY_V        3.2f

// ── Feature flags (set in platformio.ini -D, default off) ───────────────────
#ifndef FEATURE_SD
#define FEATURE_SD          0
#endif
#ifndef FEATURE_LORA
#define FEATURE_LORA        0
#endif
#ifndef FEATURE_ENCODER
#define FEATURE_ENCODER     0
#endif
#ifndef FEATURE_BATT
#define FEATURE_BATT        0
#endif
#ifndef FEATURE_BMP
#define FEATURE_BMP         0
#endif
#ifndef FEATURE_BUZZER
#define FEATURE_BUZZER      0
#endif

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID           "YOUR_SSID"
#define WIFI_PASSWORD       "YOUR_PASSWORD"
#define MDNS_HOSTNAME       "deskmon"    // → deskmon.local
#define WS_PORT             81

// ── Session defaults ─────────────────────────────────────────────────────────
#define SESSION_DEFAULT_SEC (25 * 60)   // 25-minute pomodoro
#define BREAK_SHORT_SEC     (5  * 60)
#define BREAK_LONG_SEC      (15 * 60)

// ── Sensor thresholds (tunable via WebSocket config packet) ──────────────────
#define IMPACT_THRESHOLD    0.8f        // m/s² RMS delta to count as impact
#define EM_SPIKE_THRESHOLD  8.0f        // µT variance to count as EM spike
#define MOVING_THRESHOLD    0.15f       // m/s² to consider device moving

// ── Task stack / priority ────────────────────────────────────────────────────
#define STACK_SENSOR        4096
#define STACK_DISPLAY       6144
#define STACK_WEB           8192
#define STACK_DISTRACT      3072
#define STACK_SESSION       2048

#define PRI_SENSOR          5           // highest — real-time sensor fusion
#define PRI_DISPLAY         3
#define PRI_WEB             4
#define PRI_DISTRACT        2
#define PRI_SESSION         1

#define CORE_SENSOR         1           // dedicated sensor core
#define CORE_DISPLAY        1
#define CORE_WEB            0           // Wi-Fi / TCP core
#define CORE_DISTRACT       0
#define CORE_SESSION        0
