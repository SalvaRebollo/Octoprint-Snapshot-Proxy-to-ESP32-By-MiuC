#pragma once

#include <stdint.h>

namespace AppBacklight {

// Call once in setup(), after AppTheme::begin().
void begin();

// Apply a brightness level to the hardware immediately (no NVS save).
// Has no effect while the screen is dimmed by inactivity.
void applyBrightness(uint8_t percent);

// Dim the backlight to the stored dim level (inactivity trigger).
void dim();

// Restore full brightness after dimming.
void restore();

bool isDimmed();

// Turn off backlight and enter deep sleep. Wakes on hardware reset/power button.
void powerOff();

}
