#include "tab_settings.h"

#include <WiFi.h>
#include <time.h>

#include "../../core/app_backlight.h"
#include "../../core/app_config.h"
#include "../../core/app_navigation.h"
#include "../../core/app_theme.h"
#include "../../core/app_ui.h"
#include "services/wifi_manager.h"

#if APP_ENABLE_OCTOPRINT
#include "../octoprint/features/octoprint_feature.h"
#endif

namespace SettingsTab {
namespace {
enum class SettingsCategory : intptr_t {
  APPEARANCE = 1,
  WIFI,
  OCTOPRINT,
  SYSTEM
};

constexpr uint16_t TIMEOUT_VALUES[] = {0, 30, 60, 120, 300, 600};
constexpr uint8_t  TIMEOUT_COUNT    = sizeof(TIMEOUT_VALUES) / sizeof(TIMEOUT_VALUES[0]);
constexpr const char *TIMEOUT_OPTIONS =
  "Desactivado\n30 segundos\n1 minuto\n2 minutos\n5 minutos\n10 minutos";

uint8_t secsToTimeoutIndex(uint16_t secs) {
  for (uint8_t i = 0; i < TIMEOUT_COUNT; i++) {
    if (TIMEOUT_VALUES[i] == secs) return i;
  }
  return 0;
}

uint16_t timeoutIndexToSecs(uint16_t index) {
  return index < TIMEOUT_COUNT ? TIMEOUT_VALUES[index] : 0;
}

lv_obj_t *categoryLayer = nullptr;
lv_obj_t *categoryTitle = nullptr;
lv_obj_t *appearancePage = nullptr;
lv_obj_t *wifiPage = nullptr;
lv_obj_t *systemPage = nullptr;

lv_obj_t *themeButton = nullptr;
lv_obj_t *themeStatus = nullptr;
lv_obj_t *primaryColorDropdown = nullptr;
lv_obj_t *tabBarHeightSlider = nullptr;
lv_obj_t *tabBarHeightValue = nullptr;
lv_obj_t *performanceMonitorSwitch = nullptr;
lv_obj_t *counterTabSwitch = nullptr;
lv_obj_t *domoticaTabSwitch = nullptr;
lv_obj_t *octoPrintTabSwitch = nullptr;
lv_obj_t *clockTabSwitch = nullptr;

lv_obj_t *brightnessSlider = nullptr;
lv_obj_t *brightnessValueLabel = nullptr;
lv_obj_t *dimBrightnessSlider = nullptr;
lv_obj_t *dimBrightnessValueLabel = nullptr;
lv_obj_t *dimTimeoutDropdown = nullptr;
lv_obj_t *clockTimeoutDropdown = nullptr;

lv_obj_t *wifiStatus = nullptr;
lv_obj_t *ipStatus = nullptr;
lv_obj_t *wifiSavedCount = nullptr;
lv_obj_t *wifiDialog = nullptr;
lv_obj_t *wifiDropdown = nullptr;
lv_obj_t *wifiPassword = nullptr;
lv_obj_t *wifiKeyboard = nullptr;
lv_obj_t *wifiDialogStatus = nullptr;

uint32_t displayedWifiScanGeneration = UINT32_MAX;
WifiManager::State previousWifiState = WifiManager::State::IDLE;
uint32_t lastNetworkUiMs = 0;
bool forceReconnect = false;

void updateAppearanceUi(bool saved = true) {
  if (themeButton != nullptr) {
    lv_obj_t *label = lv_obj_get_child(themeButton, 0);
    if (label != nullptr) {
      lv_label_set_text(
        label,
        AppTheme::isDarkMode() ? "ACTIVAR MODO CLARO" : "ACTIVAR MODO OSCURO"
      );
      lv_obj_center(label);
    }
  }

  if (primaryColorDropdown != nullptr) {
    lv_dropdown_set_selected(
      primaryColorDropdown,
      AppTheme::primaryColorIndex()
    );
  }

  if (tabBarHeightSlider != nullptr) {
    lv_slider_set_value(
      tabBarHeightSlider,
      AppTheme::tabBarHeight(),
      LV_ANIM_OFF
    );
  }
  if (tabBarHeightValue != nullptr) {
    lv_label_set_text_fmt(
      tabBarHeightValue,
      "%u px",
      AppTheme::tabBarHeight()
    );
  }
  if (performanceMonitorSwitch != nullptr) {
    if (AppTheme::showPerformanceMonitor()) {
      lv_obj_add_state(performanceMonitorSwitch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(performanceMonitorSwitch, LV_STATE_CHECKED);
    }
  }
  if (counterTabSwitch != nullptr) {
    if (AppTheme::showCounterTab()) lv_obj_add_state(counterTabSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(counterTabSwitch, LV_STATE_CHECKED);
  }
#if APP_ENABLE_DOMOTICA
  if (domoticaTabSwitch != nullptr) {
    if (AppTheme::showDomoticaTab()) lv_obj_add_state(domoticaTabSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(domoticaTabSwitch, LV_STATE_CHECKED);
  }
#endif
#if APP_ENABLE_OCTOPRINT
  if (octoPrintTabSwitch != nullptr) {
    if (AppTheme::showOctoPrintTab()) lv_obj_add_state(octoPrintTabSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(octoPrintTabSwitch, LV_STATE_CHECKED);
  }
#endif
  if (clockTabSwitch != nullptr) {
    if (AppTheme::showClockTab()) lv_obj_add_state(clockTabSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(clockTabSwitch, LV_STATE_CHECKED);
  }

  if (themeStatus != nullptr) {
    if (!saved) {
      lv_label_set_text(themeStatus, "Tema aplicado, pero no se pudo guardar");
    } else {
      lv_label_set_text_fmt(
        themeStatus,
        "Tema: %s / %s",
        AppTheme::isDarkMode() ? "Oscuro" : "Claro",
        AppTheme::primaryColorName(AppTheme::primaryColorIndex())
      );
    }
  }
}

void onPerformanceMonitorChanged(lv_event_t *event) {
  if (
    lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
    performanceMonitorSwitch == nullptr
  ) {
    return;
  }

  bool enabled = lv_obj_has_state(
    performanceMonitorSwitch,
    LV_STATE_CHECKED
  );
  bool saved = AppTheme::setShowPerformanceMonitor(enabled);
  appApplyPerformanceMonitorVisibility();
  updateAppearanceUi(saved);
}

void onTabVisibilityChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

  lv_obj_t *target = lv_event_get_target(event);
  bool visible = lv_obj_has_state(target, LV_STATE_CHECKED);
  bool saved = false;

  if (target == counterTabSwitch) {
    saved = AppTheme::setShowCounterTab(visible);
  }
#if APP_ENABLE_DOMOTICA
  else if (target == domoticaTabSwitch) {
    saved = AppTheme::setShowDomoticaTab(visible);
  }
#endif
#if APP_ENABLE_OCTOPRINT
  else if (target == octoPrintTabSwitch) {
    saved = AppTheme::setShowOctoPrintTab(visible);
  }
#endif
  else if (target == clockTabSwitch) {
    saved = AppTheme::setShowClockTab(visible);
  }
  else {
    return;
  }

  updateAppearanceUi(saved);
  appRebuildTabs();
}

void updateNetworkUi() {
  if (wifiStatus == nullptr || ipStatus == nullptr) return;

  if (wifiManager.isConnected()) {
    String ssid = WiFi.SSID();
    lv_label_set_text_fmt(wifiStatus, "WiFi: %s (%ld dBm)", ssid.c_str(), WiFi.RSSI());
    String ip = WiFi.localIP().toString();
    lv_label_set_text_fmt(ipStatus, "IP: %s", ip.c_str());
  } else {
    lv_label_set_text_fmt(wifiStatus, "WiFi: %s", wifiManager.statusText());
    lv_label_set_text(ipStatus, "IP: --");
  }

  if (wifiSavedCount != nullptr) {
    lv_label_set_text_fmt(
      wifiSavedCount,
      "Redes guardadas: %u/%u",
      wifiManager.savedCount(),
      WifiManager::MAX_SAVED_NETWORKS
    );
  }
}

void showCategory(SettingsCategory category) {
  if (
    categoryLayer == nullptr ||
    categoryTitle == nullptr ||
    appearancePage == nullptr ||
    wifiPage == nullptr ||
    systemPage == nullptr
  ) {
    return;
  }

  lv_obj_add_flag(appearancePage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(systemPage, LV_OBJ_FLAG_HIDDEN);

  switch (category) {
    case SettingsCategory::APPEARANCE:
      lv_label_set_text(categoryTitle, "Ajustes de apariencia");
      updateAppearanceUi();
      lv_obj_clear_flag(appearancePage, LV_OBJ_FLAG_HIDDEN);
      break;

    case SettingsCategory::WIFI:
      lv_label_set_text(categoryTitle, "Ajustes de WiFi");
      updateNetworkUi();
      lv_obj_clear_flag(wifiPage, LV_OBJ_FLAG_HIDDEN);
      break;

    case SettingsCategory::OCTOPRINT:
#if APP_ENABLE_OCTOPRINT
      lv_obj_add_flag(categoryLayer, LV_OBJ_FLAG_HIDDEN);
      OctoPrintFeature::showSettings();
#endif
      return;

    case SettingsCategory::SYSTEM:
      lv_label_set_text(categoryTitle, "Ajustes del sistema");
      lv_obj_clear_flag(systemPage, LV_OBJ_FLAG_HIDDEN);
      break;
  }

  lv_obj_clear_flag(categoryLayer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(categoryLayer);
}

void onOpenCategory(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  SettingsCategory category = static_cast<SettingsCategory>(
    reinterpret_cast<intptr_t>(lv_event_get_user_data(event))
  );
  showCategory(category);
}

void onCloseCategory(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || categoryLayer == nullptr) return;
  lv_obj_add_flag(categoryLayer, LV_OBJ_FLAG_HIDDEN);
}

void onThemeToggle(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  bool saved = AppTheme::toggle();
  appApplyTabViewAppearance();
  updateAppearanceUi(saved);
}

void onPrimaryColorChanged(lv_event_t *event) {
  if (
    lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
    primaryColorDropdown == nullptr
  ) {
    return;
  }

  uint16_t selected = lv_dropdown_get_selected(primaryColorDropdown);
  bool saved = AppTheme::setPrimaryColor(static_cast<uint8_t>(selected));
  appApplyTabViewAppearance();
  updateAppearanceUi(saved);
}

void onTabBarHeightChanged(lv_event_t *event) {
  if (tabBarHeightSlider == nullptr) return;

  lv_event_code_t code = lv_event_get_code(event);
  uint16_t height = static_cast<uint16_t>(
    lv_slider_get_value(tabBarHeightSlider)
  );

  if (code == LV_EVENT_VALUE_CHANGED) {
    if (tabBarHeightValue != nullptr) {
      lv_label_set_text_fmt(tabBarHeightValue, "%u px", height);
    }
    appPreviewTabBarHeight(height);
    return;
  }

  if (code == LV_EVENT_RELEASED) {
    bool saved = AppTheme::setTabBarHeight(height);
    appApplyTabViewAppearance();
    updateAppearanceUi(saved);
  }
}

void refreshWifiScanOptions() {
  if (wifiDropdown == nullptr) return;

  uint32_t generation = wifiManager.scanGeneration();
  if (generation == displayedWifiScanGeneration) return;
  displayedWifiScanGeneration = generation;

  String options;
  for (uint8_t i = 0; i < wifiManager.scanCount(); i++) {
    const WifiManager::ScanNetwork &network = wifiManager.scanNetwork(i);
    String ssid = network.ssid;
    ssid.replace("\n", " ");
    ssid.replace("\r", " ");

    if (!options.isEmpty()) options += "\n";
    options += ssid;
    options += "  ";
    options += String(network.rssi);
    options += " dBm";
    if (wifiManager.isSaved(network.ssid)) options += "  [guardada]";
    if (!network.secured) options += "  [abierta]";
  }

  if (options.isEmpty()) options = "No se encontraron redes";
  lv_dropdown_set_options(wifiDropdown, options.c_str());
  lv_dropdown_set_selected(wifiDropdown, 0);
}

void onReconnectClick(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) forceReconnect = true;
}

void onWifiPasswordEvent(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (
    (code != LV_EVENT_FOCUSED && code != LV_EVENT_CLICKED) ||
    wifiKeyboard == nullptr
  ) {
    return;
  }

  lv_obj_clear_flag(wifiKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(wifiKeyboard);
}

void onWifiKeyboardEvent(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (
    (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) ||
    wifiKeyboard == nullptr
  ) {
    return;
  }

  lv_obj_add_flag(wifiKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_state(wifiPassword, LV_STATE_FOCUSED);
}

void onOpenWifiDialog(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || wifiDialog == nullptr) return;
  lv_textarea_set_text(wifiPassword, "");
  lv_obj_add_flag(wifiKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(wifiDialogStatus, "Buscando redes...");
  lv_obj_clear_flag(wifiDialog, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(wifiDialog);
  displayedWifiScanGeneration = UINT32_MAX;
  wifiManager.startScan();
}

void onCloseWifiDialog(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || wifiDialog == nullptr) return;
  lv_textarea_set_text(wifiPassword, "");
  lv_obj_add_flag(wifiKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiDialog, LV_OBJ_FLAG_HIDDEN);
}

void onScanWifiClick(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_label_set_text(
    wifiDialogStatus,
    wifiManager.startScan() ? "Buscando redes..." : "El escaneo ya esta en curso"
  );
}

void onConnectWifiClick(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  uint16_t selected = lv_dropdown_get_selected(wifiDropdown);
  if (selected >= wifiManager.scanCount()) {
    lv_label_set_text(wifiDialogStatus, "Selecciona una red valida");
    return;
  }

  const WifiManager::ScanNetwork &network = wifiManager.scanNetwork(selected);
  String password = lv_textarea_get_text(wifiPassword);
  bool accepted = wifiManager.isSaved(network.ssid) && password.isEmpty()
    ? wifiManager.connectSaved(network.ssid)
    : wifiManager.saveAndConnect(network.ssid, password);

  lv_textarea_set_text(wifiPassword, "");
  if (accepted) {
    lv_label_set_text(wifiDialogStatus, "Red guardada. Conectando...");
    displayedWifiScanGeneration = UINT32_MAX;
    updateNetworkUi();
  } else {
    lv_label_set_text(wifiDialogStatus, "No se pudo guardar o conectar");
  }
}

void onForgetWifiClick(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  uint16_t selected = lv_dropdown_get_selected(wifiDropdown);
  if (selected >= wifiManager.scanCount()) {
    lv_label_set_text(wifiDialogStatus, "Selecciona una red valida");
    return;
  }

  const WifiManager::ScanNetwork &network = wifiManager.scanNetwork(selected);
  if (!wifiManager.isSaved(network.ssid)) {
    lv_label_set_text(wifiDialogStatus, "Esa red no esta guardada");
    return;
  }

  if (wifiManager.forgetNetwork(network.ssid)) {
    lv_label_set_text(wifiDialogStatus, "Red olvidada. Buscando de nuevo...");
    displayedWifiScanGeneration = UINT32_MAX;
    updateNetworkUi();
  } else {
    lv_label_set_text(wifiDialogStatus, "No se pudo olvidar la red");
  }
}

void createAppearancePage() {
  appearancePage = lv_obj_create(categoryLayer);
  lv_obj_set_pos(appearancePage, 0, 40);
  lv_obj_set_size(appearancePage, 480, 232);
  lv_obj_add_flag(appearancePage, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(appearancePage, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(appearancePage, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_border_width(appearancePage, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(appearancePage, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(appearancePage, 20, LV_PART_MAIN);

  lv_obj_t *modeLabel = lv_label_create(appearancePage);
  lv_label_set_text(modeLabel, "Modo de interfaz");
  lv_obj_set_pos(modeLabel, 15, 15);

  themeButton = appCreateButton(
    appearancePage,
    "",
    15,
    42,
    220,
    44,
    onThemeToggle
  );

  themeStatus = lv_label_create(appearancePage);
  lv_obj_set_pos(themeStatus, 250, 48);
  lv_obj_set_width(themeStatus, 215);
  lv_label_set_long_mode(themeStatus, LV_LABEL_LONG_WRAP);

  lv_obj_t *primaryColorLabel = lv_label_create(appearancePage);
  lv_label_set_text(primaryColorLabel, "Color principal");
  lv_obj_set_pos(primaryColorLabel, 15, 115);

  String primaryColorOptions;
  for (uint8_t i = 0; i < AppTheme::primaryColorCount(); i++) {
    if (!primaryColorOptions.isEmpty()) primaryColorOptions += "\n";
    primaryColorOptions += AppTheme::primaryColorName(i);
  }

  primaryColorDropdown = lv_dropdown_create(appearancePage);
  lv_dropdown_set_options(primaryColorDropdown, primaryColorOptions.c_str());
  lv_dropdown_set_selected(primaryColorDropdown, AppTheme::primaryColorIndex());
  lv_obj_set_pos(primaryColorDropdown, 140, 103);
  lv_obj_set_size(primaryColorDropdown, 190, 42);
  lv_obj_add_event_cb(
    primaryColorDropdown,
    onPrimaryColorChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

  lv_obj_t *tabBarHeightLabel = lv_label_create(appearancePage);
  lv_label_set_text(tabBarHeightLabel, "Altura de la barra superior");
  lv_obj_set_pos(tabBarHeightLabel, 15, 165);

  tabBarHeightValue = lv_label_create(appearancePage);
  lv_obj_set_width(tabBarHeightValue, 70);
  lv_obj_set_style_text_align(
    tabBarHeightValue,
    LV_TEXT_ALIGN_RIGHT,
    LV_PART_MAIN
  );
  lv_obj_set_pos(tabBarHeightValue, 385, 165);

  tabBarHeightSlider = lv_slider_create(appearancePage);
  lv_slider_set_range(
    tabBarHeightSlider,
    AppTheme::tabBarMinHeight(),
    AppTheme::tabBarMaxHeight()
  );
  lv_obj_set_pos(tabBarHeightSlider, 20, 198);
  lv_obj_set_size(tabBarHeightSlider, 430, 20);
  lv_obj_add_event_cb(
    tabBarHeightSlider,
    onTabBarHeightChanged,
    LV_EVENT_ALL,
    nullptr
  );

  lv_obj_t *hint = lv_label_create(appearancePage);
  lv_label_set_text(
    hint,
    "El color se aplica a botones, sliders y tabs activas."
  );
  lv_obj_set_width(hint, 450);
  lv_obj_set_pos(hint, 15, 240);

  lv_obj_t *performanceLabel = lv_label_create(appearancePage);
  lv_label_set_text(performanceLabel, "Mostrar FPS y CPU");
  lv_obj_set_pos(performanceLabel, 15, 285);

  performanceMonitorSwitch = lv_switch_create(appearancePage);
  lv_obj_set_pos(performanceMonitorSwitch, 190, 274);
  lv_obj_set_size(performanceMonitorSwitch, 58, 34);
  lv_obj_add_event_cb(
    performanceMonitorSwitch,
    onPerformanceMonitorChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

  lv_obj_t *tabsTitle = lv_label_create(appearancePage);
  lv_label_set_text(tabsTitle, "Tabs visibles");
  lv_obj_set_pos(tabsTitle, 15, 335);

  lv_obj_t *counterTabLabel = lv_label_create(appearancePage);
  lv_label_set_text(counterTabLabel, "Contador");
  lv_obj_set_pos(counterTabLabel, 15, 380);
  counterTabSwitch = lv_switch_create(appearancePage);
  lv_obj_set_pos(counterTabSwitch, 190, 369);
  lv_obj_set_size(counterTabSwitch, 58, 34);
  lv_obj_add_event_cb(
    counterTabSwitch,
    onTabVisibilityChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

#if APP_ENABLE_DOMOTICA
  lv_obj_t *domoticaTabLabel = lv_label_create(appearancePage);
  lv_label_set_text(domoticaTabLabel, "Domotica");
  lv_obj_set_pos(domoticaTabLabel, 15, 425);
  domoticaTabSwitch = lv_switch_create(appearancePage);
  lv_obj_set_pos(domoticaTabSwitch, 190, 414);
  lv_obj_set_size(domoticaTabSwitch, 58, 34);
  lv_obj_add_event_cb(
    domoticaTabSwitch,
    onTabVisibilityChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );
#endif

#if APP_ENABLE_OCTOPRINT
  lv_obj_t *octoPrintTabLabel = lv_label_create(appearancePage);
  lv_label_set_text(octoPrintTabLabel, "OctoPrint");
  lv_obj_set_pos(octoPrintTabLabel, 15, 470);
  octoPrintTabSwitch = lv_switch_create(appearancePage);
  lv_obj_set_pos(octoPrintTabSwitch, 190, 459);
  lv_obj_set_size(octoPrintTabSwitch, 58, 34);
  lv_obj_add_event_cb(
    octoPrintTabSwitch,
    onTabVisibilityChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );
#endif

  lv_obj_t *clockTabLabel = lv_label_create(appearancePage);
  lv_label_set_text(clockTabLabel, "Reloj");
  lv_obj_set_pos(clockTabLabel, 15, 515);
  clockTabSwitch = lv_switch_create(appearancePage);
  lv_obj_set_pos(clockTabSwitch, 190, 504);
  lv_obj_set_size(clockTabSwitch, 58, 34);
  lv_obj_add_event_cb(
    clockTabSwitch,
    onTabVisibilityChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

  appCreateSpacer(appearancePage, 0, 560, 1, 20);

  updateAppearanceUi();
  lv_obj_add_flag(appearancePage, LV_OBJ_FLAG_HIDDEN);
}

void onBrightnessChanged(lv_event_t *event) {
  if (brightnessSlider == nullptr) return;
  lv_event_code_t code = lv_event_get_code(event);
  uint8_t value = (uint8_t)lv_slider_get_value(brightnessSlider);

  if (code == LV_EVENT_VALUE_CHANGED) {
    if (brightnessValueLabel != nullptr)
      lv_label_set_text_fmt(brightnessValueLabel, "%u%%", value);
    AppBacklight::applyBrightness(value); // live preview
    return;
  }
  if (code == LV_EVENT_RELEASED) {
    AppTheme::setBrightness(value);
  }
}

void onDimBrightnessChanged(lv_event_t *event) {
  if (dimBrightnessSlider == nullptr) return;
  lv_event_code_t code = lv_event_get_code(event);
  uint8_t value = (uint8_t)lv_slider_get_value(dimBrightnessSlider);

  if (code == LV_EVENT_VALUE_CHANGED) {
    if (dimBrightnessValueLabel != nullptr)
      lv_label_set_text_fmt(dimBrightnessValueLabel, "%u%%", value);
    return;
  }
  if (code == LV_EVENT_RELEASED) {
    AppTheme::setDimBrightness(value);
  }
}

void onDimTimeoutChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || dimTimeoutDropdown == nullptr) return;
  uint16_t secs = timeoutIndexToSecs(lv_dropdown_get_selected(dimTimeoutDropdown));
  AppTheme::setDimTimeoutSecs(secs);
}

void onClockTimeoutChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || clockTimeoutDropdown == nullptr) return;
  uint16_t secs = timeoutIndexToSecs(lv_dropdown_get_selected(clockTimeoutDropdown));
  AppTheme::setClockTimeoutSecs(secs);
}

void onPowerOff(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  AppBacklight::powerOff();
}

void createSystemPage() {
  systemPage = lv_obj_create(categoryLayer);
  lv_obj_set_pos(systemPage, 0, 40);
  lv_obj_set_size(systemPage, 480, 232);
  lv_obj_add_flag(systemPage, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(systemPage, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(systemPage, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_border_width(systemPage, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(systemPage, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(systemPage, 20, LV_PART_MAIN);

  // --- Brightness ---
  lv_obj_t *brightLabel = lv_label_create(systemPage);
  lv_label_set_text(brightLabel, "Brillo de pantalla");
  lv_obj_set_pos(brightLabel, 15, 15);

  brightnessValueLabel = lv_label_create(systemPage);
  lv_obj_set_width(brightnessValueLabel, 55);
  lv_obj_set_style_text_align(brightnessValueLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_pos(brightnessValueLabel, 400, 15);
  lv_label_set_text_fmt(brightnessValueLabel, "%u%%", AppTheme::brightness());

  brightnessSlider = lv_slider_create(systemPage);
  lv_slider_set_range(brightnessSlider, 10, 100);
  lv_slider_set_value(brightnessSlider, AppTheme::brightness(), LV_ANIM_OFF);
  lv_obj_set_pos(brightnessSlider, 20, 48);
  lv_obj_set_size(brightnessSlider, 430, 20);
  lv_obj_add_event_cb(brightnessSlider, onBrightnessChanged, LV_EVENT_ALL, nullptr);

  // --- Dim on inactivity ---
  lv_obj_t *dimTimeoutLabel = lv_label_create(systemPage);
  lv_label_set_text(dimTimeoutLabel, "Atenuar pantalla tras inactividad");
  lv_obj_set_pos(dimTimeoutLabel, 15, 90);

  dimTimeoutDropdown = lv_dropdown_create(systemPage);
  lv_dropdown_set_options(dimTimeoutDropdown, TIMEOUT_OPTIONS);
  lv_dropdown_set_selected(dimTimeoutDropdown, secsToTimeoutIndex(AppTheme::dimTimeoutSecs()));
  lv_obj_set_pos(dimTimeoutDropdown, 15, 113);
  lv_obj_set_size(dimTimeoutDropdown, 200, 40);
  lv_obj_add_event_cb(dimTimeoutDropdown, onDimTimeoutChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *dimValLabel = lv_label_create(systemPage);
  lv_label_set_text(dimValLabel, "Brillo al atenuar");
  lv_obj_set_pos(dimValLabel, 15, 168);

  dimBrightnessValueLabel = lv_label_create(systemPage);
  lv_obj_set_width(dimBrightnessValueLabel, 55);
  lv_obj_set_style_text_align(dimBrightnessValueLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_pos(dimBrightnessValueLabel, 400, 168);
  lv_label_set_text_fmt(dimBrightnessValueLabel, "%u%%", AppTheme::dimBrightness());

  dimBrightnessSlider = lv_slider_create(systemPage);
  lv_slider_set_range(dimBrightnessSlider, 5, 80);
  lv_slider_set_value(dimBrightnessSlider, AppTheme::dimBrightness(), LV_ANIM_OFF);
  lv_obj_set_pos(dimBrightnessSlider, 20, 200);
  lv_obj_set_size(dimBrightnessSlider, 430, 20);
  lv_obj_add_event_cb(dimBrightnessSlider, onDimBrightnessChanged, LV_EVENT_ALL, nullptr);

  // --- Clock on inactivity ---
  lv_obj_t *clockTimeoutLabel = lv_label_create(systemPage);
  lv_label_set_text(clockTimeoutLabel, "Mostrar reloj tras inactividad");
  lv_obj_set_pos(clockTimeoutLabel, 15, 243);

  clockTimeoutDropdown = lv_dropdown_create(systemPage);
  lv_dropdown_set_options(clockTimeoutDropdown, TIMEOUT_OPTIONS);
  lv_dropdown_set_selected(clockTimeoutDropdown, secsToTimeoutIndex(AppTheme::clockTimeoutSecs()));
  lv_obj_set_pos(clockTimeoutDropdown, 15, 266);
  lv_obj_set_size(clockTimeoutDropdown, 200, 40);
  lv_obj_add_event_cb(clockTimeoutDropdown, onClockTimeoutChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *clockHint = lv_label_create(systemPage);
  lv_label_set_text(clockHint, "Activa el tab Reloj en Apariencia para usar esta opcion.");
  lv_obj_set_width(clockHint, 440);
  lv_obj_set_pos(clockHint, 15, 316);

  // --- Power off ---
  appCreateButton(systemPage, "APAGAR SISTEMA", 15, 360, 215, 50, onPowerOff);

  lv_obj_t *powerHint = lv_label_create(systemPage);
  lv_label_set_text(powerHint, "El sistema entra en modo de bajo consumo.\nUsa el boton fisico de encendido para reiniciar.");
  lv_obj_set_width(powerHint, 440);
  lv_obj_set_pos(powerHint, 15, 420);

  appCreateSpacer(systemPage, 0, 480, 1, 20);

  lv_obj_add_flag(systemPage, LV_OBJ_FLAG_HIDDEN);
}

void createWifiPage() {
  wifiPage = lv_obj_create(categoryLayer);
  lv_obj_set_pos(wifiPage, 0, 40);
  lv_obj_set_size(wifiPage, 480, 232);
  lv_obj_clear_flag(wifiPage, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(wifiPage, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(wifiPage, 0, LV_PART_MAIN);

  appCreateButton(wifiPage, "RECONECTAR", 15, 15, 150, 42, onReconnectClick);
  appCreateButton(wifiPage, "CONFIGURAR WIFI", 180, 15, 190, 42, onOpenWifiDialog);

  wifiStatus = lv_label_create(wifiPage);
  lv_obj_set_width(wifiStatus, 450);
  lv_obj_set_pos(wifiStatus, 15, 80);

  ipStatus = lv_label_create(wifiPage);
  lv_obj_set_pos(ipStatus, 15, 112);

  wifiSavedCount = lv_label_create(wifiPage);
  lv_obj_set_pos(wifiSavedCount, 15, 144);

  lv_obj_t *hint = lv_label_create(wifiPage);
  lv_label_set_text(hint, "Al arrancar se intenta conectar a la red guardada con mejor senal.");
  lv_obj_set_width(hint, 450);
  lv_obj_set_pos(hint, 15, 184);

  updateNetworkUi();
  lv_obj_add_flag(wifiPage, LV_OBJ_FLAG_HIDDEN);
}

void createWifiDialog() {
  wifiDialog = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(wifiDialog, 0, 0);
  lv_obj_set_size(wifiDialog, 480, 272);
  lv_obj_clear_flag(wifiDialog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(wifiDialog, lv_color_hex(0x101010), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(wifiDialog, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(wifiDialog, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(wifiDialog, 0, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(wifiDialog);
  lv_label_set_text(title, "Configurar WiFi");
  lv_obj_set_pos(title, 10, 9);
  lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);

  appCreateButton(wifiDialog, "CERRAR", 405, 3, 68, 31, onCloseWifiDialog);

  wifiDropdown = lv_dropdown_create(wifiDialog);
  lv_dropdown_set_options(wifiDropdown, "Buscando redes...");
  lv_obj_set_pos(wifiDropdown, 10, 38);
  lv_obj_set_size(wifiDropdown, 305, 40);
  appCreateButton(wifiDialog, "BUSCAR", 325, 38, 145, 40, onScanWifiClick);

  wifiPassword = lv_textarea_create(wifiDialog);
  lv_textarea_set_one_line(wifiPassword, true);
  lv_textarea_set_password_mode(wifiPassword, true);
  lv_textarea_set_max_length(wifiPassword, 63);
  lv_textarea_set_placeholder_text(wifiPassword, "Contrasena (vacia si ya esta guardada)");
  lv_obj_set_pos(wifiPassword, 10, 84);
  lv_obj_set_size(wifiPassword, 460, 38);
  lv_obj_add_event_cb(wifiPassword, onWifiPasswordEvent, LV_EVENT_ALL, nullptr);

  wifiDialogStatus = lv_label_create(wifiDialog);
  lv_label_set_text(wifiDialogStatus, "Selecciona una red");
  lv_obj_set_width(wifiDialogStatus, 460);
  lv_obj_set_pos(wifiDialogStatus, 10, 126);
  lv_obj_set_style_text_color(wifiDialogStatus, lv_color_white(), LV_PART_MAIN);

  appCreateButton(wifiDialog, "GUARDAR Y CONECTAR", 10, 148, 220, 34, onConnectWifiClick);
  appCreateButton(wifiDialog, "OLVIDAR", 240, 148, 105, 34, onForgetWifiClick);

  wifiKeyboard = lv_keyboard_create(wifiDialog);
  lv_keyboard_set_mode(wifiKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(wifiKeyboard, wifiPassword);
  lv_obj_set_size(wifiKeyboard, 480, 146);
  lv_obj_align(wifiKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(wifiKeyboard, onWifiKeyboardEvent, LV_EVENT_ALL, nullptr);
  lv_obj_add_flag(wifiKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(wifiDialog, LV_OBJ_FLAG_HIDDEN);
}
}

void create(lv_obj_t *parent) {
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Categorias de ajustes");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *description = lv_label_create(parent);
  lv_label_set_text(description, "Selecciona la seccion que quieres configurar");
  lv_obj_set_width(description, 450);
  lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 42);

  appCreateButton(
    parent,
    "APARIENCIA",
    15,
    82,
    215,
    72,
    onOpenCategory,
    reinterpret_cast<void *>(static_cast<intptr_t>(SettingsCategory::APPEARANCE))
  );

  appCreateButton(
    parent,
    "WIFI",
    250,
    82,
    215,
    72,
    onOpenCategory,
    reinterpret_cast<void *>(static_cast<intptr_t>(SettingsCategory::WIFI))
  );

#if APP_ENABLE_OCTOPRINT
  if (AppTheme::showOctoPrintTab()) {
    appCreateButton(
      parent,
      "OCTOPRINT",
      15,
      164,
      215,
      72,
      onOpenCategory,
      reinterpret_cast<void *>(static_cast<intptr_t>(SettingsCategory::OCTOPRINT))
    );
  }
#endif

  appCreateButton(
    parent,
    "SISTEMA",
    250,
    164,
    215,
    72,
    onOpenCategory,
    reinterpret_cast<void *>(static_cast<intptr_t>(SettingsCategory::SYSTEM))
  );
}

void createOverlay() {
  categoryLayer = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(categoryLayer, 0, 0);
  lv_obj_set_size(categoryLayer, 480, 272);
  lv_obj_clear_flag(categoryLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(categoryLayer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(categoryLayer, 0, LV_PART_MAIN);

  categoryTitle = lv_label_create(categoryLayer);
  lv_label_set_text(categoryTitle, "Ajustes");
  lv_obj_set_pos(categoryTitle, 12, 11);

  appCreateButton(categoryLayer, "VOLVER", 390, 3, 80, 32, onCloseCategory);

  createAppearancePage();
  createWifiPage();
  createSystemPage();
  lv_obj_add_flag(categoryLayer, LV_OBJ_FLAG_HIDDEN);

  createWifiDialog();
}

void beginWifi() {
  wifiManager.begin();
  updateNetworkUi();
}

void loop() {
  wifiManager.loop();
  refreshWifiScanOptions();

  WifiManager::State currentState = wifiManager.state();
  if (currentState != previousWifiState) {
    previousWifiState = currentState;
    updateNetworkUi();
    if (wifiDialogStatus != nullptr) {
      lv_label_set_text_fmt(wifiDialogStatus, "Estado: %s", wifiManager.statusText());
    }
  }

  if (wifiManager.consumeConnectionChanged()) {
    bool connected = wifiManager.isConnected();
    if (connected) {
      Serial.print("WiFi conectado. IP: ");
      Serial.println(WiFi.localIP());
      // Sync time via NTP (Spain timezone with automatic DST).
      configTzTime(
        "CET-1CEST,M3.5.0,M10.5.0/3",
        "pool.ntp.org",
        "time.google.com"
      );
    }
#if APP_ENABLE_OCTOPRINT
    OctoPrintFeature::onWifiConnectionChanged(connected);
#endif
    updateNetworkUi();
  }

  if (forceReconnect) {
    forceReconnect = false;
    wifiManager.reconnect();
  }

  if (millis() - lastNetworkUiMs >= 1000) {
    lastNetworkUiMs = millis();
    updateNetworkUi();
  }
}
}