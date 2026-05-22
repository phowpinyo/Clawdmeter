#include "../../hal/display_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ST7789 over SPI. Rotation is disabled on this port (BOARD_HAS_ROTATION=0)
// — gfx->setRotation() doesn't clear the panel's GRAM, so toggling it at
// runtime leaves the previous orientation's pixels visible as a ghost.
// Re-enabling needs a fillScreen(0) immediately after setRotation().
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

void display_hal_tick(void) {}

void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
}
