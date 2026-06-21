#include "tab_settings.h"

#include <WiFi.h>

#include "../../core/app_config.h"
#include "../../core/app_ui.h"
#include "services/wifi_manager.h"

#if APP_ENABLE_OCTOPRINT
#include "../octoprint/features/octoprint_feature.h"
#endif

namespace SettingsTab {
namespace {
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
}

void create(lv_obj_t *parent) {
  lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(parent, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_pad_bottom(parent, 30, LV_PART_MAIN);

  constexpr lv_coord_t y = 0;

  lv_obj_t *wifiTitle = lv_label_create(parent);
  lv_label_set_text(wifiTitle, "Conexion WiFi");
  lv_obj_set_pos(wifiTitle, 10, y + 5);

  appCreateButton(parent, "RECONECTAR", 10, y + 32, 150, 40, onReconnectClick);
  appCreateButton(parent, "CONFIGURAR WIFI", 170, y + 32, 190, 40, onOpenWifiDialog);

  wifiStatus = lv_label_create(parent);
  lv_obj_set_width(wifiStatus, 455);
  lv_obj_set_pos(wifiStatus, 10, y + 85);

  ipStatus = lv_label_create(parent);
  lv_obj_set_pos(ipStatus, 10, y + 112);

  wifiSavedCount = lv_label_create(parent);
  lv_obj_set_pos(wifiSavedCount, 10, y + 139);

  lv_obj_t *bottomSpacer = lv_obj_create(parent);
  lv_obj_set_pos(bottomSpacer, 0, y + 175);
  lv_obj_set_size(bottomSpacer, 1, 30);
  lv_obj_set_style_bg_opa(bottomSpacer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(bottomSpacer, 0, LV_PART_MAIN);

  updateNetworkUi();
}

void createOverlay() {
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