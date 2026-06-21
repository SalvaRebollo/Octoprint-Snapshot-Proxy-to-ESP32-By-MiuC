#pragma once

#include <stdint.h>

namespace AppTheme {
void begin();
bool isDarkMode();
bool setDarkMode(bool enabled);
bool toggle();

uint8_t primaryColorCount();
uint8_t primaryColorIndex();
const char *primaryColorName(uint8_t index);
bool setPrimaryColor(uint8_t index);
}