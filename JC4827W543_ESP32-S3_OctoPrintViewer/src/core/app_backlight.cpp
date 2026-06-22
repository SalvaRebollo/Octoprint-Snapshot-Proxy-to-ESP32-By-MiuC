#include "app_backlight.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "app_theme.h"

namespace AppBacklight {
namespace {
constexpr uint8_t  BL_PIN    = 1;
constexpr uint32_t LEDC_FREQ = 5000;
constexpr uint8_t  LEDC_BITS = 8;  // 8-bit resolution → 0-255

bool dimmed = false;

void applyDuty(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledcWrite(BL_PIN, (uint32_t)percent * 255u / 100u);
}
}

void begin() {
  // ESP32 Arduino core v3+: ledcAttach(pin, freq, resolution_bits)
  ledcAttach(BL_PIN, LEDC_FREQ, LEDC_BITS);
  applyDuty(AppTheme::brightness());
}

void applyBrightness(uint8_t percent) {
  if (!dimmed) applyDuty(percent);
}

void dim() {
  if (dimmed) return;
  dimmed = true;
  applyDuty(AppTheme::dimBrightness());
}

void restore() {
  if (!dimmed) return;
  dimmed = false;
  applyDuty(AppTheme::brightness());
}

bool isDimmed() { return dimmed; }

void powerOff() {
  applyDuty(0);
  // No I2C PMIC on this board — deep sleep is the only soft power-off available.
  // Wake requires a hardware reset (physical power button).
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();
}

}
