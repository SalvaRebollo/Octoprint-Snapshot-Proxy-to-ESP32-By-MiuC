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

bool showClockTab();
bool setShowClockTab(bool enabled);

// Screen brightness (10–100 %). Saved to NVS.
uint8_t brightness();
bool    setBrightness(uint8_t percent);

// Brightness used when dimmed by inactivity (5–80 %).
uint8_t dimBrightness();
bool    setDimBrightness(uint8_t percent);

// Seconds of inactivity before dimming (0 = disabled).
uint16_t dimTimeoutSecs();
bool     setDimTimeoutSecs(uint16_t secs);

// Seconds of inactivity before switching to the clock tab (0 = disabled).
uint16_t clockTimeoutSecs();
bool     setClockTimeoutSecs(uint16_t secs);

// Last normal active tab, restored on boot. Settings and Clock are never saved.
AppPage lastActivePage();
bool setLastActivePage(AppPage page);
}