#pragma once

#include <stdint.h>

// Set to 0 to build a template without the OctoPrint display and logic.
#define APP_ENABLE_OCTOPRINT 1
#define APP_ENABLE_DOMOTICA 1

constexpr uint16_t APP_SCREEN_WIDTH = 480;
constexpr uint16_t APP_SCREEN_HEIGHT = 272;
constexpr uint16_t APP_TAB_BAR_HEIGHT = 25;