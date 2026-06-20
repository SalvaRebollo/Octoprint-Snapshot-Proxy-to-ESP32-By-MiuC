#pragma once

#include <stdint.h>

// Cambia a 0 para construir una plantilla sin la pantalla ni la logica OctoPrint.
#define APP_ENABLE_OCTOPRINT 1

constexpr uint16_t APP_SCREEN_WIDTH = 480;
constexpr uint16_t APP_SCREEN_HEIGHT = 272;
constexpr uint16_t APP_TAB_BAR_HEIGHT = 20;//36;