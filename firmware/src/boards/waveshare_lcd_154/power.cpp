#include "../../hal/power_hal.h"
#include "board.h"
#include <Arduino.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

// No PMU on this board. Three pieces of power infrastructure:
//
//   BAT_POWER_GPIO    output — soft-power latch hold. HIGH from boot,
//                     driven LOW for long-press shutdown.
//   BTN_PWR_GPIO      input  — PWR button (active LOW, pull-up). Short
//                     press = power_hal_pwr_pressed() for cycle-screen,
//                     long press ≥ PWR_LONG_PRESS_MS = release latch.
//   BAT_ADC_GPIO      ADC    — battery voltage through a ×3 divider.
//   BAT_CHARGING_GPIO input  — LOW = charging.
//
// Voltage → percent mapping is taken verbatim from the Waveshare
// 01_factory ESP-IDF bsp_power_manager.c (charger IC characteristic).

#define PWR_DEBOUNCE_MS     30
#define PWR_LONG_PRESS_MS   3000
#define BATTERY_POLL_MS     2000
#define CHARGING_POLL_MS    500

static bool     last_level       = true;
static uint32_t last_change_ms   = 0;
static uint32_t press_start_ms   = 0;
static bool     pwr_pressed_flag = false;
static bool     shutdown_armed   = false;

static int      cached_pct       = -1;
static bool     cached_charging  = false;
static uint32_t last_battery_ms  = 0;
static uint32_t last_charging_ms = 0;

static adc_oneshot_unit_handle_t adc_handle  = nullptr;
static adc_cali_handle_t         adc_cali    = nullptr;
static bool                      adc_cal_ok  = false;
static adc_channel_t             adc_channel = ADC_CHANNEL_0;

static float read_battery_voltage(void) {
    if (!adc_handle) return 4.2f;
    int raw = 0;
    if (adc_oneshot_read(adc_handle, adc_channel, &raw) != ESP_OK) return 4.2f;
    if (adc_cal_ok) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(adc_cali, raw, &mv) == ESP_OK) {
            return (mv / 1000.0f) * 3.0f;   // ×3 voltage divider
        }
    }
    return 4.2f;
}

static int voltage_to_percent(float v) {
    // Stair-step from factory bsp_power_manager.c — coarse but stable.
    if (v < 3.52f) return 1;
    if (v < 3.64f) return 20;
    if (v < 3.76f) return 40;
    if (v < 3.88f) return 60;
    if (v < 4.00f) return 80;
    return 100;
}

void power_hal_init(void) {
    // Soft-power latch is already HIGH from board_init(); the PWR button
    // here is what we *read* to detect short / long press.
    pinMode(BTN_PWR_GPIO, INPUT_PULLUP);
    last_level = (digitalRead(BTN_PWR_GPIO) == HIGH);

    pinMode(BAT_CHARGING_GPIO, INPUT_PULLUP);

    // ADC1 channel 0 corresponds to GPIO 1 on the ESP32-S3.
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    if (adc_oneshot_new_unit(&unit_cfg, &adc_handle) == ESP_OK) {
        adc_channel = (adc_channel_t)(BAT_ADC_GPIO - 1);   // GPIO1 → ADC1_CH0
        adc_oneshot_chan_cfg_t chan_cfg = {};
        chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        chan_cfg.atten = ADC_ATTEN_DB_12;
        adc_oneshot_config_channel(adc_handle, adc_channel, &chan_cfg);

        adc_cali_curve_fitting_config_t cali_cfg = {};
        cali_cfg.unit_id = ADC_UNIT_1;
        cali_cfg.chan = adc_channel;
        cali_cfg.atten = ADC_ATTEN_DB_12;
        cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        adc_cal_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali) == ESP_OK);
    }

    cached_pct = voltage_to_percent(read_battery_voltage());
    cached_charging = (digitalRead(BAT_CHARGING_GPIO) == LOW);
    Serial.printf("Battery init: %d%% charging=%d\n", cached_pct, cached_charging);
}

void power_hal_tick(void) {
    bool level = (digitalRead(BTN_PWR_GPIO) == HIGH);
    uint32_t now = millis();

    if (level != last_level) {
        if (now - last_change_ms >= PWR_DEBOUNCE_MS) {
            last_change_ms = now;
            if (!level) {
                press_start_ms = now;
                shutdown_armed = false;
            } else {
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

    if (now - last_charging_ms >= CHARGING_POLL_MS) {
        last_charging_ms = now;
        cached_charging = (digitalRead(BAT_CHARGING_GPIO) == LOW);
    }
    if (now - last_battery_ms >= BATTERY_POLL_MS) {
        last_battery_ms = now;
        cached_pct = voltage_to_percent(read_battery_voltage());
    }
}

int  power_hal_battery_pct(void) { return cached_pct; }
bool power_hal_is_charging(void) { return cached_charging; }
bool power_hal_is_vbus_in(void)  { return cached_charging; }   // charger IC active ≈ USB plugged

bool power_hal_pwr_pressed(void) {
    if (pwr_pressed_flag) {
        pwr_pressed_flag = false;
        return true;
    }
    return false;
}
