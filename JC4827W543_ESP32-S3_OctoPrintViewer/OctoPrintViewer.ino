#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "touch.h"
#include "wifi_manager.h"

// ============================================================
// RED Y PROXY
// ============================================================

const char *SNAPSHOT_BASE_URL =
  "http://192.168.25.60:30113/snapshot-lite.jpg";

// ============================================================
// PARÁMETROS DEL VISOR
// ============================================================

struct ResolutionPreset {
  uint16_t width;
  uint16_t height;
};

const ResolutionPreset RESOLUTIONS[] = {
  {320, 180},
  {368, 207},
  {480, 270},
  {480, 272}
};

constexpr uint8_t RESOLUTION_COUNT =
  sizeof(RESOLUTIONS) / sizeof(RESOLUTIONS[0]);

constexpr uint8_t DEFAULT_RESOLUTION_INDEX = 1;
constexpr uint8_t DEFAULT_JPEG_QUALITY = 75;
constexpr float DEFAULT_ZOOM = 1.0f;
constexpr float DEFAULT_X = 0.50f;
constexpr float DEFAULT_Y = 0.50f;
constexpr uint32_t DEFAULT_INTERVAL_MS = 1000;

static uint8_t resolutionIndex = DEFAULT_RESOLUTION_INDEX;
static uint16_t cameraWidth = 368;
static uint16_t cameraHeight = 207;
static uint8_t jpegQuality = DEFAULT_JPEG_QUALITY;
static float cameraZoom = DEFAULT_ZOOM;
static float cameraX = DEFAULT_X;
static float cameraY = DEFAULT_Y;
static uint32_t snapshotIntervalMs = DEFAULT_INTERVAL_MS;

constexpr float MOVE_STEP = 0.05f;
constexpr float ZOOM_STEP = 0.2f;
constexpr uint16_t MAX_CAMERA_WIDTH = 480;
constexpr uint16_t MAX_CAMERA_HEIGHT = 272;
constexpr size_t CAMERA_BUFFER_BYTES =
  MAX_CAMERA_WIDTH * MAX_CAMERA_HEIGHT * sizeof(uint16_t);
constexpr size_t MAX_JPEG_BYTES = 512 * 1024;

// ============================================================
// PANTALLA JC4827W543
// ============================================================

#define GFX_BL 1

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  45, 47, 21, 48, 40, 39
);

Arduino_GFX *panel = new Arduino_NV3041A(
  bus,
  GFX_NOT_DEFINED,
  0,
  true
);

Arduino_GFX *gfx = new Arduino_Canvas(480, 272, panel);

// ============================================================
// LVGL
// ============================================================

static lv_disp_draw_buf_t drawBuffer;
static lv_disp_drv_t displayDriver;
static lv_color_t *lvglBuffer = nullptr;
static bool displayFlushPending = false;

static lv_obj_t *tabView = nullptr;
static lv_obj_t *counterLabel = nullptr;
static int counterValue = 0;

static lv_obj_t *cameraFrame = nullptr;
static lv_obj_t *cameraImage = nullptr;
static lv_obj_t *cameraStatus = nullptr;

static lv_obj_t *fullscreenLayer = nullptr;
static lv_obj_t *fullscreenImage = nullptr;
static lv_obj_t *fullscreenStatus = nullptr;
static bool fullscreenActive = false;

static lv_obj_t *cameraControls = nullptr;
static lv_obj_t *cameraValuesLabel = nullptr;

static lv_obj_t *wifiStatus = nullptr;
static lv_obj_t *ipStatus = nullptr;
static lv_obj_t *savedStatus = nullptr;

static lv_obj_t *wifiDialog = nullptr;
static lv_obj_t *wifiDropdown = nullptr;
static lv_obj_t *wifiPassword = nullptr;
static lv_obj_t *wifiKeyboard = nullptr;
static lv_obj_t *wifiDialogStatus = nullptr;
static lv_obj_t *wifiSavedCount = nullptr;
static uint32_t displayedWifiScanGeneration = UINT32_MAX;
static WifiManager::State previousWifiState = WifiManager::State::IDLE;

static lv_obj_t *resolutionDropdown = nullptr;
static lv_obj_t *qualitySlider = nullptr;
static lv_obj_t *zoomSlider = nullptr;
static lv_obj_t *xSlider = nullptr;
static lv_obj_t *ySlider = nullptr;
static lv_obj_t *intervalDropdown = nullptr;

static lv_obj_t *qualityValueLabel = nullptr;
static lv_obj_t *zoomValueLabel = nullptr;
static lv_obj_t *xValueLabel = nullptr;
static lv_obj_t *yValueLabel = nullptr;

// ============================================================
// PREFERENCIAS PERSISTENTES
// ============================================================

static Preferences preferences;

// ============================================================
// DOBLE BUFFER Y TAREA DE DESCARGA
// ============================================================

struct SnapshotRequest {
  uint16_t width;
  uint16_t height;
  uint8_t quality;
  float zoom;
  float x;
  float y;
};

static uint16_t *frameBuffers[2] = {nullptr, nullptr};
static uint8_t *jpegBytes = nullptr;
static lv_img_dsc_t cameraDescriptor = {};
static JPEGDEC jpegDecoder;

static int frontBufferIndex = 0;
static int pendingFrameIndex = -1;
static uint16_t pendingFrameWidth = 0;
static uint16_t pendingFrameHeight = 0;
static uint16_t displayedFrameWidth = 368;
static uint16_t displayedFrameHeight = 207;

static uint16_t *decodeTarget = nullptr;
static uint16_t decodeWidth = 0;
static uint16_t decodeHeight = 0;

static SnapshotRequest pendingRequest = {};
static TaskHandle_t snapshotTaskHandle = nullptr;
static portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool downloadBusy = false;
static volatile bool frameReady = false;
static volatile bool workerStatusDirty = false;
static char workerStatus[96] = "";

// ============================================================
// ESTADO GENERAL
// ============================================================

static uint32_t lastSnapshotMs = 0;
static uint32_t lastNetworkUiMs = 0;
static bool forceSnapshot = false;
static bool forceReconnect = false;

// ============================================================
// ENUMS
// ============================================================

enum CameraAction : intptr_t {
  CAMERA_UP = 1,
  CAMERA_DOWN,
  CAMERA_LEFT,
  CAMERA_RIGHT,
  CAMERA_ZOOM_IN,
  CAMERA_ZOOM_OUT,
  CAMERA_RESET,
  CAMERA_CLOSE,
  CAMERA_FULLSCREEN,
  CAMERA_SETTINGS
};

enum ParameterType : intptr_t {
  PARAM_QUALITY = 1,
  PARAM_ZOOM,
  PARAM_X,
  PARAM_Y
};

// ============================================================
// UTILIDADES
// ============================================================

float clampFloat(float value, float minimum, float maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

uint32_t normalizeInterval(uint32_t value) {
  if (value == 500 || value == 1000 || value == 2000 || value == 5000) {
    return value;
  }
  return DEFAULT_INTERVAL_MS;
}

uint8_t intervalToDropdownIndex(uint32_t value) {
  switch (value) {
    case 500: return 0;
    case 1000: return 1;
    case 2000: return 2;
    case 5000: return 3;
    default: return 1;
  }
}

void serviceUi() {
  lv_timer_handler();
  if (displayFlushPending) {
    displayFlushPending = false;
    gfx->flush();
  }
}

void setCameraStatus(const char *text) {
  if (cameraStatus != nullptr) {
    lv_label_set_text(cameraStatus, text);
  }
  if (fullscreenStatus != nullptr) {
    lv_label_set_text(fullscreenStatus, text);
  }
}

void setWorkerStatus(const char *text) {
  portENTER_CRITICAL(&stateMux);
  snprintf(workerStatus, sizeof(workerStatus), "%s", text);
  workerStatusDirty = true;
  portEXIT_CRITICAL(&stateMux);
}

void applyWorkerStatus() {
  char localStatus[96];
  bool hasStatus = false;

  portENTER_CRITICAL(&stateMux);
  if (workerStatusDirty) {
    snprintf(localStatus, sizeof(localStatus), "%s", workerStatus);
    workerStatusDirty = false;
    hasStatus = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (hasStatus) {
    setCameraStatus(localStatus);
  }
}

void setDefaultSettings() {
  resolutionIndex = DEFAULT_RESOLUTION_INDEX;
  cameraWidth = RESOLUTIONS[resolutionIndex].width;
  cameraHeight = RESOLUTIONS[resolutionIndex].height;
  jpegQuality = DEFAULT_JPEG_QUALITY;
  cameraZoom = DEFAULT_ZOOM;
  cameraX = DEFAULT_X;
  cameraY = DEFAULT_Y;
  snapshotIntervalMs = DEFAULT_INTERVAL_MS;
}

void loadSettings() {
  if (!preferences.begin("octoview", true)) {
    Serial.println("No se pudo abrir NVS; se usaran valores por defecto");
    setDefaultSettings();
    return;
  }

  resolutionIndex = preferences.getUChar(
    "resolution",
    DEFAULT_RESOLUTION_INDEX
  );
  jpegQuality = preferences.getUChar("quality", DEFAULT_JPEG_QUALITY);
  cameraZoom = preferences.getFloat("zoom", DEFAULT_ZOOM);
  cameraX = preferences.getFloat("x", DEFAULT_X);
  cameraY = preferences.getFloat("y", DEFAULT_Y);
  snapshotIntervalMs = preferences.getUInt("interval", DEFAULT_INTERVAL_MS);

  preferences.end();

  if (resolutionIndex >= RESOLUTION_COUNT) {
    resolutionIndex = DEFAULT_RESOLUTION_INDEX;
  }

  jpegQuality = constrain(jpegQuality, 30, 95);
  cameraZoom = isfinite(cameraZoom)
    ? clampFloat(cameraZoom, 1.0f, 5.0f)
    : DEFAULT_ZOOM;
  cameraX = isfinite(cameraX)
    ? clampFloat(cameraX, 0.0f, 1.0f)
    : DEFAULT_X;
  cameraY = isfinite(cameraY)
    ? clampFloat(cameraY, 0.0f, 1.0f)
    : DEFAULT_Y;
  snapshotIntervalMs = normalizeInterval(snapshotIntervalMs);

  cameraWidth = RESOLUTIONS[resolutionIndex].width;
  cameraHeight = RESOLUTIONS[resolutionIndex].height;
}

bool saveSettings() {
  if (!preferences.begin("octoview", false)) {
    return false;
  }

  bool ok = true;
  ok &= preferences.putUChar("resolution", resolutionIndex) > 0;
  ok &= preferences.putUChar("quality", jpegQuality) > 0;
  ok &= preferences.putFloat("zoom", cameraZoom) > 0;
  ok &= preferences.putFloat("x", cameraX) > 0;
  ok &= preferences.putFloat("y", cameraY) > 0;
  ok &= preferences.putUInt("interval", snapshotIntervalMs) > 0;

  preferences.end();
  return ok;
}

void updateCameraValuesLabel() {
  if (cameraValuesLabel == nullptr) return;

  char text[96];
  snprintf(
    text,
    sizeof(text),
    "%ux%u  Q%d  Z %.1fx  X %.2f  Y %.2f",
    cameraWidth,
    cameraHeight,
    jpegQuality,
    cameraZoom,
    cameraX,
    cameraY
  );
  lv_label_set_text(cameraValuesLabel, text);
}

void updateParameterLabels() {
  if (qualityValueLabel != nullptr) {
    lv_label_set_text_fmt(qualityValueLabel, "%d", jpegQuality);
  }
  if (zoomValueLabel != nullptr) {
    lv_label_set_text_fmt(zoomValueLabel, "%.1fx", cameraZoom);
  }
  if (xValueLabel != nullptr) {
    lv_label_set_text_fmt(xValueLabel, "%.2f", cameraX);
  }
  if (yValueLabel != nullptr) {
    lv_label_set_text_fmt(yValueLabel, "%.2f", cameraY);
  }
  updateCameraValuesLabel();
}

void syncParameterControls() {
  if (resolutionDropdown != nullptr) {
    lv_dropdown_set_selected(resolutionDropdown, resolutionIndex);
  }
  if (intervalDropdown != nullptr) {
    lv_dropdown_set_selected(
      intervalDropdown,
      intervalToDropdownIndex(snapshotIntervalMs)
    );
  }
  if (qualitySlider != nullptr) {
    lv_slider_set_value(qualitySlider, jpegQuality, LV_ANIM_OFF);
  }
  if (zoomSlider != nullptr) {
    lv_slider_set_value(
      zoomSlider,
      static_cast<int>(cameraZoom * 10.0f),
      LV_ANIM_OFF
    );
  }
  if (xSlider != nullptr) {
    lv_slider_set_value(
      xSlider,
      static_cast<int>(cameraX * 100.0f),
      LV_ANIM_OFF
    );
  }
  if (ySlider != nullptr) {
    lv_slider_set_value(
      ySlider,
      static_cast<int>(cameraY * 100.0f),
      LV_ANIM_OFF
    );
  }
  updateParameterLabels();
}

void markSettingsDirty() {
  if (savedStatus != nullptr) {
    lv_label_set_text(savedStatus, "Cambios sin guardar");
  }
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

uint16_t calculateImageZoom(
  uint16_t sourceWidth,
  uint16_t sourceHeight,
  uint16_t maxWidth,
  uint16_t maxHeight
) {
  uint32_t zoomX =
    (static_cast<uint32_t>(maxWidth) * 256) / sourceWidth;
  uint32_t zoomY =
    (static_cast<uint32_t>(maxHeight) * 256) / sourceHeight;

  uint32_t zoom = min(zoomX, zoomY);
  if (zoom > 768) zoom = 768;
  if (zoom < 1) zoom = 1;
  return static_cast<uint16_t>(zoom);
}

void updateImageLayout() {
  if (displayedFrameWidth == 0 || displayedFrameHeight == 0) return;

  if (cameraImage != nullptr) {
    lv_img_set_zoom(
      cameraImage,
      calculateImageZoom(
        displayedFrameWidth,
        displayedFrameHeight,
        400,
        198
      )
    );
    lv_obj_center(cameraImage);
  }

  if (fullscreenImage != nullptr) {
    lv_img_set_zoom(
      fullscreenImage,
      calculateImageZoom(
        displayedFrameWidth,
        displayedFrameHeight,
        480,
        272
      )
    );
    lv_obj_center(fullscreenImage);
  }
}

void hideCameraControls() {
  if (cameraControls != nullptr) {
    lv_obj_add_flag(cameraControls, LV_OBJ_FLAG_HIDDEN);
  }
}

void showCameraControls() {
  if (cameraControls == nullptr) return;
  updateCameraValuesLabel();
  lv_obj_clear_flag(cameraControls, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cameraControls);
}

void toggleCameraControls() {
  if (cameraControls == nullptr) return;

  if (lv_obj_has_flag(cameraControls, LV_OBJ_FLAG_HIDDEN)) {
    showCameraControls();
  } else {
    hideCameraControls();
  }
}

void enterFullscreen() {
  if (fullscreenLayer == nullptr) return;
  hideCameraControls();
  fullscreenActive = true;
  lv_obj_clear_flag(fullscreenLayer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(fullscreenLayer);
  updateImageLayout();
}

void exitFullscreen() {
  if (fullscreenLayer == nullptr) return;
  fullscreenActive = false;
  lv_obj_add_flag(fullscreenLayer, LV_OBJ_FLAG_HIDDEN);
  hideCameraControls();
}

// ============================================================
// WIFI NO BLOQUEANTE
// ============================================================

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

  if (options.isEmpty()) {
    options = "No se encontraron redes";
  }

  lv_dropdown_set_options(wifiDropdown, options.c_str());
  lv_dropdown_set_selected(wifiDropdown, 0);
}

void serviceWifiConnection() {
  wifiManager.loop();
  refreshWifiScanOptions();

  WifiManager::State currentState = wifiManager.state();
  if (currentState != previousWifiState) {
    previousWifiState = currentState;
    updateNetworkUi();

    if (wifiDialogStatus != nullptr) {
      lv_label_set_text_fmt(
        wifiDialogStatus,
        "Estado: %s",
        wifiManager.statusText()
      );
    }
  }

  if (wifiManager.consumeConnectionChanged()) {
    if (wifiManager.isConnected()) {
      Serial.print("WiFi conectado. IP: ");
      Serial.println(WiFi.localIP());
      setCameraStatus("WiFi conectado. Cargando camara...");
      forceSnapshot = true;
    } else {
      setCameraStatus("WiFi desconectado");
    }
    updateNetworkUi();
  }
}

// ============================================================
// EVENTOS LVGL
// ============================================================

void onCounterClick(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  counterValue++;
  lv_label_set_text_fmt(counterLabel, "%d", counterValue);
}

void onCameraAreaTap(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  toggleCameraControls();
}

void onFullscreenExit(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  exitFullscreen();
}

void onRefreshClick(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_tabview_set_act(tabView, 1, LV_ANIM_ON);
  forceSnapshot = true;
}

void onReconnectClick(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  forceReconnect = true;
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

  if (wifiManager.startScan()) {
    lv_label_set_text(wifiDialogStatus, "Buscando redes...");
  } else {
    lv_label_set_text(wifiDialogStatus, "El escaneo ya esta en curso");
  }
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
  bool accepted;

  if (wifiManager.isSaved(network.ssid) && password.isEmpty()) {
    accepted = wifiManager.connectSaved(network.ssid);
  } else {
    accepted = wifiManager.saveAndConnect(network.ssid, password);
  }

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

void onSaveSettings(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  if (saveSettings()) {
    lv_label_set_text(savedStatus, "Valores guardados");
  } else {
    lv_label_set_text(savedStatus, "Error al guardar");
  }
}

void onTabChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  hideCameraControls();

  if (lv_tabview_get_tab_act(tabView) == 1) {
    forceSnapshot = true;
  }
}

void onCameraControl(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  CameraAction action = static_cast<CameraAction>(
    reinterpret_cast<intptr_t>(lv_event_get_user_data(event))
  );

  switch (action) {
    case CAMERA_UP: cameraY -= MOVE_STEP; break;
    case CAMERA_DOWN: cameraY += MOVE_STEP; break;
    case CAMERA_LEFT: cameraX -= MOVE_STEP; break;
    case CAMERA_RIGHT: cameraX += MOVE_STEP; break;
    case CAMERA_ZOOM_IN: cameraZoom += ZOOM_STEP; break;
    case CAMERA_ZOOM_OUT: cameraZoom -= ZOOM_STEP; break;

    case CAMERA_RESET:
      cameraZoom = DEFAULT_ZOOM;
      cameraX = DEFAULT_X;
      cameraY = DEFAULT_Y;
      break;

    case CAMERA_CLOSE:
      hideCameraControls();
      return;

    case CAMERA_FULLSCREEN:
      enterFullscreen();
      return;

    case CAMERA_SETTINGS:
      exitFullscreen();
      lv_tabview_set_act(tabView, 2, LV_ANIM_ON);
      return;
  }

  cameraZoom = clampFloat(cameraZoom, 1.0f, 5.0f);
  cameraX = clampFloat(cameraX, 0.0f, 1.0f);
  cameraY = clampFloat(cameraY, 0.0f, 1.0f);

  syncParameterControls();
  markSettingsDirty();
  setCameraStatus("Actualizando encuadre...");
  forceSnapshot = true;
}

void onResolutionChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

  resolutionIndex = lv_dropdown_get_selected(resolutionDropdown);
  if (resolutionIndex >= RESOLUTION_COUNT) {
    resolutionIndex = DEFAULT_RESOLUTION_INDEX;
  }

  cameraWidth = RESOLUTIONS[resolutionIndex].width;
  cameraHeight = RESOLUTIONS[resolutionIndex].height;
  updateCameraValuesLabel();
  markSettingsDirty();
  setCameraStatus("Cambiando resolucion...");
  forceSnapshot = true;
}

void onIntervalChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

  switch (lv_dropdown_get_selected(intervalDropdown)) {
    case 0: snapshotIntervalMs = 500; break;
    case 1: snapshotIntervalMs = 1000; break;
    case 2: snapshotIntervalMs = 2000; break;
    case 3: snapshotIntervalMs = 5000; break;
    default: snapshotIntervalMs = DEFAULT_INTERVAL_MS; break;
  }

  markSettingsDirty();
  forceSnapshot = true;
}

void onParameterSlider(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  ParameterType type = static_cast<ParameterType>(
    reinterpret_cast<intptr_t>(lv_event_get_user_data(event))
  );

  if (code == LV_EVENT_VALUE_CHANGED) {
    switch (type) {
      case PARAM_QUALITY:
        jpegQuality = lv_slider_get_value(qualitySlider);
        break;
      case PARAM_ZOOM:
        cameraZoom = lv_slider_get_value(zoomSlider) / 10.0f;
        break;
      case PARAM_X:
        cameraX = lv_slider_get_value(xSlider) / 100.0f;
        break;
      case PARAM_Y:
        cameraY = lv_slider_get_value(ySlider) / 100.0f;
        break;
    }
    updateParameterLabels();
    markSettingsDirty();
  }

  if (code == LV_EVENT_RELEASED) {
    setCameraStatus("Aplicando parametros...");
    forceSnapshot = true;
  }
}

void onResetParameters(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  setDefaultSettings();
  syncParameterControls();
  if (saveSettings()) {
    lv_label_set_text(savedStatus, "Valores por defecto guardados");
  } else {
    lv_label_set_text(savedStatus, "Error al guardar valores por defecto");
  }
  setCameraStatus("Restaurando parametros...");
  forceSnapshot = true;
}

// ============================================================
// CREACIÓN DE BOTONES
// ============================================================

lv_obj_t *createButton(
  lv_obj_t *parent,
  const char *text,
  lv_coord_t x,
  lv_coord_t y,
  lv_coord_t width,
  lv_coord_t height,
  lv_event_cb_t callback,
  void *userData = nullptr
) {
  lv_obj_t *button = lv_btn_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, userData);

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

lv_obj_t *createCameraActionButton(
  lv_obj_t *parent,
  const char *text,
  lv_coord_t x,
  lv_coord_t y,
  lv_coord_t width,
  lv_coord_t height,
  CameraAction action
) {
  return createButton(
    parent,
    text,
    x,
    y,
    width,
    height,
    onCameraControl,
    reinterpret_cast<void *>(static_cast<intptr_t>(action))
  );
}

// ============================================================
// PANEL FLOTANTE
// ============================================================

void createCameraControls() {
  cameraControls = lv_obj_create(lv_layer_top());
  lv_obj_set_size(cameraControls, 340, 238);
  lv_obj_center(cameraControls);
  lv_obj_clear_flag(cameraControls, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(cameraControls, lv_color_hex(0x101010), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cameraControls, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_border_width(cameraControls, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(cameraControls, lv_color_hex(0x25B9D7), LV_PART_MAIN);
  lv_obj_set_style_radius(cameraControls, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_all(cameraControls, 0, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(cameraControls);
  lv_label_set_text(title, "Control de encuadre");
  lv_obj_set_pos(title, 10, 8);

  cameraValuesLabel = lv_label_create(cameraControls);
  lv_obj_set_width(cameraValuesLabel, 318);
  lv_obj_set_style_text_align(cameraValuesLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_pos(cameraValuesLabel, 10, 35);

  createCameraActionButton(cameraControls, "X", 298, 5, 34, 30, CAMERA_CLOSE);
  createCameraActionButton(cameraControls, "^", 145, 65, 50, 36, CAMERA_UP);
  createCameraActionButton(cameraControls, "<", 87, 105, 50, 36, CAMERA_LEFT);
  createCameraActionButton(cameraControls, "RESET", 143, 105, 54, 36, CAMERA_RESET);
  createCameraActionButton(cameraControls, ">", 203, 105, 50, 36, CAMERA_RIGHT);
  createCameraActionButton(cameraControls, "v", 145, 145, 50, 36, CAMERA_DOWN);
  createCameraActionButton(cameraControls, "ZOOM -", 10, 145, 100, 36, CAMERA_ZOOM_OUT);
  createCameraActionButton(cameraControls, "ZOOM +", 230, 145, 100, 36, CAMERA_ZOOM_IN);
  createCameraActionButton(cameraControls, "FULLSCREEN", 10, 192, 150, 38, CAMERA_FULLSCREEN);
  createCameraActionButton(cameraControls, "PARAMETROS", 180, 192, 150, 38, CAMERA_SETTINGS);

  updateCameraValuesLabel();
  lv_obj_add_flag(cameraControls, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
// PANTALLA COMPLETA
// ============================================================

void createFullscreenLayer() {
  fullscreenLayer = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(fullscreenLayer, 0, 0);
  lv_obj_set_size(fullscreenLayer, 480, 272);
  lv_obj_clear_flag(fullscreenLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(fullscreenLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(fullscreenLayer, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(fullscreenLayer, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(fullscreenLayer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(fullscreenLayer, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(fullscreenLayer, onCameraAreaTap, LV_EVENT_CLICKED, nullptr);

  fullscreenImage = lv_img_create(fullscreenLayer);
  lv_obj_center(fullscreenImage);

  fullscreenStatus = lv_label_create(fullscreenLayer);
  lv_obj_set_width(fullscreenStatus, 420);
  lv_obj_set_style_text_align(fullscreenStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(fullscreenStatus, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(fullscreenStatus, LV_OPA_50, LV_PART_MAIN);
  lv_obj_align(fullscreenStatus, LV_ALIGN_BOTTOM_MID, 0, -4);

  lv_obj_t *exitButton = createButton(
    fullscreenLayer,
    "SALIR",
    414,
    5,
    60,
    34,
    onFullscreenExit
  );
  lv_obj_move_foreground(exitButton);
  lv_obj_add_flag(fullscreenLayer, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
// CONFIGURACION WIFI
// ============================================================

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

  createButton(wifiDialog, "CERRAR", 405, 3, 68, 31, onCloseWifiDialog);

  wifiDropdown = lv_dropdown_create(wifiDialog);
  lv_dropdown_set_options(wifiDropdown, "Buscando redes...");
  lv_obj_set_pos(wifiDropdown, 10, 38);
  lv_obj_set_size(wifiDropdown, 305, 40);

  createButton(wifiDialog, "BUSCAR", 325, 38, 145, 40, onScanWifiClick);

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

  createButton(wifiDialog, "GUARDAR Y CONECTAR", 10, 148, 220, 34, onConnectWifiClick);
  createButton(wifiDialog, "OLVIDAR", 240, 148, 105, 34, onForgetWifiClick);

  wifiKeyboard = lv_keyboard_create(wifiDialog);
  lv_keyboard_set_mode(wifiKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(wifiKeyboard, wifiPassword);
  lv_obj_set_size(wifiKeyboard, 480, 146);
  lv_obj_align(wifiKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(wifiKeyboard, onWifiKeyboardEvent, LV_EVENT_ALL, nullptr);
  lv_obj_add_flag(wifiKeyboard, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(wifiDialog, LV_OBJ_FLAG_HIDDEN);
}
// ============================================================
// TAB DE AJUSTES CON SCROLL VERTICAL
// ============================================================

void createParametersTab(lv_obj_t *settingsTab) {
  lv_obj_add_flag(settingsTab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(settingsTab, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(settingsTab, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_pad_bottom(settingsTab, 30, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(settingsTab);
  lv_label_set_text(title, "Parametros del snapshot");
  lv_obj_set_pos(title, 130, 5);

  lv_obj_t *resolutionLabel = lv_label_create(settingsTab);
  lv_label_set_text(resolutionLabel, "Resolucion");
  lv_obj_set_pos(resolutionLabel, 10, 42);

  resolutionDropdown = lv_dropdown_create(settingsTab);
  lv_dropdown_set_options(
    resolutionDropdown,
    "320 x 180\n368 x 207\n480 x 270\n480 x 272"
  );
  lv_obj_set_pos(resolutionDropdown, 100, 32);
  lv_obj_set_size(resolutionDropdown, 125, 40);
  lv_obj_add_event_cb(
    resolutionDropdown,
    onResolutionChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

  lv_obj_t *intervalLabel = lv_label_create(settingsTab);
  lv_label_set_text(intervalLabel, "Refresco");
  lv_obj_set_pos(intervalLabel, 245, 42);

  intervalDropdown = lv_dropdown_create(settingsTab);
  lv_dropdown_set_options(intervalDropdown, "0.5 s\n1 s\n2 s\n5 s");
  lv_obj_set_pos(intervalDropdown, 320, 32);
  lv_obj_set_size(intervalDropdown, 120, 40);
  lv_obj_add_event_cb(
    intervalDropdown,
    onIntervalChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

  lv_obj_t *qualityLabel = lv_label_create(settingsTab);
  lv_label_set_text(qualityLabel, "Calidad");
  lv_obj_set_pos(qualityLabel, 10, 92);

  qualityValueLabel = lv_label_create(settingsTab);
  lv_obj_set_pos(qualityValueLabel, 75, 92);

  qualitySlider = lv_slider_create(settingsTab);
  lv_obj_set_pos(qualitySlider, 125, 93);
  lv_obj_set_size(qualitySlider, 315, 18);
  lv_slider_set_range(qualitySlider, 30, 95);
  lv_obj_add_event_cb(
    qualitySlider,
    onParameterSlider,
    LV_EVENT_ALL,
    reinterpret_cast<void *>(static_cast<intptr_t>(PARAM_QUALITY))
  );

  lv_obj_t *zoomLabel = lv_label_create(settingsTab);
  lv_label_set_text(zoomLabel, "Zoom");
  lv_obj_set_pos(zoomLabel, 10, 137);

  zoomValueLabel = lv_label_create(settingsTab);
  lv_obj_set_pos(zoomValueLabel, 75, 137);

  zoomSlider = lv_slider_create(settingsTab);
  lv_obj_set_pos(zoomSlider, 125, 138);
  lv_obj_set_size(zoomSlider, 315, 18);
  lv_slider_set_range(zoomSlider, 10, 50);
  lv_obj_add_event_cb(
    zoomSlider,
    onParameterSlider,
    LV_EVENT_ALL,
    reinterpret_cast<void *>(static_cast<intptr_t>(PARAM_ZOOM))
  );

  lv_obj_t *xLabel = lv_label_create(settingsTab);
  lv_label_set_text(xLabel, "X");
  lv_obj_set_pos(xLabel, 10, 182);

  xValueLabel = lv_label_create(settingsTab);
  lv_obj_set_pos(xValueLabel, 75, 182);

  xSlider = lv_slider_create(settingsTab);
  lv_obj_set_pos(xSlider, 125, 183);
  lv_obj_set_size(xSlider, 315, 18);
  lv_slider_set_range(xSlider, 0, 100);
  lv_obj_add_event_cb(
    xSlider,
    onParameterSlider,
    LV_EVENT_ALL,
    reinterpret_cast<void *>(static_cast<intptr_t>(PARAM_X))
  );

  lv_obj_t *yLabel = lv_label_create(settingsTab);
  lv_label_set_text(yLabel, "Y");
  lv_obj_set_pos(yLabel, 10, 227);

  yValueLabel = lv_label_create(settingsTab);
  lv_obj_set_pos(yValueLabel, 75, 227);

  ySlider = lv_slider_create(settingsTab);
  lv_obj_set_pos(ySlider, 125, 228);
  lv_obj_set_size(ySlider, 315, 18);
  lv_slider_set_range(ySlider, 0, 100);
  lv_obj_add_event_cb(
    ySlider,
    onParameterSlider,
    LV_EVENT_ALL,
    reinterpret_cast<void *>(static_cast<intptr_t>(PARAM_Y))
  );

  createButton(settingsTab, "VER CAMARA", 10, 275, 135, 40, onRefreshClick);
  createButton(settingsTab, "GUARDAR", 155, 275, 115, 40, onSaveSettings);
  createButton(settingsTab, "RESET", 280, 275, 80, 40, onResetParameters);
  createButton(settingsTab, "RECONECTAR", 10, 327, 150, 40, onReconnectClick);
  createButton(settingsTab, "CONFIGURAR WIFI", 170, 327, 190, 40, onOpenWifiDialog);

  savedStatus = lv_label_create(settingsTab);
  lv_label_set_text(savedStatus, "Cambios sin guardar");
  lv_obj_set_pos(savedStatus, 10, 378);

  wifiStatus = lv_label_create(settingsTab);
  lv_obj_set_width(wifiStatus, 455);
  lv_obj_set_pos(wifiStatus, 10, 410);

  ipStatus = lv_label_create(settingsTab);
  lv_obj_set_pos(ipStatus, 10, 437);

  wifiSavedCount = lv_label_create(settingsTab);
  lv_obj_set_pos(wifiSavedCount, 10, 464);

  lv_obj_t *bottomSpacer = lv_obj_create(settingsTab);
  lv_obj_set_pos(bottomSpacer, 0, 500);
  lv_obj_set_size(bottomSpacer, 1, 30);
  lv_obj_set_style_bg_opa(bottomSpacer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(bottomSpacer, 0, LV_PART_MAIN);

  syncParameterControls();
  updateNetworkUi();
}

// ============================================================
// INTERFAZ PRINCIPAL
// ============================================================

void createInterface() {
  lv_obj_clean(lv_scr_act());

  tabView = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 36);
  lv_obj_set_size(tabView, 480, 272);
  lv_obj_center(tabView);
  lv_obj_add_event_cb(tabView, onTabChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *counterTab = lv_tabview_add_tab(tabView, "Contador");
  lv_obj_t *cameraTab = lv_tabview_add_tab(tabView, "OctoPrint");
  lv_obj_t *settingsTab = lv_tabview_add_tab(tabView, "Ajustes");

  lv_obj_clear_flag(counterTab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(cameraTab, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *counterTitle = lv_label_create(counterTab);
  lv_label_set_text(counterTitle, "Mi primer contador");
  lv_obj_align(counterTitle, LV_ALIGN_TOP_MID, 0, 20);

  counterLabel = lv_label_create(counterTab);
  lv_label_set_text(counterLabel, "0");
  lv_obj_align(counterLabel, LV_ALIGN_CENTER, 0, -25);

  createButton(counterTab, "SUMAR", 135, 125, 180, 65, onCounterClick);

  lv_obj_set_style_pad_all(cameraTab, 0, LV_PART_MAIN);

  cameraFrame = lv_obj_create(cameraTab);
  lv_obj_set_size(cameraFrame, 400, 198);
  lv_obj_align(cameraFrame, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(cameraFrame, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cameraFrame, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(cameraFrame, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cameraFrame, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(cameraFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(cameraFrame, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(cameraFrame, onCameraAreaTap, LV_EVENT_CLICKED, nullptr);

  cameraImage = lv_img_create(cameraFrame);
  lv_obj_center(cameraImage);

  cameraStatus = lv_label_create(cameraTab);
  lv_label_set_text(cameraStatus, "Esperando conexion...");
  lv_obj_set_width(cameraStatus, 470);
  lv_obj_set_style_text_align(cameraStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(cameraStatus, LV_ALIGN_BOTTOM_MID, 0, -2);

  createParametersTab(settingsTab);
  createFullscreenLayer();
  createCameraControls();
  createWifiDialog();

  lv_tabview_set_act(tabView, 1, LV_ANIM_OFF);
}

// ============================================================
// DRIVERS LVGL
// ============================================================

void myDisplayFlush(
  lv_disp_drv_t *display,
  const lv_area_t *area,
  lv_color_t *colors
) {
  uint32_t width = area->x2 - area->x1 + 1;
  uint32_t height = area->y2 - area->y1 + 1;

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(
    area->x1,
    area->y1,
    reinterpret_cast<uint16_t *>(&colors->full),
    width,
    height
  );
#else
  gfx->draw16bitRGBBitmap(
    area->x1,
    area->y1,
    reinterpret_cast<uint16_t *>(&colors->full),
    width,
    height
  );
#endif

  displayFlushPending = true;
  lv_disp_flush_ready(display);
}

void myTouchpadRead(lv_indev_drv_t *, lv_indev_data_t *data) {
  if (touch_has_signal() && touch_touched()) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch_last_x;
    data->point.y = touch_last_y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ============================================================
// MEMORIA, JPEG Y DOBLE BUFFER
// ============================================================

bool allocateCameraMemory() {
  frameBuffers[0] = static_cast<uint16_t *>(
    heap_caps_malloc(
      CAMERA_BUFFER_BYTES,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    )
  );

  frameBuffers[1] = static_cast<uint16_t *>(
    heap_caps_malloc(
      CAMERA_BUFFER_BYTES,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    )
  );

  jpegBytes = static_cast<uint8_t *>(
    heap_caps_malloc(
      MAX_JPEG_BYTES,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    )
  );

  if (frameBuffers[0] == nullptr || frameBuffers[1] == nullptr || jpegBytes == nullptr) {
    Serial.println("No se pudo reservar PSRAM");
    return false;
  }

  memset(frameBuffers[0], 0, CAMERA_BUFFER_BYTES);
  memset(frameBuffers[1], 0, CAMERA_BUFFER_BYTES);

  cameraDescriptor.header.always_zero = 0;
  cameraDescriptor.header.w = displayedFrameWidth;
  cameraDescriptor.header.h = displayedFrameHeight;
  cameraDescriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
  cameraDescriptor.data_size =
    displayedFrameWidth * displayedFrameHeight * sizeof(uint16_t);
  cameraDescriptor.data = reinterpret_cast<const uint8_t *>(
    frameBuffers[frontBufferIndex]
  );

  return true;
}

int drawJpegBlock(JPEGDRAW *draw) {
  if (decodeTarget == nullptr) return 0;
  if (draw->x >= decodeWidth || draw->y >= decodeHeight) return 1;

  int sourceWidth = draw->iWidth;
  int usableWidth = draw->iWidthUsed > 0 ? draw->iWidthUsed : draw->iWidth;
  int copyWidth = min(
    usableWidth,
    static_cast<int>(decodeWidth) - draw->x
  );
  int copyHeight = min(
    draw->iHeight,
    static_cast<int>(decodeHeight) - draw->y
  );

  for (int row = 0; row < copyHeight; row++) {
    uint16_t *destination =
      decodeTarget + ((draw->y + row) * decodeWidth) + draw->x;
    uint16_t *source = draw->pPixels + (row * sourceWidth);
    memcpy(destination, source, copyWidth * sizeof(uint16_t));
  }

  return 1;
}

bool queueSnapshotRequest() {
  if (snapshotTaskHandle == nullptr || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  bool queued = false;

  portENTER_CRITICAL(&stateMux);
  if (!downloadBusy && !frameReady) {
    pendingRequest.width = cameraWidth;
    pendingRequest.height = cameraHeight;
    pendingRequest.quality = jpegQuality;
    pendingRequest.zoom = cameraZoom;
    pendingRequest.x = cameraX;
    pendingRequest.y = cameraY;
    downloadBusy = true;
    queued = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (queued) {
    setCameraStatus("Descargando captura...");
    xTaskNotifyGive(snapshotTaskHandle);
  }

  return queued;
}

bool isValidSnapshotRequest(const SnapshotRequest &request) {
  return request.width > 0 &&
    request.width <= MAX_CAMERA_WIDTH &&
    request.height > 0 &&
    request.height <= MAX_CAMERA_HEIGHT &&
    request.quality >= 30 &&
    request.quality <= 95 &&
    isfinite(request.zoom) &&
    request.zoom >= 1.0f &&
    request.zoom <= 5.0f &&
    isfinite(request.x) &&
    request.x >= 0.0f &&
    request.x <= 1.0f &&
    isfinite(request.y) &&
    request.y >= 0.0f &&
    request.y <= 1.0f;
}

void applyPendingFrame() {
  int newFrontIndex = -1;
  uint16_t newWidth = 0;
  uint16_t newHeight = 0;

  portENTER_CRITICAL(&stateMux);
  if (frameReady) {
    newFrontIndex = pendingFrameIndex;
    newWidth = pendingFrameWidth;
    newHeight = pendingFrameHeight;
  }
  portEXIT_CRITICAL(&stateMux);

  if (
    newFrontIndex < 0 ||
    newFrontIndex > 1 ||
    newWidth == 0 ||
    newWidth > MAX_CAMERA_WIDTH ||
    newHeight == 0 ||
    newHeight > MAX_CAMERA_HEIGHT
  ) {
    if (newFrontIndex >= 0) {
      portENTER_CRITICAL(&stateMux);
      pendingFrameIndex = -1;
      frameReady = false;
      portEXIT_CRITICAL(&stateMux);
      setCameraStatus("Frame descartado por dimensiones invalidas");
    }
    return;
  }

  lv_img_cache_invalidate_src(&cameraDescriptor);

  displayedFrameWidth = newWidth;
  displayedFrameHeight = newHeight;
  cameraDescriptor.header.w = newWidth;
  cameraDescriptor.header.h = newHeight;
  cameraDescriptor.data_size = newWidth * newHeight * sizeof(uint16_t);
  cameraDescriptor.data = reinterpret_cast<const uint8_t *>(
    frameBuffers[newFrontIndex]
  );

  lv_img_set_src(cameraImage, &cameraDescriptor);
  lv_img_set_src(fullscreenImage, &cameraDescriptor);
  updateImageLayout();
  lv_obj_invalidate(cameraImage);
  lv_obj_invalidate(fullscreenImage);

  portENTER_CRITICAL(&stateMux);
  frontBufferIndex = newFrontIndex;
  pendingFrameIndex = -1;
  frameReady = false;
  portEXIT_CRITICAL(&stateMux);
}

bool downloadAndDecodeSnapshot(
  const SnapshotRequest &request,
  int targetBufferIndex
) {
  if (
    targetBufferIndex < 0 ||
    targetBufferIndex > 1 ||
    frameBuffers[targetBufferIndex] == nullptr ||
    !isValidSnapshotRequest(request)
  ) {
    setWorkerStatus("Peticion de captura invalida");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    setWorkerStatus("WiFi desconectado");
    return false;
  }

  char url[320];
  snprintf(
    url,
    sizeof(url),
    "%s?w=%u&h=%u&q=%u&zoom=%.1f&x=%.2f&y=%.2f&t=%lu",
    SNAPSHOT_BASE_URL,
    request.width,
    request.height,
    request.quality,
    request.zoom,
    request.x,
    request.y,
    static_cast<unsigned long>(millis())
  );

  Serial.println(url);

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);

  if (!http.begin(client, url)) {
    setWorkerStatus("No se pudo abrir la URL");
    return false;
  }

  http.useHTTP10(true);
  http.addHeader("Cache-Control", "no-cache");

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Error HTTP: %d\n", httpCode);
    http.end();
    setWorkerStatus("Error HTTP descargando captura");
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0 || static_cast<size_t>(contentLength) > MAX_JPEG_BYTES) {
    Serial.printf("Tamano JPEG invalido: %d\n", contentLength);
    http.end();
    setWorkerStatus("JPEG demasiado grande o sin tamano");
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t totalRead = 0;
  uint32_t lastDataAt = millis();

  while (totalRead < static_cast<size_t>(contentLength)) {
    int availableBytes = stream->available();

    if (availableBytes > 0) {
      size_t remaining = static_cast<size_t>(contentLength) - totalRead;
      size_t bytesToRead = min(
        static_cast<size_t>(availableBytes),
        remaining
      );

      int bytesRead = stream->read(jpegBytes + totalRead, bytesToRead);
      if (bytesRead > 0) {
        totalRead += bytesRead;
        lastDataAt = millis();
      }
    } else {
      if (millis() - lastDataAt > 5000) break;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  http.end();

  if (totalRead != static_cast<size_t>(contentLength)) {
    Serial.printf(
      "JPEG incompleto: %u/%d bytes\n",
      static_cast<unsigned>(totalRead),
      contentLength
    );
    setWorkerStatus("Captura JPEG incompleta");
    return false;
  }

  decodeTarget = frameBuffers[targetBufferIndex];
  decodeWidth = request.width;
  decodeHeight = request.height;
  memset(decodeTarget, 0, CAMERA_BUFFER_BYTES);

  if (!jpegDecoder.openRAM(
        jpegBytes,
        static_cast<int>(totalRead),
        drawJpegBlock
      )) {
    Serial.printf("Error abriendo JPEG: %d\n", jpegDecoder.getLastError());
    decodeTarget = nullptr;
    decodeWidth = 0;
    decodeHeight = 0;
    setWorkerStatus("La captura no es un JPEG valido");
    return false;
  }

  if (
    jpegDecoder.getWidth() != request.width ||
    jpegDecoder.getHeight() != request.height
  ) {
    Serial.printf(
      "Resolucion inesperada: %dx%d\n",
      jpegDecoder.getWidth(),
      jpegDecoder.getHeight()
    );
    jpegDecoder.close();
    decodeTarget = nullptr;
    decodeWidth = 0;
    decodeHeight = 0;
    setWorkerStatus("Resolucion JPEG inesperada");
    return false;
  }

  jpegDecoder.setPixelType(RGB565_BIG_ENDIAN);
  int decoded = jpegDecoder.decode(0, 0, 0);
  int jpegError = jpegDecoder.getLastError();
  jpegDecoder.close();

  decodeTarget = nullptr;
  decodeWidth = 0;
  decodeHeight = 0;

  if (!decoded) {
    Serial.printf("Error decodificando JPEG: %d\n", jpegError);
    setWorkerStatus("Error decodificando JPEG");
    return false;
  }

  return true;
}

void snapshotTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    SnapshotRequest request;
    int targetBufferIndex;

    portENTER_CRITICAL(&stateMux);
    request = pendingRequest;
    targetBufferIndex = 1 - frontBufferIndex;
    portEXIT_CRITICAL(&stateMux);

    bool success = downloadAndDecodeSnapshot(request, targetBufferIndex);

    portENTER_CRITICAL(&stateMux);
    if (success) {
      pendingFrameIndex = targetBufferIndex;
      pendingFrameWidth = request.width;
      pendingFrameHeight = request.height;
      frameReady = true;
    }
    downloadBusy = false;
    portEXIT_CRITICAL(&stateMux);

    if (success) {
      setWorkerStatus("Camara actualizada - toca la imagen");
    }
  }
}

// ============================================================
// SETUP Y LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando OctoPrint Viewer...");

  loadSettings();

  if (!gfx->begin()) {
    Serial.println("gfx->begin() fallo");
  }

  gfx->fillScreen(RGB565_BLACK);
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  touch_init(gfx->width(), gfx->height(), gfx->getRotation());
  lv_init();

  uint32_t screenWidth = gfx->width();
  uint32_t screenHeight = gfx->height();
  uint32_t bufferSize = screenWidth * 40;

  lvglBuffer = static_cast<lv_color_t *>(
    heap_caps_malloc(
      sizeof(lv_color_t) * bufferSize,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    )
  );

  if (lvglBuffer == nullptr) {
    lvglBuffer = static_cast<lv_color_t *>(
      heap_caps_malloc(
        sizeof(lv_color_t) * bufferSize,
        MALLOC_CAP_8BIT
      )
    );
  }

  if (lvglBuffer == nullptr) {
    Serial.println("No se pudo reservar buffer LVGL");
    return;
  }

  lv_disp_draw_buf_init(&drawBuffer, lvglBuffer, nullptr, bufferSize);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = screenWidth;
  displayDriver.ver_res = screenHeight;
  displayDriver.flush_cb = myDisplayFlush;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  static lv_indev_drv_t inputDriver;
  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = myTouchpadRead;
  lv_indev_drv_register(&inputDriver);

  if (!allocateCameraMemory()) {
    setCameraStatus("No se pudo reservar PSRAM");
    return;
  }

  createInterface();
  serviceUi();

  BaseType_t taskResult = xTaskCreatePinnedToCore(
    snapshotTask,
    "snapshotTask",
    16384,
    nullptr,
    1,
    &snapshotTaskHandle,
    0
  );

  if (taskResult != pdPASS) {
    snapshotTaskHandle = nullptr;
    setCameraStatus("No se pudo crear la tarea de camara");
    Serial.println("No se pudo crear snapshotTask");
  }

  wifiManager.begin();
  updateNetworkUi();
  Serial.println("Setup terminado");
}

void loop() {
  serviceUi();
  applyWorkerStatus();
  applyPendingFrame();
  serviceWifiConnection();

  if (forceReconnect) {
    forceReconnect = false;
    wifiManager.reconnect();
  }

  if (millis() - lastNetworkUiMs >= 1000) {
    lastNetworkUiMs = millis();
    updateNetworkUi();
  }

  bool cameraTabActive =
    tabView != nullptr && lv_tabview_get_tab_act(tabView) == 1;
  bool shouldUpdateCamera = cameraTabActive || fullscreenActive;
  bool intervalElapsed =
    millis() - lastSnapshotMs >= snapshotIntervalMs;

  if (shouldUpdateCamera && (forceSnapshot || intervalElapsed)) {
    if (queueSnapshotRequest()) {
      forceSnapshot = false;
      lastSnapshotMs = millis();
    }
  }

  delay(5);
}