#include "../../hal/imu_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <SensorQMI8658.hpp>

// Same chip + same logic as the AMOLED-2.16 port — polled at 10Hz with
// hysteresis so the rotation only commits after the new orientation is
// stable for STABLE_TIME_MS.

#define IMU_POLL_MS       100
#define STABLE_TIME_MS    300
#define TILT_THRESHOLD    0.5f

static SensorQMI8658 imu;
static uint8_t  current_rotation   = 0;
static uint8_t  candidate_rotation = 0;
static uint32_t candidate_since    = 0;
static uint32_t last_poll_ms       = 0;
static bool     imu_ok             = false;

// QMI8658 on the LCD-1.54 is mounted 90° rotated from the AMOLED-2.16
// reference, AND Arduino_GFX's setRotation(1) is 90° CCW (not CW like
// the standard MADCTL convention), so the landscape axes invert. Final
// mapping verified visually against the physical board:
//   USB bottom (ax≈ 0, ay≈-1) → rotation 0 (portrait)
//   USB left   (ax≈+1, ay≈ 0) → rotation 3 (landscape CCW)
//   USB top    (ax≈ 0, ay≈+1) → rotation 2 (portrait 180°)
//   USB right  (ax≈-1, ay≈ 0) → rotation 1 (landscape CW)
static uint8_t accel_to_rotation(float ax, float ay) {
    float abs_ax = fabsf(ax);
    float abs_ay = fabsf(ay);
    if (abs_ax < TILT_THRESHOLD && abs_ay < TILT_THRESHOLD) {
        return 255;
    }
    if (abs_ay > abs_ax) return (ay > 0) ? 2 : 0;
    return (ax > 0) ? 3 : 1;
}

void imu_hal_init(void) {
    if (!imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        Serial.println("QMI8658 init failed");
        return;
    }
    Serial.println("QMI8658 init OK");
    imu.configAccelerometer(
        SensorQMI8658::ACC_RANGE_4G,
        SensorQMI8658::ACC_ODR_LOWPOWER_21Hz,
        SensorQMI8658::LPF_MODE_3);
    imu.enableAccelerometer();
    imu_ok = true;
}

void imu_hal_tick(void) {
    if (!imu_ok) return;
    uint32_t now = millis();
    if (now - last_poll_ms < IMU_POLL_MS) return;
    last_poll_ms = now;

    float ax, ay, az;
    if (!imu.getAccelerometer(ax, ay, az)) return;

    uint8_t want = accel_to_rotation(ax, ay);
    if (want == 255) return;

    if (want != candidate_rotation) {
        candidate_rotation = want;
        candidate_since = now;
        return;
    }
    if (want != current_rotation && now - candidate_since >= STABLE_TIME_MS) {
        current_rotation = want;
        Serial.printf("Rotation: %d\n", current_rotation);
    }
}

uint8_t imu_hal_rotation_quadrant(void) { return current_rotation; }
