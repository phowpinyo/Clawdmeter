#include "../../hal/board_caps.h"
#include "board.h"

static const BoardCaps caps = {
    .name = BOARD_NAME,
    .width = LCD_WIDTH,
    .height = LCD_HEIGHT,
    .button_count = 3,
    .has_rotation = true,
    .has_battery = false,
    .has_imu = true,
};

const BoardCaps& board_caps(void) { return caps; }
