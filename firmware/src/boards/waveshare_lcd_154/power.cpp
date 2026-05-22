#include "../../hal/power_hal.h"
#include "board.h"
#include <Arduino.h>

// No PMU on this board. The PWR button is wired directly to BTN_PWR_GPIO
// (GPIO 4) with an external pull-up, active LOW.
//
// Short press (release before PWR_LONG_PRESS_MS) → power_hal_pwr_pressed()
// fires, which the shared main.cpp uses to cycle screens / splash animations.
//
// Long press (held for ≥ PWR_LONG_PRESS_MS) → we drive BAT_POWER_GPIO LOW,
// releasing the soft-power latch IC. On battery the board cuts immediately.
// If USB is still attached, USB power bypasses the latch and the board may
// stay alive — that's expected, the user wanted "shutdown intent" anyway.

#define PWR_DEBOUNCE_MS    30
#define PWR_LONG_PRESS_MS  3000

static bool     last_level       = true;   // pulled-up idle
static uint32_t last_change_ms   = 0;
static uint32_t press_start_ms   = 0;      // 0 = not currently pressed
static bool     pwr_pressed_flag = false;
static bool     shutdown_armed   = false;  // true once long-press fires

void power_hal_init(void) {
    pinMode(BTN_PWR_GPIO, INPUT_PULLUP);
    last_level = (digitalRead(BTN_PWR_GPIO) == HIGH);
}

void power_hal_tick(void) {
    bool level = (digitalRead(BTN_PWR_GPIO) == HIGH);
    uint32_t now = millis();

    if (level != last_level) {
        if (now - last_change_ms >= PWR_DEBOUNCE_MS) {
            last_change_ms = now;
            if (!level) {
                // falling edge: press starts
                press_start_ms = now;
                shutdown_armed = false;
            } else {
                // rising edge: press ends. Only surface as cycle-screen if
                // the long-press shutdown didn't already fire.
                if (!shutdown_armed && press_start_ms != 0) {
                    pwr_pressed_flag = true;
                }
                press_start_ms = 0;
            }
            last_level = level;
        }
    } else if (!level && !shutdown_armed && press_start_ms != 0
               && (now - press_start_ms >= PWR_LONG_PRESS_MS)) {
        shutdown_armed = true;
        Serial.println("PWR long-press → release power latch");
        digitalWrite(BAT_POWER_GPIO, LOW);
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
