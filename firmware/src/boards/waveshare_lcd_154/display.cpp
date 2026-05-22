#include "../../hal/display_hal.h"
#include "../../hal/imu_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// ST7789 over SPI with native MADCTL rotation. Because setRotation() only
// rewrites MADCTL and leaves the panel's GRAM untouched, the previous
// orientation's pixels remain visible in the new coordinate system as a
// ghost until LVGL overwrites them. We fillScreen(0) immediately after
// setRotation() to wipe the GRAM in one shot, then ramp the backlight
// back up so the change reads as deliberate.
//
// Backlight is a plain GPIO (no panel command). We PWM it via ledc so
// display_hal_set_brightness() retains its 0..255 contract.

#define BL_LEDC_FREQ 5000
#define BL_LEDC_RES  8

static Arduino_DataBus* bus = nullptr;
static Arduino_ST7789*  gfx = nullptr;

void display_hal_init(void) {
    bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO);
    gfx = new Arduino_ST7789(
        bus, LCD_RESET, 0 /* rotation */, true /* IPS */, LCD_WIDTH, LCD_HEIGHT);
}

void display_hal_begin(void) {
    gfx->begin();
    gfx->fillScreen(0x0000);

    ledcAttach(LCD_BL, BL_LEDC_FREQ, BL_LEDC_RES);
    ledcWrite(LCD_BL, 200);
}

void display_hal_set_brightness(uint8_t level) {
    ledcWrite(LCD_BL, level);
}

void display_hal_fill_screen(uint16_t color) {
    if (gfx) gfx->fillScreen(color);
}

void display_hal_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                             const uint16_t* pixels) {
    if (gfx) gfx->draw16bitRGBBitmap(x, y, (uint16_t*)pixels, w, h);
}

void display_hal_tick(void) {
    static uint8_t  last_rotation = 0;
    static uint8_t  ramp_step = 0;
    static uint32_t ramp_last = 0;

    uint8_t rot = imu_hal_rotation_quadrant();
    if (rot != last_rotation) {
        display_hal_set_brightness(0);
        last_rotation = rot;
        if (gfx) {
            gfx->setRotation(rot);
            gfx->fillScreen(0x0000);
        }
        lv_obj_invalidate(lv_screen_active());
        ramp_step = 1;
        return;
    }

    if (ramp_step == 0) return;
    uint32_t now = millis();
    if (now - ramp_last < 25) return;
    ramp_last = now;

    static const uint8_t levels[] = {60, 120, 170, 200};
    display_hal_set_brightness(levels[ramp_step - 1]);
    if (++ramp_step > 4) ramp_step = 0;
}

void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
}
