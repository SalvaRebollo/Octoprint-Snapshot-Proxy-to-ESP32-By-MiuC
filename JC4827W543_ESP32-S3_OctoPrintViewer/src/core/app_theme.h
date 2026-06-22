#pragma once

#include <stdint.h>

#include "app_navigation.h"

namespace AppTheme {
void begin();
bool isDarkMode();
bool setDarkMode(bool enabled);
bool toggle();

uint8_t primaryColorCount();
uint8_t primaryColorIndex();
const char *primaryColorName(uint8_t index);
bool setPrimaryColor(uint8_t index);

uint16_t tabBarHeight();
uint16_t tabBarMinHeight();
uint16_t tabBarMaxHeight();
bool setTabBarHeight(uint16_t height);

bool showPerformanceMonitor();
bool setShowPerformanceMonitor(bool enabled);

bool showCounterTab();
bool setShowCounterTab(bool enabled);
bool showDomoticaTab();
bool setShowDomoticaTab(bool enabled);
bool showOctoPrintTab();
bool setShowOctoPrintTab(bool enabled);

// Ultima pestaña normal activa, restaurada al arrancar. Ajustes nunca se guarda.
AppPage lastActivePage();
bool setLastActivePage(AppPage page);
}