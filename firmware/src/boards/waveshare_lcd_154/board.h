#pragma once

// Waveshare ESP32-S3-Touch-LCD-1.54 — 1.54" IPS LCD kit.
// 240x240 ST7789 (SPI) + CST816 touch + QMI8658 IMU.
// No PMU (simple Li-ion charger chip only) and no battery measurement.
// Native ST7789 MADCTL rotation is used (the panel is square so it works
// without geometry surprises) — no CPU rotation strip needed.

#define BOARD_NAME           "Waveshare LCD 1.54"

// ---- Display geometry ----
#define LCD_WIDTH            240
#define LCD_HEIGHT           240

// ---- ST7789 SPI display pins ----
#define LCD_DC               45
#define LCD_CS               21
#define LCD_SCLK             38
#define LCD_MOSI             39
#define LCD_MISO             -1
#define LCD_RESET            40
#define LCD_BL               46    // backlight, PWM via ledc

// ---- I2C bus (touch + IMU) ----
#define IIC_SDA              42
#define IIC_SCL              41

// ---- Touch (CST816 via TouchDrvCSTXXX library) ----
#define TP_INT               48
#define TP_RST               47
#define CST816_ADDR          0x15

// ---- IMU (QMI8658) ----
// INT pin exists on GPIO 6 but isn't used — IMU is polled at 10Hz in imu.cpp.

// ---- Buttons ----
#define BTN_BACK_GPIO        0     // BOOT — primary, Space (PTT)
#define BTN_FWD_GPIO         5     // PLUS — secondary, Shift+Tab (mode toggle)
#define BTN_PWR_GPIO         4     // PWR — middle, cycle screens / cycle splash animation

// ---- Capability flags ----
#define BOARD_HAS_SECONDARY_BUTTON 1
#define BOARD_HAS_ROTATION         0
#define BOARD_HAS_IMU              1
#define BOARD_HAS_BATTERY          0
#define BOARD_HAS_IO_EXPANDER      0
