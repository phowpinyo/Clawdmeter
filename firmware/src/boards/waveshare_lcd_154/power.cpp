#include "../../hal/power_hal.h"
#include "board.h"
#include <Arduino.h>

// No PMU on this board. The PWR button is wired directly to BTN_PWR_GPIO
// (GPIO 4) with an external pull-up, active LOW. We poll for a falling
// edge with debouncing and surface it as power_hal_pwr_pressed() so the
// shared main.cpp logic (cycle screen / cycle splash animation) works
// without changes.

#define PWR_DEBOUNCE_MS 30

static bool     last_level       = true;   // pulled-up idle
static uint32_t last_change_ms   = 0;
static bool     pwr_pressed_flag = false;

void power_hal_init(void) {
    pinMode(BTN_PWR_GPIO, INPUT_PULLUP);
    last_level = (digitalRead(BTN_PWR_GPIO) == HIGH);
}

void power_hal_tick(void) {
    bool level = (digitalRead(BTN_PWR_GPIO) == HIGH);
    if (level != last_level) {
        uint32_t now = millis();
        if (now - last_change_ms >= PWR_DEBOUNCE_MS) {
            last_change_ms = now;
            if (!level) {           // falling edge = press
                pwr_pressed_flag = true;
            }
            last_level = level;
        }
    }
}

int  power_hal_battery_pct(void) { return -1; }
bool power_hal_is_charging(void) { return false; }
bool power_hal_is_vbus_in(void)  { return false; }

bool power_hal_pwr_pressed(void) {
    if (pwr_pressed_flag) {
        pwr_pressed_flag = false;
        return true;
    }
    return false;
}
