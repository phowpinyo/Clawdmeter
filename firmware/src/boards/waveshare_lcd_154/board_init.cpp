#include "board.h"
#include <Arduino.h>
#include <Wire.h>

extern "C" void board_init(void) {
    // Hold the power latch HIGH first thing, before anything else can take
    // ms. If we don't, the moment the user releases the PWR button on a
    // battery-only boot the latch drops and the board dies.
    pinMode(BAT_POWER_GPIO, OUTPUT);
    digitalWrite(BAT_POWER_GPIO, HIGH);

    Wire.begin(IIC_SDA, IIC_SCL);
}
