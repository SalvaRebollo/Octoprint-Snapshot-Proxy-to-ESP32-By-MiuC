#include "app_theme.h"

#include <Preferences.h>
#include <lvgl.h>

namespace AppTheme {
namespace {
constexpr const char *NVS_NAMESPACE = "appui";
constexpr const char *NVS_DARK_KEY = "dark";
constexpr const char *NVS_COLOR_KEY = "primary";
constexpr bool DEFAULT_DARK_MODE = true;
constexpr uint8_t DEFAULT_PRIMARY_COLOR = 0;

struct PrimaryColor {
  const char *name;
  uint32_t rgb;
};

// Tonos deliberadamente oscuros para mantener contraste con texto blanco.
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

bool isValidPrimaryColor(uint8_t index) {
  return index < PRIMARY_COLOR_COUNT;
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

bool saveDarkMode() {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;
  size_t written = preferences.putBool(NVS_DARK_KEY, darkMode);
  preferences.end();
  return written > 0;
}

bool savePrimaryColor() {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;
  size_t written = preferences.putUChar(NVS_COLOR_KEY, selectedPrimaryColor);
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
    preferences.end();
  } else {
    darkMode = DEFAULT_DARK_MODE;
    selectedPrimaryColor = DEFAULT_PRIMARY_COLOR;
  }

  if (!isValidPrimaryColor(selectedPrimaryColor)) {
    selectedPrimaryColor = DEFAULT_PRIMARY_COLOR;
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
  return saveDarkMode();
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
  return savePrimaryColor();
}
}