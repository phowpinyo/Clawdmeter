# Waveshare ESP32-S3-Touch-LCD-1.54 — Developer Guide

Practical reference for building **new firmware projects** on this board.
Distilled from the Clawdmeter port — every gotcha here cost real time
to find. Read this once before starting, keep open while debugging.

> If you're porting *Clawdmeter itself* to a different board, see
> [`../porting/adding-a-board.md`](../porting/adding-a-board.md) instead.
> This document is for **starting fresh** on the LCD-1.54.

---

## 1. Hardware at a glance

| | |
|---|---|
| MCU | ESP32-S3R8 (Xtensa LX7 dual-core, 240 MHz) |
| RAM | 512 KB SRAM + 8 MB stacked PSRAM (OPI) |
| Flash | 16 MB |
| Display | 1.54" IPS 240×240, ST7789 over **4-wire SPI** (not QSPI) |
| Touch | CST816 capacitive over I2C (addr `0x15`) |
| IMU | QMI8658 6-axis over I2C (addr `0x6B`) |
| Audio | ES8311 codec + ES7210 echo-cancel + NS4150B amp + mic + speaker |
| Buttons | 3 — BOOT (GPIO 0), PLUS (GPIO 4), PWR (GPIO 5) |
| Battery | 3.7V Li-ion via MX1.25 2-pin + charger IC + soft-power latch |
| TF card | microSD slot |
| USB | USB-C (CDC + JTAG, no boot-mode dance needed) |
| Antenna | onboard 2.4 GHz chip antenna (Wi-Fi b/g/n + BLE 5) |

**SKUs**: 33866/33867 = no touch, **33868/33869 = with touch** (Clawdmeter
needs this version).

---

## 2. Pin map (single source of truth)

Extracted from the Waveshare 01_factory ESP-IDF demo. **The Waveshare
wiki page does NOT list these GPIOs in text** — they're only in the
demo source.

### Display (ST7789 SPI)

```c
#define LCD_DC      45
#define LCD_CS      21
#define LCD_SCLK    38
#define LCD_MOSI    39
#define LCD_MISO    -1     // unused
#define LCD_RESET   40
#define LCD_BL      46     // backlight, PWM via ledc
```

### I2C bus (shared touch + IMU)

```c
#define IIC_SDA     42
#define IIC_SCL     41
```

### Touch (CST816)

```c
#define TP_INT      48     // falling-edge interrupt
#define TP_RST      47
#define CST816_ADDR 0x15
```

### IMU (QMI8658)

```c
#define IMU_INT     6      // unused — easier to poll at ~10Hz
#define QMI8658_L_SLAVE_ADDRESS  0x6B
```

### Buttons (pull-up, active LOW)

```c
#define BTN_BOOT_GPIO  0    // BOOT label on PCB
#define BTN_PLUS_GPIO  4    // PLUS label on PCB
#define BTN_PWR_GPIO   5    // PWR  label on PCB (also drives the latch IC)
```

**Easy to mix up:** the `02_button_example.ino` lists GPIOs as `0, 5, 4`
in that order without labels — `0` is BOOT, but `5` is PWR and `4` is
PLUS, not the other way around. Verify against `01_factory/main/main.c`
which assigns `GPIO_NUM_5` to `pwr_btn` explicitly.

### Power / battery

```c
#define BAT_POWER_GPIO     2    // OUT — latch hold (HIGH = on, LOW = release)
#define BAT_ADC_GPIO       1    // ADC1_CH0 — battery voltage (×3 divider)
#define BAT_CHARGING_GPIO  3    // IN — LOW = charging
```

### Voltage → percent (stair-step from factory)

```c
if (v < 3.52f) return 1;
if (v < 3.64f) return 20;
if (v < 3.76f) return 40;
if (v < 3.88f) return 60;
if (v < 4.00f) return 80;
return 100;
```

---

## 3. Critical bring-up rules

Get these wrong and the board behaves badly in non-obvious ways.

### 3.1 Hold the power latch HIGH **before anything else**

```c
extern "C" void board_init(void) {
    pinMode(BAT_POWER_GPIO, OUTPUT);
    digitalWrite(BAT_POWER_GPIO, HIGH);   // MUST be first
    Wire.begin(IIC_SDA, IIC_SCL);
    // ... other init
}
```

If you skip this and the user releases the PWR button before your code
finishes booting (~20 ms window), the latch IC drops power and the
board dies. On USB it doesn't matter (USB bypasses the latch); on
battery it's fatal.

### 3.2 ST7789 `setRotation()` leaves a GRAM ghost

```c
// In your rotation-change handler:
gfx->setRotation(new_rotation);
gfx->fillScreen(0x0000);   // ← REQUIRED, wipes panel RAM
```

`setRotation()` only rewrites MADCTL; the panel's GRAM keeps the
previous frame, so the old orientation's pixels bleed through until
LVGL (or whoever) overwrites them. Always blank after a rotation
change.

### 3.3 Constructor must pass `row_offset2=80`

The ST7789 chip's GRAM is 240×320 but the panel is 240×240. At
rotation 0/2 the panel maps to GRAM (0,0)..(239,239); at rotation 1/3
the visible window has to skip 80 unused rows.

```c
gfx = new Arduino_ST7789(
    bus, LCD_RESET, 0 /*rot*/, true /*IPS*/, 240, 240,
    /*col_off1*/ 0, /*row_off1*/ 0, /*col_off2*/ 0, /*row_off2*/ 80);
```

Without those offsets, landscape rotations render the UI in only part
of the screen with a black band on one side.

### 3.4 Backlight is a GPIO, not a panel command

ST7789 has no brightness register. Use ledc PWM on GPIO 46:

```c
ledcAttach(LCD_BL, 5000 /*Hz*/, 8 /*bits*/);
ledcWrite(LCD_BL, 200);   // 0..255
```

### 3.5 IMU axis mapping is per-board

Don't copy `accel_to_rotation()` from another board and assume it
works. The QMI8658 on the LCD-1.54 is mounted 90°/180° rotated from
the Waveshare AMOLED-2.16. Verify with a debug log:

```c
Serial.printf("IMU ax=%+.2f ay=%+.2f az=%+.2f\n", ax, ay, az);
```

Hold the board at each physical orientation for ~10 s, record the
ax/ay readings, then derive the mapping. For Clawdmeter on LCD-1.54:

| User orientation | ax | ay | Wanted display rotation |
|---|---|---|---|
| USB bottom | ≈ 0 | -1 | 2 (portrait, "natural") |
| USB left | +1 | ≈ 0 | 3 (landscape CCW) |
| USB top | ≈ 0 | +1 | 0 (portrait flipped) |
| USB right | -1 | ≈ 0 | 1 (landscape CW) |

Also note: Arduino_GFX's `setRotation(1)` is **CCW**, not the standard
MADCTL CW. Verify visually.

### 3.6 PWR button has dual role

PWR is both a user-readable GPIO input (GPIO 5) and the hardware
trigger for the latch IC. Suggested handling:

```c
short press (< 3s) → in-app action (cycle screen, menu, etc.)
long press (≥ 3s) → digitalWrite(BAT_POWER_GPIO, LOW)   // power off
```

Suppress the short-press event when the long-press fires so the user
doesn't get both.

---

## 4. PlatformIO setup

### `platformio.ini` env

```ini
[env:waveshare_lcd_154]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38-1/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi          ; REQUIRED for OPI PSRAM
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
board_build.partitions = default_16MB.csv
upload_speed = 921600
monitor_speed = 115200

build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DBOARD_HAS_PSRAM
    ; NimBLE peripheral config (drop if you don't need BLE)
    -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=1
    -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0
    -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0
    -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=2
    ; LVGL config (drop if you don't need LVGL)
    -DLV_CONF_SKIP
    -DLV_COLOR_DEPTH=16
    -DLV_TICK_CUSTOM=1

lib_deps =
    moononournation/GFX Library for Arduino@^1.5.6
    lewisxhe/SensorLib@^0.2.6        ; for TouchDrvCSTXXX (CST816) + SensorQMI8658
    lvgl/lvgl@^9.2.0                  ; optional
    bblanchon/ArduinoJson@^7.0.0      ; optional
    h2zero/NimBLE-Arduino@^2.1.1      ; optional
```

**Two critical settings most ESP32 projects miss:**

1. **`board_build.arduino.memory_type = qio_opi`** — without this the
   PSRAM is invisible (`MALLOC_CAP_SPIRAM` returns NULL) and big
   framebuffers fail to allocate silently.
2. **pioarduino platform** (not standard `espressif32`) — GFX Library
   for Arduino needs Arduino Core 3.x's `esp32-hal-periman.h`. Standard
   `espressif32` ships Core 2.x.

### Flash + monitor

```bash
pio run -e waveshare_lcd_154                                              # build
pio run -e waveshare_lcd_154 -t upload --upload-port /dev/cu.usbmodem*    # flash macOS
pio device monitor --port /dev/cu.usbmodem* --baud 115200                 # serial
```

The board exposes native USB-CDC + JTAG — no boot-mode button dance
needed; `pio run -t upload` just works.

---

## 5. Recommended minimal project skeleton

```
my-project/
├── platformio.ini
└── src/
    ├── main.cpp                  setup() + loop()
    ├── board_init.cpp            power latch + Wire.begin (call from setup)
    ├── display.cpp               ST7789 + ledc backlight wrapper
    ├── touch.cpp                 CST816 ISR + getPoint
    ├── imu.cpp                   QMI8658 polling + axis remap
    ├── input.cpp                 BOOT/PLUS button polling
    └── power.cpp                 PWR button + ADC battery + charging
```

You can copy these modules directly from
`firmware/src/boards/waveshare_lcd_154/` in the Clawdmeter repo and
strip out the HAL header includes — they're the production-tested
versions.

---

## 6. Ready-to-paste skeletons

### 6.1 ST7789 + LVGL setup

```c
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#define BL_LEDC_FREQ 5000
#define BL_LEDC_RES  8

static Arduino_DataBus* bus = nullptr;
static Arduino_ST7789*  gfx = nullptr;

void display_init() {
    bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO);
    gfx = new Arduino_ST7789(bus, LCD_RESET, 0, true, 240, 240,
                              0, 0, 0, 80);                   // row_offset2=80
    gfx->begin();
    gfx->fillScreen(0x0000);

    ledcAttach(LCD_BL, BL_LEDC_FREQ, BL_LEDC_RES);
    ledcWrite(LCD_BL, 200);                                   // ~78% brightness
}
```

### 6.2 CST816 touch reader (interrupt + I2C)

```c
#include <TouchDrvCSTXXX.hpp>

static TouchDrvCSTXXX touch;
static volatile bool touch_data_ready = false;

static void IRAM_ATTR touch_isr() { touch_data_ready = true; }

void touch_init() {
    touch.setPins(TP_RST, TP_INT);
    touch.begin(Wire, CST816_ADDR, IIC_SDA, IIC_SCL);
    touch.setMaxCoordinates(240, 240);
    pinMode(TP_INT, INPUT_PULLUP);
    attachInterrupt(TP_INT, touch_isr, FALLING);
}

void touch_read(uint16_t* x, uint16_t* y, bool* pressed) {
    static uint16_t lx = 0, ly = 0;
    static bool lp = false;
    if (touch_data_ready) {
        touch_data_ready = false;
        int16_t tx[1], ty[1];
        uint8_t n = touch.getPoint(tx, ty, 1);
        lp = (n > 0);
        if (lp) { lx = tx[0]; ly = ty[0]; }
    }
    *x = lx; *y = ly; *pressed = lp;
}
```

### 6.3 IMU rotation (with the LCD-1.54 axis remap)

```c
#include <SensorQMI8658.hpp>

static SensorQMI8658 imu;

void imu_init() {
    imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_LOWPOWER_21Hz,
                            SensorQMI8658::LPF_MODE_3);
    imu.enableAccelerometer();
}

// Returns rotation quadrant 0..3, or 255 if ambiguous (face-up/down).
uint8_t imu_rotation(float ax, float ay) {
    if (fabsf(ax) < 0.5f && fabsf(ay) < 0.5f) return 255;
    if (fabsf(ay) > fabsf(ax)) return (ay > 0) ? 0 : 2;
    return (ax > 0) ? 3 : 1;
}
```

### 6.4 Power latch + long-press shutdown

```c
void power_init() {
    pinMode(BTN_PWR_GPIO, INPUT_PULLUP);
    // BAT_POWER_GPIO already driven HIGH by board_init
}

void power_tick() {
    static uint32_t press_start = 0;
    static bool armed = false;
    bool pressed = (digitalRead(BTN_PWR_GPIO) == LOW);
    uint32_t now = millis();

    if (pressed && press_start == 0) press_start = now;
    if (!pressed) { press_start = 0; armed = false; }
    if (pressed && !armed && (now - press_start >= 3000)) {
        armed = true;
        digitalWrite(BAT_POWER_GPIO, LOW);   // release latch → power off
    }
}
```

### 6.5 Battery voltage (ADC)

```c
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

static adc_oneshot_unit_handle_t adc_handle = nullptr;
static adc_cali_handle_t adc_cali = nullptr;
static adc_channel_t adc_chan = ADC_CHANNEL_0;

void battery_init() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1,
                                            .ulp_mode = ADC_ULP_MODE_DISABLE};
    adc_oneshot_new_unit(&unit_cfg, &adc_handle);
    adc_chan = (adc_channel_t)(BAT_ADC_GPIO - 1);   // GPIO1 → ADC1_CH0
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12,
                                       .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_oneshot_config_channel(adc_handle, adc_chan, &chan_cfg);

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1, .chan = adc_chan,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali);
}

float battery_voltage() {
    int raw = 0, mv = 0;
    adc_oneshot_read(adc_handle, adc_chan, &raw);
    adc_cali_raw_to_voltage(adc_cali, raw, &mv);
    return (mv / 1000.0f) * 3.0f;   // ×3 divider
}
```

---

## 7. UI sizing for 240×240

This panel is **smaller than most demo apps assume** (most ESP32 LVGL
demos target 320×240 or 480×480). Plan for:

- **Cap title font** at ~28 px (styrene_28 / similar). Tiempos_34 +
  flows often overshoot the top edge.
- **Avoid 48×48 status icons** in corners — they crowd everything.
  Scale to ~24-32 px or position carefully.
- **content_y ~30 px** leaves room for title + ~200 px for content.
- **Two stacked panels** of ~100 px each + 6 px gap fit comfortably.
- **Hide tertiary chrome** (credits, decorative anim text) — there's
  no room.

---

## 8. BLE peripheral (NimBLE-Arduino)

If you need BLE — Clawdmeter has a working "Claude Controller" peripheral
in `firmware/src/ble.cpp` you can model. Key tips:

- Use NimBLE (`h2zero/NimBLE-Arduino`), not the legacy Arduino-Bluetooth
  — much lower flash + RAM.
- Set `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2` if you also expose HID (lets
  the OS hold the HID link while your app holds the data service).
- Use a 16-bit characteristic for ack/nack signals, JSON-over-write for
  payloads.
- Cache MAC on the host side, but be ready for fresh discovery — ESP32-S3
  BLE MACs are factory-burned per chip, so any board swap invalidates
  cached pairings.

---

## 9. Touch + LVGL integration

LVGL 9 input device wiring (you can copy from `main.cpp` in the
Clawdmeter repo, but the essence is):

```c
static void my_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    uint16_t x, y; bool pressed;
    touch_read(&x, &y, &pressed);
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

lv_indev_t* indev = lv_indev_create();
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, my_touch_cb);
```

**Hard requirement:** `touch_read()` must return in well under 5 ms
(LVGL polls every screen refresh; any I2C burst longer than a screen
tick will visibly stutter). One I2C transaction per call is fine —
don't call `touch.getPoint()` from anywhere else.

---

## 10. Self-screenshot for visual QA

Clawdmeter ships a `screenshot` serial command that dumps the LVGL
framebuffer as raw RGB565 over USB CDC; `./screenshot.sh out.png` on
the host wraps it into a PNG sized to the panel (240×240).

**Strongly recommended pattern** for any new UI project — saves
hundreds of "press the button + photograph + send to me" round trips.
Copy the implementation from `firmware/src/main.cpp` (search for
`"screenshot"`) and adapt the output size.

To screenshot a specific app screen on first flash, temporarily change
your initial screen in `setup()` and revert before committing.

---

## 11. Reference URLs

- **Wiki**: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.54
- **Demos (Arduino + ESP-IDF)**: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.54
- **Spotpear user guide** (button labels + power semantics in English):
  https://spotpear.com/wiki/ESP32-S3-1.54inch-LCD-Display-TouchScreen-AI-DeepSeek.html
- **Arduino_GFX library docs** (ST7789 supported variants):
  https://github.com/moononournation/Arduino_GFX
- **SensorLib** (CST816, QMI8658):
  https://github.com/lewisxhe/SensorLib

The most useful file in the Waveshare GitHub repo, hands-down, is
`examples/ESP32-S3-Touch-LCD-1.54-demo/ESP-IDF-5.5.1/01_factory/components/esp_bsp/bsp_power_manager.c`.
It's where the GPIO 2 / 3 / 1 power assignments come from — none of
that is in the wiki text.

---

## 12. Project ideas for this board (you have the BSP, go)

The screen + IMU + buttons + BLE + audio combo is plenty for:

- **Desk-side status dashboards** (Clawdmeter, weather, GitHub notifs,
  Calendar next-event countdown)
- **Bluetooth HID accessories** (custom macropad with screen feedback,
  push-to-talk button)
- **IoT control surfaces** (Home Assistant tile remote)
- **AI voice toys** (mic + speaker + Wi-Fi → Gemini Live API,
  Xiaozhi-style)
- **Compact data loggers** (write IMU + audio to TF card, BLE upload)

The pattern: pick which BSP modules you need, copy them out of
Clawdmeter, write your app on top.
