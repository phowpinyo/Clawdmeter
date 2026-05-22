#include "../../hal/input_hal.h"
#include "board.h"
#include <Arduino.h>

void input_hal_init(void) {
    pinMode(BTN_BACK_GPIO, INPUT_PULLUP);
    pinMode(BTN_FWD_GPIO,  INPUT_PULLUP);
    // BTN_PWR_GPIO is owned by power.cpp (it feeds power_hal_pwr_pressed,
    // which main.cpp uses for cycle-screen / cycle-animation).
}

bool input_hal_is_held(InputButton btn) {
    switch (btn) {
    case INPUT_BTN_PRIMARY:
        return digitalRead(BTN_BACK_GPIO) == LOW;
    case INPUT_BTN_SECONDARY:
        return digitalRead(BTN_FWD_GPIO) == LOW;
    }
    return false;
}
