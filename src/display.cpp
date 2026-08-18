// LilyGo T-Display-S3 status display (ST7789, 8 bit parallel bus)

#include "display.h"

#ifdef OPTION_DISPLAY

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <inttypes.h>
#include <math.h>

#include "xcplib.hpp"

#include "pressure_monitor.h"

#ifdef OPTION_ANALOG
#include "analog.h"
#endif

class Display : public lgfx::LGFX_Device {
    lgfx::Bus_Parallel8 _bus;
    lgfx::Panel_ST7789 _panel;
    lgfx::Light_PWM _light;

  public:
    Display() {
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
static constexpr int32_t DISPLAY_LINE_HEIGHT = 24;

void displayInit(void) {
    pinMode(LCD_POWER_ON, OUTPUT);
    digitalWrite(LCD_POWER_ON, HIGH);
    lcdMutex = xSemaphoreCreateMutex();
    lcd.init();
    lcd.setRotation(1);
    lcd.setBrightness(180);
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setTextWrap(false);
}

int32_t displayLineCount(void) { return (lcd.height() / DISPLAY_LINE_HEIGHT); }

void displayLine(int32_t line, const char *text, uint16_t color) {
    if (lcdMutex == nullptr || xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    const int32_t y = line * DISPLAY_LINE_HEIGHT;
    lcd.fillRect(0, y, lcd.width(), DISPLAY_LINE_HEIGHT, TFT_BLACK);
    lcd.setCursor(0, y + 4);
    lcd.setTextColor(color, TFT_BLACK);
    lcd.print(text);
    xSemaphoreGive(lcdMutex);
}

void displayError(const char *text) { displayLine(3, text, TFT_RED); }

void displayUpdate(uint32_t slowTaskPeriodMs, uint16_t slowCounter, uint32_t fastTaskPeriodMs, uint16_t fastCounter) {
    char line[40];

    if (XcpIsDaqRunning()) {
        snprintf(line, sizeof(line), "XCP DAQ running");
    } else if (XcpIsConnected()) {
        snprintf(line, sizeof(line), "XCP Connected");
    } else if (XcpIsStarted()) {
        snprintf(line, sizeof(line), "WiFi.IP %s", WiFi.localIP().toString().c_str());
    } else {
        snprintf(line, sizeof(line), "XCP Offline");
    }
    displayLine(displayLineCount() - 7, line, TFT_WHITE);

    if (isnan(pressure)) {
        snprintf(line, sizeof(line), "Pressure: ---");
    } else {
        snprintf(line, sizeof(line), "Pressure: %.3f bar", pressure);
    }
    displayLine(displayLineCount() - 6, line, TFT_MAGENTA);

#ifdef OPTION_ANALOG
    if (!analogIsPresent()) {
        snprintf(line, sizeof(line), "ADS1115: not found");
    } else if (isnan(pressure_sensor_voltage)) {
        snprintf(line, sizeof(line), "ADS1115: found");
    } else {
        snprintf(line, sizeof(line), "ADS1115: %.3f V", pressure_sensor_voltage);
    }
    displayLine(displayLineCount() - 5, line, analogIsPresent() ? TFT_CYAN : TFT_RED);
#endif

    snprintf(line, sizeof(line), "slowTask: %ums %u", slowTaskPeriodMs, slowCounter);
    displayLine(displayLineCount() - 4, line, TFT_YELLOW);

    snprintf(line, sizeof(line), "fastTask: %ums %u", fastTaskPeriodMs, fastCounter);
    displayLine(displayLineCount() - 3, line, TFT_RED);

    size_t rxStackSize = 0, txStackSize = 0;
    XcpEthServerDebugInfo(&rxStackSize, &txStackSize);
    snprintf(line, sizeof(line), "Stack: rx=%u, tx=%u", (uint16_t)rxStackSize, (uint16_t)txStackSize);
    displayLine(displayLineCount() - 2, line, TFT_BLUE);

    snprintf(line, sizeof(line), "XCP clock %" PRIu64 "", ApplXcpGetClock64());
    displayLine(displayLineCount() - 1, line, TFT_GREEN);
}

#endif
