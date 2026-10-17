// Pressure display on the LilyGo T-Display-S3 (ST7789, 8 bit parallel bus)
//
// Layout, 320x170 in landscape:
//
//   status                          192.168.0.x   XCP
//
//     1.08 bar                      <- large, the value you actually read
//
//   [========|                  ]   <- current value on a fixed scale
//   0                                6
//   Temp  21.4 C                    <- only with OPTION_TEMPERATUR
//
// Flicker is avoided two ways: the whole page is redrawn at most once per
// DISPLAY_PERIOD_MS, and every field is drawn with an opaque text background in
// a fixed-width format, so glyphs overwrite the previous content instead of
// being cleared first. Fields whose rendered text has not changed are skipped
// entirely.

#include "display.h"

#ifdef OPTION_DISPLAY

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <math.h>
#include <string.h>

#include "xcp.h" // NOP when !defined(OPTION_XCP)

#ifdef OPTION_ANALOG
#include "analog.h"
#endif

class Display : public lgfx::LGFX_Device
{
    lgfx::Bus_Parallel8 _bus;
    lgfx::Panel_ST7789 _panel;
    lgfx::Light_PWM _light;

public:
    Display()
    {
        {
            auto cfg = _bus.config();
            cfg.freq_write = 20000000;
            cfg.pin_wr = LCD_WR;
            cfg.pin_rd = LCD_RD;
            cfg.pin_rs = LCD_DC;
            cfg.pin_d0 = LCD_D0;
            cfg.pin_d1 = LCD_D1;
            cfg.pin_d2 = LCD_D2;
            cfg.pin_d3 = LCD_D3;
            cfg.pin_d4 = LCD_D4;
            cfg.pin_d5 = LCD_D5;
            cfg.pin_d6 = LCD_D6;
            cfg.pin_d7 = LCD_D7;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        {
            auto cfg = _panel.config();
            cfg.pin_cs = LCD_CS;
            cfg.pin_rst = LCD_RES;
            cfg.pin_busy = -1;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 170;
            cfg.panel_height = 320;
            cfg.offset_x = 35;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.invert = true;
            cfg.rgb_order = false;
            _panel.config(cfg);
        }

        {
            auto cfg = _light.config();
            cfg.pin_bl = LCD_BL;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        setPanel(&_panel);
    }
};

static Display lcd;
static SemaphoreHandle_t lcdMutex = nullptr;

// The built-in font cell is 6x8 pixels before scaling
static constexpr int32_t GLYPH_W = 6;
static constexpr int32_t GLYPH_H = 8;

static constexpr int32_t STATUS_SIZE = 2; // 12x16
static constexpr int32_t VALUE_SIZE = 6;  // 36x48
static constexpr int32_t UNIT_SIZE = 3;   // 18x24
static constexpr int32_t LABEL_SIZE = 2;  // 12x16

static constexpr int32_t STATUS_Y = 4;
static constexpr int32_t VALUE_X = 14;
static constexpr int32_t VALUE_Y = 38;
static constexpr int32_t BAR_X = 14;
static constexpr int32_t BAR_H = 14;
// The panel is 170 pixels high: the value ends at 86, and the bar and its
// scale sit just high enough to leave a 16 pixel line for the temperature.
static constexpr int32_t BAR_Y = 97;
static constexpr int32_t LABEL_Y = 117;
static constexpr int32_t TEMP_Y = 143;

static constexpr uint16_t COLOR_BG = TFT_BLACK;
static constexpr uint16_t COLOR_VALUE = TFT_WHITE;
static constexpr uint16_t COLOR_UNIT = TFT_DARKGREY;
static constexpr uint16_t COLOR_STATUS = TFT_DARKGREY;
static constexpr uint16_t COLOR_SCALE = TFT_DARKGREY;
static constexpr uint16_t COLOR_BAR = TFT_GREEN;
static constexpr uint16_t COLOR_BAR_TRACK = TFT_DARKGREY;
static constexpr uint16_t COLOR_ALERT = TFT_RED;
static constexpr uint16_t COLOR_TEMP = TFT_CYAN;

// Cached rendering, so unchanged fields are not redrawn
static char lastStatus[32] = "";
static char lastValue[16] = "";
#ifdef OPTION_TEMPERATUR
static char lastTemperature[16] = "";
#endif
static int32_t lastMarker = -2; // -1 means 'no value'; -2 forces the first draw
static uint32_t lastRenderMs = 0;

static int32_t barWidth(void) { return lcd.width() - 2 * BAR_X; }

// Draw text with an opaque background so it overwrites whatever was there.
// Callers pass fixed-width strings, so no separate clear is needed.
static void drawText(int32_t x, int32_t y, int32_t size, uint16_t color, const char *text)
{
    lcd.setTextSize(size);
    lcd.setTextColor(color, COLOR_BG);
    lcd.setCursor(x, y);
    lcd.print(text);
}

// Always render into the same number of characters. Fixed width is what lets
// the opaque text background overwrite the previous value without clearing the
// area first; a string that grew and later shrank would otherwise leave the
// tail of the longer one behind.
#define PRESSURE_FIELD_WIDTH 6

static void formatPressure(char *out, size_t len, float value, int decimals)
{
    if (isnan(value))
    {
        snprintf(out, len, "%*s", PRESSURE_FIELD_WIDTH, "---");
    }
    else
    {
        snprintf(out, len, "%*.*f", PRESSURE_FIELD_WIDTH, decimals, (double)value);
    }
}

void displayInit(void)
{
    pinMode(LCD_POWER_ON, OUTPUT);
    digitalWrite(LCD_POWER_ON, HIGH);
    lcdMutex = xSemaphoreCreateMutex();
    lcd.init();
    lcd.setRotation(1);
    lcd.setBrightness(180);
    lcd.fillScreen(COLOR_BG);
    lcd.setTextWrap(false);

    // Scale labels at both ends of the bar, they never change
    char label[16];
    snprintf(label, sizeof(label), "%g", (double)DISPLAY_BAR_MIN);
    drawText(BAR_X, LABEL_Y, LABEL_SIZE, COLOR_SCALE, label);
    snprintf(label, sizeof(label), "%g", (double)DISPLAY_BAR_MAX);
    drawText(lcd.width() - BAR_X - (int32_t)strlen(label) * GLYPH_W * LABEL_SIZE, LABEL_Y, LABEL_SIZE, COLOR_SCALE, label);

#ifdef OPTION_TEMPERATUR
    // Label and unit of the temperature line; the value in between is rendered
    // as a fixed 5 character field. The built-in font has no degree sign, so it
    // is drawn as a small circle.
    drawText(BAR_X, TEMP_Y, LABEL_SIZE, COLOR_SCALE, "Temp");
    const int32_t unitX = BAR_X + 11 * GLYPH_W * LABEL_SIZE;
    lcd.drawCircle(unitX + 4, TEMP_Y + 3, 2, COLOR_SCALE);
    drawText(unitX + 9, TEMP_Y, LABEL_SIZE, COLOR_SCALE, "C");
#endif
}

void displayError(const char *text)
{
    if (lcdMutex == nullptr || xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        return;
    }
    lcd.fillRect(0, VALUE_Y, lcd.width(), 48, COLOR_BG);
    drawText(VALUE_X, VALUE_Y, STATUS_SIZE, COLOR_ALERT, text);
    xSemaphoreGive(lcdMutex);
}

static void renderStatus(void)
{
    char status[32];

#ifdef OPTION_XCP
    if (xcpStatusText() != nullptr)
    {
        // A tool is attached; show that instead of the address
        snprintf(status, sizeof(status), "%-21s", xcpStatusText());
    }
    else
#endif
        if (WiFi.status() == WL_CONNECTED)
    {
        snprintf(status, sizeof(status), "%-21s", WiFi.localIP().toString().c_str());
    }
    else
    {
        snprintf(status, sizeof(status), "%-21s", "no network");
    }

    if (strcmp(status, lastStatus) != 0)
    {
        strncpy(lastStatus, status, sizeof(lastStatus) - 1);
        lastStatus[sizeof(lastStatus) - 1] = '\0';
#ifdef OPTION_ANALOG
        const uint16_t color = analogIsPresent() ? COLOR_STATUS : COLOR_ALERT;
#else
        const uint16_t color = COLOR_STATUS;
#endif
        drawText(BAR_X, STATUS_Y, STATUS_SIZE, color, status);
    }
}

static void renderValue(float pressure)
{
    char value[16];
    formatPressure(value, sizeof(value), pressure, 2);

    if (strcmp(value, lastValue) != 0)
    {
        strncpy(lastValue, value, sizeof(lastValue) - 1);
        lastValue[sizeof(lastValue) - 1] = '\0';
        drawText(VALUE_X, VALUE_Y, VALUE_SIZE, COLOR_VALUE, value);
        // The unit sits after the digits, aligned to the bottom of the value
        const int32_t valueW = (int32_t)strlen(value) * GLYPH_W * VALUE_SIZE;
        drawText(VALUE_X + valueW + 8, VALUE_Y + (GLYPH_H * VALUE_SIZE) - (GLYPH_H * UNIT_SIZE), UNIT_SIZE, COLOR_UNIT, "bar");
    }
}

static_assert(DISPLAY_BAR_MAX > DISPLAY_BAR_MIN, "DISPLAY_BAR_MAX must be above DISPLAY_BAR_MIN");

static void renderBar(float pressure)
{
    // Bar: where the current value sits on the fixed scale, clamped at both ends
    int32_t marker = -1;
    if (!isnan(pressure))
    {
        const float fraction = (pressure - DISPLAY_BAR_MIN) / (DISPLAY_BAR_MAX - DISPLAY_BAR_MIN);
        marker = (int32_t)(fraction * (float)(barWidth() - 2));
        if (marker < 0)
        {
            marker = 0;
        }
        else if (marker > barWidth() - 2)
        {
            marker = barWidth() - 2;
        }
    }

    if (marker != lastMarker)
    {
        lastMarker = marker;
        lcd.drawRect(BAR_X, BAR_Y, barWidth(), BAR_H, COLOR_BAR_TRACK);
        if (marker >= 0)
        {
            lcd.fillRect(BAR_X + 1, BAR_Y + 1, marker, BAR_H - 2, COLOR_BAR);
            lcd.fillRect(BAR_X + 1 + marker, BAR_Y + 1, barWidth() - 2 - marker, BAR_H - 2, COLOR_BG);
        }
        else
        {
            lcd.fillRect(BAR_X + 1, BAR_Y + 1, barWidth() - 2, BAR_H - 2, COLOR_BG);
        }
    }
}

#ifdef OPTION_TEMPERATUR
static void renderTemperature(float temperature)
{
    // The DS18B20 range is -55 to 125, so 5 characters always fit
    char text[16];
    if (isnan(temperature))
    {
        snprintf(text, sizeof(text), "%5s", "---");
    }
    else
    {
        snprintf(text, sizeof(text), "%5.1f", (double)temperature);
    }

    if (strcmp(text, lastTemperature) != 0)
    {
        strncpy(lastTemperature, text, sizeof(lastTemperature) - 1);
        lastTemperature[sizeof(lastTemperature) - 1] = '\0';
        drawText(BAR_X + 5 * GLYPH_W * LABEL_SIZE, TEMP_Y, LABEL_SIZE, COLOR_TEMP, text);
    }
}
#endif

void displayUpdate(float pressure, float temperature)
{
    const uint32_t now = millis();
    if ((uint32_t)(now - lastRenderMs) < DISPLAY_PERIOD_MS)
    {
        return;
    }
    lastRenderMs = now;

    if (lcdMutex == nullptr || xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        return;
    }

    renderStatus();
    renderValue(pressure);
    renderBar(pressure);
#ifdef OPTION_TEMPERATUR
    renderTemperature(temperature);
#else
    (void)temperature;
#endif

    xSemaphoreGive(lcdMutex);
}

#endif
