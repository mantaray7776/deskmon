// ─────────────────────────────────────────────────────────────────────────────
//  TFT_eSPI User_Setup.h for DeskMon ESP32-S3 + ILI9341 2.8" display
//  Place this file in include/ and add to platformio.ini:
//    build_flags = -DUSER_SETUP_LOADED -DUSER_SETUP_INFO="User_Setup"
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

// Driver
#define ILI9341_DRIVER

// Resolution
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// Pin assignments — match Config.h
#define TFT_MOSI    18
#define TFT_SCLK    20
#define TFT_CS      21
#define TFT_DC      22
#define TFT_RST     23
#define TFT_MISO    19

// SPI frequency
#define SPI_FREQUENCY       40000000   // 40 MHz — safe for ILI9341
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// Colour order
#define TFT_RGB_ORDER TFT_BGR

// Fonts to load
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
