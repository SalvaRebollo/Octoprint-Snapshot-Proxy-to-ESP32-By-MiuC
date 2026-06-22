#include "app_theme.h"

#include <Preferences.h>
#include <lvgl.h>

#include "app_config.h"

namespace AppTheme {
namespace {
constexpr const char *NVS_NAMESPACE = "appui";
constexpr const char *NVS_DARK_KEY = "dark";
constexpr const char *NVS_COLOR_KEY = "primary";
constexpr const char *NVS_TAB_HEIGHT_KEY = "tabheight";
constexpr const char *NVS_PERF_MONITOR_KEY = "showperf";
constexpr const char *NVS_COUNTER_TAB_KEY = "tabcounter";
constexpr const char *NVS_DOMOTICA_TAB_KEY = "tabdomotica";
constexpr const char *NVS_OCTOPRINT_TAB_KEY = "taboctoprint";
constexpr const char *NVS_LAST_TAB_KEY = "lasttab";
constexpr bool DEFAULT_DARK_MODE = true;
constexpr uint8_t DEFAULT_PRIMARY_COLOR = 0;
constexpr uint16_t MIN_TAB_BAR_HEIGHT = 20;
constexpr uint16_t MAX_TAB_BAR_HEIGHT = 50;
constexpr bool DEFAULT_SHOW_PERFORMANCE_MONITOR = true;
constexpr bool DEFAULT_SHOW_COUNTER_TAB = false;
constexpr bool DEFAULT_SHOW_DOMOTICA_TAB = true;
constexpr bool DEFAULT_SHOW_OCTOPRINT_TAB = true;
constexpr AppPage DEFAULT_LAST_ACTIVE_PAGE = AppPage::OCTOPRINT;

struct PrimaryColor {
  const char *name;
  uint32_t rgb;
};

// Intentionally dark shades to keep enough contrast against white text.
constexpr PrimaryColor PRIMARY_COLORS[] = {
  {"Azul",     0x1565C0},
  {"Verde",    0x2E7D32},
  {"Naranja",  0xC2410C},
  {"Turquesa", 0x00796B},
  {"Morado",   0x6A1B9A},
  {"Rojo",     0xC62828}
};

constexpr uint8_t PRIMARY_COLOR_COUNT =
  sizeof(PRIMARY_COLORS) / sizeof(PRIMARY_COLORS[0]);

bool darkMode = DEFAULT_DARK_MODE;
uint8_t selectedPrimaryColor = DEFAULT_PRIMARY_COLOR;
uint16_t selectedTabBarHeight = APP_TAB_BAR_HEIGHT;
bool performanceMonitorVisible = DEFAULT_SHOW_PERFORMANCE_MONITOR;
bool counterTabVisible = DEFAULT_SHOW_COUNTER_TAB;
bool domoticaTabVisible = DEFAULT_SHOW_DOMOTICA_TAB;
bool octoPrintTabVisible = DEFAULT_SHOW_OCTOPRINT_TAB;
AppPage lastActivePageValue = DEFAULT_LAST_ACTIVE_PAGE;

bool isValidPrimaryColor(uint8_t index) {
  return index < PRIMARY_COLOR_COUNT;
}

bool isValidTabBarHeight(uint16_t height) {
  return height >= MIN_TAB_BAR_HEIGHT && height <= MAX_TAB_BAR_HEIGHT;
}

void apply() {
  lv_disp_t *display = lv_disp_get_default();
  if (display == nullptr) return;

  lv_color_t primary = lv_color_hex(PRIMARY_COLORS[selectedPrimaryColor].rgb);
  lv_color_t secondary = lv_color_hex(0x455A64);

  lv_theme_t *theme = lv_theme_default_init(
    display,
    primary,
    secondary,
    darkMode,
    LV_FONT_DEFAULT
  );
  lv_disp_set_theme(display, theme);
  lv_obj_report_style_change(nullptr);

  lv_obj_t *screen = lv_scr_act();
  if (screen != nullptr) lv_obj_invalidate(screen);
}

// Writes a single preference value to NVS (one value per key).
bool writePreference(const char *key, bool value) {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;
  size_t written = preferences.putBool(key, value);
  preferences.end();
  return written > 0;
}

bool writePreference(const char *key, uint8_t value) {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;
  size_t written = preferences.putUChar(key, value);
  preferences.end();
  return written > 0;
}

bool writePreference(const char *key, uint16_t value) {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;
  size_t written = preferences.putUShort(key, value);
  preferences.end();
  return written > 0;
}
}

void begin() {
  Preferences preferences;
  if (preferences.begin(NVS_NAMESPACE, true)) {
    darkMode = preferences.getBool(NVS_DARK_KEY, DEFAULT_DARK_MODE);
    selectedPrimaryColor = preferences.getUChar(
      NVS_COLOR_KEY,
      DEFAULT_PRIMARY_COLOR
    );
    selectedTabBarHeight = preferences.getUShort(
      NVS_TAB_HEIGHT_KEY,
      APP_TAB_BAR_HEIGHT
    );
    performanceMonitorVisible = preferences.getBool(
      NVS_PERF_MONITOR_KEY,
      DEFAULT_SHOW_PERFORMANCE_MONITOR
    );
    counterTabVisible = preferences.getBool(
      NVS_COUNTER_TAB_KEY,
      DEFAULT_SHOW_COUNTER_TAB
    );
    domoticaTabVisible = preferences.getBool(
      NVS_DOMOTICA_TAB_KEY,
      DEFAULT_SHOW_DOMOTICA_TAB
    );
    octoPrintTabVisible = preferences.getBool(
      NVS_OCTOPRINT_TAB_KEY,
      DEFAULT_SHOW_OCTOPRINT_TAB
    );
    uint8_t storedPage = preferences.getUChar(
      NVS_LAST_TAB_KEY,
      static_cast<uint8_t>(DEFAULT_LAST_ACTIVE_PAGE)
    );
    lastActivePageValue = storedPage <= static_cast<uint8_t>(AppPage::SETTINGS)
      ? static_cast<AppPage>(storedPage)
      : DEFAULT_LAST_ACTIVE_PAGE;
    preferences.end();
  }
  // If NVS fails to open, all variables keep their initial default values.

  if (!isValidPrimaryColor(selectedPrimaryColor)) {
    selectedPrimaryColor = DEFAULT_PRIMARY_COLOR;
  }
  if (!isValidTabBarHeight(selectedTabBarHeight)) {
    selectedTabBarHeight = APP_TAB_BAR_HEIGHT;
  }
  apply();
}

bool isDarkMode() {
  return darkMode;
}

bool setDarkMode(bool enabled) {
  if (darkMode == enabled) return true;
  darkMode = enabled;
  apply();
  return writePreference(NVS_DARK_KEY, darkMode);
}

bool toggle() {
  return setDarkMode(!darkMode);
}

uint8_t primaryColorCount() {
  return PRIMARY_COLOR_COUNT;
}

uint8_t primaryColorIndex() {
  return selectedPrimaryColor;
}

const char *primaryColorName(uint8_t index) {
  if (!isValidPrimaryColor(index)) return "Desconocido";
  return PRIMARY_COLORS[index].name;
}

bool setPrimaryColor(uint8_t index) {
  if (!isValidPrimaryColor(index)) return false;
  if (selectedPrimaryColor == index) return true;
  selectedPrimaryColor = index;
  apply();
  return writePreference(NVS_COLOR_KEY, selectedPrimaryColor);
}

uint16_t tabBarHeight() {
  return selectedTabBarHeight;
}

uint16_t tabBarMinHeight() {
  return MIN_TAB_BAR_HEIGHT;
}

uint16_t tabBarMaxHeight() {
  return MAX_TAB_BAR_HEIGHT;
}

bool setTabBarHeight(uint16_t height) {
  if (!isValidTabBarHeight(height)) return false;
  if (selectedTabBarHeight == height) return true;
  selectedTabBarHeight = height;
  return writePreference(NVS_TAB_HEIGHT_KEY, selectedTabBarHeight);
}

bool showPerformanceMonitor() {
  return performanceMonitorVisible;
}

bool setShowPerformanceMonitor(bool enabled) {
  if (performanceMonitorVisible == enabled) return true;
  performanceMonitorVisible = enabled;
  return writePreference(NVS_PERF_MONITOR_KEY, performanceMonitorVisible);
}

bool showCounterTab() {
  return counterTabVisible;
}

bool setShowCounterTab(bool enabled) {
  if (counterTabVisible == enabled) return true;
  counterTabVisible = enabled;
  return writePreference(NVS_COUNTER_TAB_KEY, counterTabVisible);
}

bool showDomoticaTab() {
  return domoticaTabVisible;
}

bool setShowDomoticaTab(bool enabled) {
  if (domoticaTabVisible == enabled) return true;
  domoticaTabVisible = enabled;
  return writePreference(NVS_DOMOTICA_TAB_KEY, domoticaTabVisible);
}

bool showOctoPrintTab() {
  return octoPrintTabVisible;
}

bool setShowOctoPrintTab(bool enabled) {
  if (octoPrintTabVisible == enabled) return true;
  octoPrintTabVisible = enabled;
  return writePreference(NVS_OCTOPRINT_TAB_KEY, octoPrintTabVisible);
}

AppPage lastActivePage() {
  return lastActivePageValue;
}

bool setLastActivePage(AppPage page) {
  // Settings is never saved as the last tab; the last normal tab is kept instead.
  if (page == AppPage::SETTINGS) return true;
  if (lastActivePageValue == page) return true;
  lastActivePageValue = page;
  return writePreference(NVS_LAST_TAB_KEY, static_cast<uint8_t>(page));
}
}