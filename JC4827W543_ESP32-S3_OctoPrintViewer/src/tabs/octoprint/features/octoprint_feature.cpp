#include "../../../core/app_config.h"

#if APP_ENABLE_OCTOPRINT
#include "octoprint_feature.h"

#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>
#include <stdlib.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../../core/app_navigation.h"
#include "../../../core/app_ui.h"

namespace OctoPrintFeature {
namespace {
constexpr const char *SNAPSHOT_PATH = "/snapshot-lite.jpg";
constexpr uint16_t DEFAULT_SNAPSHOT_PORT = 30113;
constexpr size_t SNAPSHOT_IP_CAPACITY = 16;

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
constexpr bool DEFAULT_SHOW_CAMERA_STATUS = true;
constexpr bool DEFAULT_SHOW_REQUEST_PARAMS = true;
constexpr uint8_t DEFAULT_OVERLAY_POSITION = 0;
constexpr float MOVE_STEP = 0.05f;
constexpr float ZOOM_STEP = 0.2f;
constexpr uint16_t MAX_CAMERA_WIDTH = 480;
constexpr uint16_t MAX_CAMERA_HEIGHT = 272;
constexpr size_t CAMERA_BUFFER_BYTES =
  MAX_CAMERA_WIDTH * MAX_CAMERA_HEIGHT * sizeof(uint16_t);
constexpr size_t MAX_JPEG_BYTES = 512 * 1024;

struct SnapshotRequest {
  char ip[SNAPSHOT_IP_CAPACITY];
  uint16_t port;
  uint16_t width;
  uint16_t height;
  uint8_t quality;
  float zoom;
  float x;
  float y;
  uint32_t cacheBust;
};

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

enum class OverlayPosition : uint8_t {
  BOTTOM = 0,
  TOP = 1
};

Preferences preferences;
uint8_t resolutionIndex = DEFAULT_RESOLUTION_INDEX;
uint16_t cameraWidth = 368;
uint16_t cameraHeight = 207;
uint8_t jpegQuality = DEFAULT_JPEG_QUALITY;
float cameraZoom = DEFAULT_ZOOM;
float cameraX = DEFAULT_X;
float cameraY = DEFAULT_Y;
uint32_t snapshotIntervalMs = DEFAULT_INTERVAL_MS;
char snapshotIp[SNAPSHOT_IP_CAPACITY] = "";
uint16_t snapshotPort = DEFAULT_SNAPSHOT_PORT;
bool showCameraStatus = DEFAULT_SHOW_CAMERA_STATUS;
bool showRequestParams = DEFAULT_SHOW_REQUEST_PARAMS;
OverlayPosition overlayPosition =
  static_cast<OverlayPosition>(DEFAULT_OVERLAY_POSITION);

lv_obj_t *cameraFrame = nullptr;
lv_obj_t *cameraImage = nullptr;
lv_obj_t *cameraStatus = nullptr;
lv_obj_t *cameraRequestUrl = nullptr;
lv_obj_t *fullscreenLayer = nullptr;
lv_obj_t *fullscreenImage = nullptr;
lv_obj_t *fullscreenStatus = nullptr;
lv_obj_t *fullscreenRequestUrl = nullptr;
lv_obj_t *cameraControls = nullptr;
lv_obj_t *cameraValuesLabel = nullptr;
lv_obj_t *parametersLayer = nullptr;
lv_obj_t *parametersContent = nullptr;
lv_obj_t *savedStatus = nullptr;
lv_obj_t *resolutionDropdown = nullptr;
lv_obj_t *qualitySlider = nullptr;
lv_obj_t *zoomSlider = nullptr;
lv_obj_t *xSlider = nullptr;
lv_obj_t *ySlider = nullptr;
lv_obj_t *intervalDropdown = nullptr;
lv_obj_t *qualityValueLabel = nullptr;
lv_obj_t *zoomValueLabel = nullptr;
lv_obj_t *xValueLabel = nullptr;
lv_obj_t *yValueLabel = nullptr;
lv_obj_t *snapshotIpTextArea = nullptr;
lv_obj_t *snapshotPortTextArea = nullptr;
lv_obj_t *parametersKeyboard = nullptr;
lv_obj_t *showCameraStatusSwitch = nullptr;
lv_obj_t *showRequestParamsSwitch = nullptr;
lv_obj_t *overlayPositionDropdown = nullptr;

uint16_t *frameBuffers[2] = {nullptr, nullptr};
uint8_t *jpegBytes = nullptr;
lv_img_dsc_t cameraDescriptor = {};
JPEGDEC jpegDecoder;
int frontBufferIndex = 0;
int pendingFrameIndex = -1;
uint16_t pendingFrameWidth = 0;
uint16_t pendingFrameHeight = 0;
uint16_t displayedFrameWidth = 368;
uint16_t displayedFrameHeight = 207;
uint16_t *decodeTarget = nullptr;
uint16_t decodeWidth = 0;
uint16_t decodeHeight = 0;
SnapshotRequest pendingRequest = {};
TaskHandle_t snapshotTaskHandle = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool downloadBusy = false;
volatile bool frameReady = false;
volatile bool workerStatusDirty = false;
char workerStatus[96] = "";

uint32_t lastSnapshotMs = 0;
bool forceSnapshot = false;
bool fullscreenActive = false;

void setReadableText(lv_obj_t *label) {
  if (label == nullptr) return;
  lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
}

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

bool isValidIpv4(const char *value) {
  if (value == nullptr || value[0] == '\0') return false;

  unsigned int octets[4];
  char trailing;
  if (
    sscanf(
      value,
      "%u.%u.%u.%u%c",
      &octets[0],
      &octets[1],
      &octets[2],
      &octets[3],
      &trailing
    ) != 4
  ) {
    return false;
  }

  for (uint8_t i = 0; i < 4; i++) {
    if (octets[i] > 255) return false;
  }
  return true;
}

bool parsePort(const char *value, uint16_t &port) {
  if (value == nullptr || value[0] == '\0') return false;

  char *end = nullptr;
  unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0 || parsed > 65535) {
    return false;
  }

  port = static_cast<uint16_t>(parsed);
  return true;
}

void setLabelTextIfChanged(lv_obj_t *label, const char *text) {
  if (label == nullptr || text == nullptr) return;
  const char *currentText = lv_label_get_text(label);
  if (currentText != nullptr && strcmp(currentText, text) == 0) return;
  lv_label_set_text(label, text);
}

void setStatus(const char *text) {
  setLabelTextIfChanged(cameraStatus, text);
  setLabelTextIfChanged(fullscreenStatus, text);
}

void setRequestUrl(const char *url) {
  setLabelTextIfChanged(cameraRequestUrl, url);
  setLabelTextIfChanged(fullscreenRequestUrl, url);
}

void setObjectVisible(lv_obj_t *object, bool visible) {
  if (object == nullptr) return;
  if (visible) lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void applyOverlayVisibility() {
  setObjectVisible(cameraStatus, showCameraStatus);
  setObjectVisible(fullscreenStatus, showCameraStatus);
  setObjectVisible(cameraRequestUrl, showRequestParams);
  setObjectVisible(fullscreenRequestUrl, showRequestParams);

  bool atTop = overlayPosition == OverlayPosition::TOP;
  if (atTop) {
    if (cameraStatus != nullptr) {
      lv_obj_align(cameraStatus, LV_ALIGN_TOP_MID, 0, 2);
    }
    if (cameraRequestUrl != nullptr) {
      lv_obj_align(
        cameraRequestUrl,
        LV_ALIGN_TOP_MID,
        0,
        showCameraStatus ? 22 : 2
      );
    }
    if (fullscreenStatus != nullptr) {
      lv_obj_set_width(fullscreenStatus, 390);
      lv_obj_align(fullscreenStatus, LV_ALIGN_TOP_LEFT, 4, 4);
    }
    if (fullscreenRequestUrl != nullptr) {
      lv_obj_set_width(fullscreenRequestUrl, 390);
      lv_obj_align(
        fullscreenRequestUrl,
        LV_ALIGN_TOP_LEFT,
        4,
        showCameraStatus ? 24 : 4
      );
    }
    return;
  }

  if (cameraStatus != nullptr) {
    lv_obj_align(
      cameraStatus,
      LV_ALIGN_BOTTOM_MID,
      0,
      showRequestParams ? -22 : -2
    );
  }
  if (cameraRequestUrl != nullptr) {
    lv_obj_align(cameraRequestUrl, LV_ALIGN_BOTTOM_MID, 0, -2);
  }
  if (fullscreenStatus != nullptr) {
    lv_obj_set_width(fullscreenStatus, 420);
    lv_obj_align(
      fullscreenStatus,
      LV_ALIGN_BOTTOM_MID,
      0,
      showRequestParams ? -24 : -4
    );
  }
  if (fullscreenRequestUrl != nullptr) {
    lv_obj_set_width(fullscreenRequestUrl, 470);
    lv_obj_align(fullscreenRequestUrl, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
}

void buildSnapshotUrl(
  const SnapshotRequest &request,
  char *url,
  size_t urlCapacity
) {
  snprintf(
    url,
    urlCapacity,
    "http://%s:%u%s?w=%u&h=%u&q=%u&zoom=%.1f&x=%.2f&y=%.2f&t=%lu",
    request.ip,
    request.port,
    SNAPSHOT_PATH,
    request.width,
    request.height,
    request.quality,
    request.zoom,
    request.x,
    request.y,
    static_cast<unsigned long>(request.cacheBust)
  );
}

void setWorkerStatus(const char *text) {
  portENTER_CRITICAL(&stateMux);
  if (strncmp(workerStatus, text, sizeof(workerStatus)) != 0) {
    snprintf(workerStatus, sizeof(workerStatus), "%s", text);
    workerStatusDirty = true;
  }
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

  if (hasStatus) setStatus(localStatus);
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
  snapshotIp[0] = '\0';
  snapshotPort = DEFAULT_SNAPSHOT_PORT;
  showCameraStatus = DEFAULT_SHOW_CAMERA_STATUS;
  showRequestParams = DEFAULT_SHOW_REQUEST_PARAMS;
  overlayPosition = static_cast<OverlayPosition>(DEFAULT_OVERLAY_POSITION);
}

void loadSettings() {
  if (!preferences.begin("octoview", true)) {
    Serial.println("No se pudo abrir NVS; se usaran valores por defecto");
    setDefaultSettings();
    return;
  }

  resolutionIndex = preferences.getUChar("resolution", DEFAULT_RESOLUTION_INDEX);
  jpegQuality = preferences.getUChar("quality", DEFAULT_JPEG_QUALITY);
  cameraZoom = preferences.getFloat("zoom", DEFAULT_ZOOM);
  cameraX = preferences.getFloat("x", DEFAULT_X);
  cameraY = preferences.getFloat("y", DEFAULT_Y);
  snapshotIntervalMs = preferences.getUInt("interval", DEFAULT_INTERVAL_MS);
  String storedIp = preferences.getString("proxyip", "");
  snapshotPort = preferences.getUShort("proxyport", DEFAULT_SNAPSHOT_PORT);
  showCameraStatus = preferences.getBool("showstatus", DEFAULT_SHOW_CAMERA_STATUS);
  showRequestParams = preferences.getBool("showparams", DEFAULT_SHOW_REQUEST_PARAMS);
  uint8_t storedPosition = preferences.getUChar(
    "labelpos",
    DEFAULT_OVERLAY_POSITION
  );
  overlayPosition = storedPosition <= static_cast<uint8_t>(OverlayPosition::TOP)
    ? static_cast<OverlayPosition>(storedPosition)
    : static_cast<OverlayPosition>(DEFAULT_OVERLAY_POSITION);
  preferences.end();

  storedIp.trim();
  if (storedIp.length() < SNAPSHOT_IP_CAPACITY) {
    storedIp.toCharArray(snapshotIp, SNAPSHOT_IP_CAPACITY);
  } else {
    snapshotIp[0] = '\0';
  }

  if (resolutionIndex >= RESOLUTION_COUNT) resolutionIndex = DEFAULT_RESOLUTION_INDEX;
  jpegQuality = constrain(jpegQuality, 30, 95);
  cameraZoom = isfinite(cameraZoom) ? clampFloat(cameraZoom, 1.0f, 5.0f) : DEFAULT_ZOOM;
  cameraX = isfinite(cameraX) ? clampFloat(cameraX, 0.0f, 1.0f) : DEFAULT_X;
  cameraY = isfinite(cameraY) ? clampFloat(cameraY, 0.0f, 1.0f) : DEFAULT_Y;
  snapshotIntervalMs = normalizeInterval(snapshotIntervalMs);
  if (snapshotIp[0] != '\0' && !isValidIpv4(snapshotIp)) snapshotIp[0] = '\0';
  if (snapshotPort == 0) snapshotPort = DEFAULT_SNAPSHOT_PORT;
  cameraWidth = RESOLUTIONS[resolutionIndex].width;
  cameraHeight = RESOLUTIONS[resolutionIndex].height;
}

bool saveSettings() {
  if (!preferences.begin("octoview", false)) return false;

  bool ok = true;
  ok &= preferences.putUChar("resolution", resolutionIndex) > 0;
  ok &= preferences.putUChar("quality", jpegQuality) > 0;
  ok &= preferences.putFloat("zoom", cameraZoom) > 0;
  ok &= preferences.putFloat("x", cameraX) > 0;
  ok &= preferences.putFloat("y", cameraY) > 0;
  ok &= preferences.putUInt("interval", snapshotIntervalMs) > 0;
  if (snapshotIp[0] == '\0') {
    if (preferences.isKey("proxyip")) ok &= preferences.remove("proxyip");
  } else {
    ok &= preferences.putString("proxyip", snapshotIp) > 0;
  }
  ok &= preferences.putUShort("proxyport", snapshotPort) > 0;
  ok &= preferences.putBool("showstatus", showCameraStatus) > 0;
  ok &= preferences.putBool("showparams", showRequestParams) > 0;
  ok &= preferences.putUChar(
    "labelpos",
    static_cast<uint8_t>(overlayPosition)
  ) > 0;
  preferences.end();
  return ok;
}

void updateCameraValuesLabel() {
  if (cameraValuesLabel == nullptr) return;
  lv_label_set_text_fmt(
    cameraValuesLabel,
    "%ux%u  Q%d  Z %.1fx  X %.2f  Y %.2f",
    cameraWidth,
    cameraHeight,
    jpegQuality,
    cameraZoom,
    cameraX,
    cameraY
  );
}

void updateParameterLabels() {
  if (qualityValueLabel != nullptr) lv_label_set_text_fmt(qualityValueLabel, "%d", jpegQuality);
  if (zoomValueLabel != nullptr) lv_label_set_text_fmt(zoomValueLabel, "%.1fx", cameraZoom);
  if (xValueLabel != nullptr) lv_label_set_text_fmt(xValueLabel, "%.2f", cameraX);
  if (yValueLabel != nullptr) lv_label_set_text_fmt(yValueLabel, "%.2f", cameraY);
  updateCameraValuesLabel();
}

void syncEndpointControls() {
  if (snapshotIpTextArea != nullptr) {
    lv_textarea_set_text(snapshotIpTextArea, snapshotIp);
  }
  if (snapshotPortTextArea != nullptr) {
    char portText[6];
    snprintf(portText, sizeof(portText), "%u", snapshotPort);
    lv_textarea_set_text(snapshotPortTextArea, portText);
  }
}

bool readEndpointControls() {
  if (snapshotIpTextArea == nullptr || snapshotPortTextArea == nullptr) return false;

  const char *ipText = lv_textarea_get_text(snapshotIpTextArea);
  const char *portText = lv_textarea_get_text(snapshotPortTextArea);
  uint16_t parsedPort;

  if (ipText[0] != '\0' && !isValidIpv4(ipText)) {
    lv_label_set_text(savedStatus, "IP invalida. Ejemplo: 192.168.1.10");
    return false;
  }
  if (!parsePort(portText, parsedPort)) {
    lv_label_set_text(savedStatus, "Puerto invalido. Usa un valor entre 1 y 65535");
    return false;
  }

  snprintf(snapshotIp, sizeof(snapshotIp), "%s", ipText);
  snapshotPort = parsedPort;
  return true;
}

void syncParameterControls() {
  if (showCameraStatusSwitch != nullptr) {
    if (showCameraStatus) lv_obj_add_state(showCameraStatusSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(showCameraStatusSwitch, LV_STATE_CHECKED);
  }
  if (showRequestParamsSwitch != nullptr) {
    if (showRequestParams) lv_obj_add_state(showRequestParamsSwitch, LV_STATE_CHECKED);
    else lv_obj_clear_state(showRequestParamsSwitch, LV_STATE_CHECKED);
  }
  if (overlayPositionDropdown != nullptr) {
    lv_dropdown_set_selected(
      overlayPositionDropdown,
      static_cast<uint8_t>(overlayPosition)
    );
  }
  applyOverlayVisibility();

  if (resolutionDropdown != nullptr) lv_dropdown_set_selected(resolutionDropdown, resolutionIndex);
  if (intervalDropdown != nullptr) {
    lv_dropdown_set_selected(intervalDropdown, intervalToDropdownIndex(snapshotIntervalMs));
  }
  if (qualitySlider != nullptr) lv_slider_set_value(qualitySlider, jpegQuality, LV_ANIM_OFF);
  if (zoomSlider != nullptr) lv_slider_set_value(zoomSlider, static_cast<int>(cameraZoom * 10.0f), LV_ANIM_OFF);
  if (xSlider != nullptr) lv_slider_set_value(xSlider, static_cast<int>(cameraX * 100.0f), LV_ANIM_OFF);
  if (ySlider != nullptr) lv_slider_set_value(ySlider, static_cast<int>(cameraY * 100.0f), LV_ANIM_OFF);
  updateParameterLabels();
}

void markSettingsDirty() {
  if (savedStatus != nullptr) lv_label_set_text(savedStatus, "Cambios sin guardar");
}

uint16_t calculateImageZoom(
  uint16_t sourceWidth,
  uint16_t sourceHeight,
  uint16_t targetWidth,
  uint16_t targetHeight,
  bool cover
) {
  uint32_t zoomX = (static_cast<uint32_t>(targetWidth) * 256) / sourceWidth;
  uint32_t zoomY = (static_cast<uint32_t>(targetHeight) * 256) / sourceHeight;
  uint32_t zoom = cover ? max(zoomX, zoomY) : min(zoomX, zoomY);
  if (zoom > 768) zoom = 768;
  if (zoom < 1) zoom = 1;
  return static_cast<uint16_t>(zoom);
}

void updateImageLayout() {
  if (displayedFrameWidth == 0 || displayedFrameHeight == 0) return;

  if (cameraFrame != nullptr && cameraImage != nullptr) {
    lv_obj_update_layout(cameraFrame);
    uint16_t frameWidth = lv_obj_get_content_width(cameraFrame);
    uint16_t frameHeight = lv_obj_get_content_height(cameraFrame);
    lv_img_set_zoom(
      cameraImage,
      calculateImageZoom(
        displayedFrameWidth,
        displayedFrameHeight,
        frameWidth,
        frameHeight,
        true
      )
    );
    lv_obj_center(cameraImage);
  }

  if (fullscreenImage != nullptr) {
    lv_img_set_zoom(
      fullscreenImage,
      calculateImageZoom(displayedFrameWidth, displayedFrameHeight, 480, 272, false)
    );
    lv_obj_center(fullscreenImage);
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
  if (lv_obj_has_flag(cameraControls, LV_OBJ_FLAG_HIDDEN)) showCameraControls();
  else hideControls();
}

void enterFullscreen() {
  if (fullscreenLayer == nullptr) return;
  hideControls();
  fullscreenActive = true;
  lv_obj_clear_flag(fullscreenLayer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(fullscreenLayer);
  updateImageLayout();
}

void exitFullscreen() {
  if (fullscreenLayer == nullptr) return;
  fullscreenActive = false;
  lv_obj_add_flag(fullscreenLayer, LV_OBJ_FLAG_HIDDEN);
  hideControls();
}

void onCameraAreaTap(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) toggleCameraControls();
}

void onFullscreenExit(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) exitFullscreen();
}

void hideParametersKeyboard() {
  if (parametersKeyboard == nullptr) return;
  lv_obj_add_flag(parametersKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *textArea = lv_keyboard_get_textarea(parametersKeyboard);
  if (textArea != nullptr) lv_obj_clear_state(textArea, LV_STATE_FOCUSED);
}

void hideParameterScreen() {
  hideParametersKeyboard();
  if (parametersLayer != nullptr) {
    lv_obj_add_flag(parametersLayer, LV_OBJ_FLAG_HIDDEN);
  }
}

void showParameterScreen() {
  if (parametersLayer == nullptr) return;
  hideControls();
  syncParameterControls();
  if (parametersContent != nullptr) {
    lv_obj_scroll_to_y(parametersContent, 0, LV_ANIM_OFF);
  }
  lv_obj_clear_flag(parametersLayer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(parametersLayer);
}

void onCloseParameters(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  hideParameterScreen();
}

void onViewCamera(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  hideParameterScreen();
  appShowPage(AppPage::OCTOPRINT);
  forceSnapshot = true;
}

void onEndpointTextEvent(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *textArea = lv_event_get_target(event);

  if (code == LV_EVENT_VALUE_CHANGED) {
    markSettingsDirty();
    return;
  }

  if (
    (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) &&
    parametersKeyboard != nullptr
  ) {
    lv_keyboard_set_mode(parametersKeyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(parametersKeyboard, textArea);
    lv_obj_clear_flag(parametersKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(parametersKeyboard);
    if (parametersContent != nullptr) {
      lv_obj_scroll_to_y(parametersContent, 0, LV_ANIM_OFF);
    }
  }
}

void onParametersKeyboardEvent(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    hideParametersKeyboard();
  }
}

void onSaveSettings(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (!readEndpointControls()) return;

  bool saved = saveSettings();
  lv_label_set_text(savedStatus, saved ? "Valores guardados" : "Error al guardar");
  if (snapshotIp[0] == '\0') {
    setRequestUrl("");
    setStatus("Configura la IP del proxy en PARAMETROS");
  } else {
    setStatus("Aplicando parametros...");
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
      hideControls();
      return;
    case CAMERA_FULLSCREEN:
      enterFullscreen();
      return;
    case CAMERA_SETTINGS:
      exitFullscreen();
      showParameterScreen();
      return;
  }

  cameraZoom = clampFloat(cameraZoom, 1.0f, 5.0f);
  cameraX = clampFloat(cameraX, 0.0f, 1.0f);
  cameraY = clampFloat(cameraY, 0.0f, 1.0f);
  syncParameterControls();
  markSettingsDirty();
  setStatus("Actualizando encuadre...");
  forceSnapshot = true;
}

void onResolutionChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
  resolutionIndex = lv_dropdown_get_selected(resolutionDropdown);
  if (resolutionIndex >= RESOLUTION_COUNT) resolutionIndex = DEFAULT_RESOLUTION_INDEX;
  cameraWidth = RESOLUTIONS[resolutionIndex].width;
  cameraHeight = RESOLUTIONS[resolutionIndex].height;
  updateCameraValuesLabel();
  markSettingsDirty();
  setStatus("Cambiando resolucion...");
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

void onVisibilityOptionChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

  lv_obj_t *target = lv_event_get_target(event);
  bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
  if (target == showCameraStatusSwitch) showCameraStatus = enabled;
  else if (target == showRequestParamsSwitch) showRequestParams = enabled;
  else return;

  applyOverlayVisibility();
  markSettingsDirty();
}

void onOverlayPositionChanged(lv_event_t *event) {
  if (
    lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
    overlayPositionDropdown == nullptr
  ) {
    return;
  }

  uint16_t selected = lv_dropdown_get_selected(overlayPositionDropdown);
  overlayPosition = selected == static_cast<uint8_t>(OverlayPosition::TOP)
    ? OverlayPosition::TOP
    : OverlayPosition::BOTTOM;
  applyOverlayVisibility();
  markSettingsDirty();
}

void onParameterSlider(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  ParameterType type = static_cast<ParameterType>(
    reinterpret_cast<intptr_t>(lv_event_get_user_data(event))
  );

  if (code == LV_EVENT_VALUE_CHANGED) {
    switch (type) {
      case PARAM_QUALITY: jpegQuality = lv_slider_get_value(qualitySlider); break;
      case PARAM_ZOOM: cameraZoom = lv_slider_get_value(zoomSlider) / 10.0f; break;
      case PARAM_X: cameraX = lv_slider_get_value(xSlider) / 100.0f; break;
      case PARAM_Y: cameraY = lv_slider_get_value(ySlider) / 100.0f; break;
    }
    updateParameterLabels();
    markSettingsDirty();
  }

  if (code == LV_EVENT_RELEASED) {
    setStatus("Aplicando parametros...");
    forceSnapshot = true;
  }
}

void onResetParameters(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  setDefaultSettings();
  setRequestUrl("");
  syncEndpointControls();
  syncParameterControls();
  lv_label_set_text(
    savedStatus,
    saveSettings() ? "Valores por defecto guardados" : "Error al guardar valores por defecto"
  );
  setStatus("Restaurando parametros...");
  forceSnapshot = true;
}

lv_obj_t *createActionButton(
  lv_obj_t *parent,
  const char *text,
  lv_coord_t x,
  lv_coord_t y,
  lv_coord_t width,
  lv_coord_t height,
  CameraAction action
) {
  return appCreateButton(
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

bool allocateCameraMemory() {
  frameBuffers[0] = static_cast<uint16_t *>(
    heap_caps_malloc(CAMERA_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  );
  frameBuffers[1] = static_cast<uint16_t *>(
    heap_caps_malloc(CAMERA_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  );
  jpegBytes = static_cast<uint8_t *>(
    heap_caps_malloc(MAX_JPEG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  );

  if (frameBuffers[0] == nullptr || frameBuffers[1] == nullptr || jpegBytes == nullptr) {
    Serial.println("No se pudo reservar PSRAM para OctoPrint");
    return false;
  }

  memset(frameBuffers[0], 0, CAMERA_BUFFER_BYTES);
  memset(frameBuffers[1], 0, CAMERA_BUFFER_BYTES);
  cameraDescriptor.header.always_zero = 0;
  cameraDescriptor.header.w = displayedFrameWidth;
  cameraDescriptor.header.h = displayedFrameHeight;
  cameraDescriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
  cameraDescriptor.data_size = displayedFrameWidth * displayedFrameHeight * sizeof(uint16_t);
  cameraDescriptor.data = reinterpret_cast<const uint8_t *>(frameBuffers[frontBufferIndex]);
  return true;
}

int drawJpegBlock(JPEGDRAW *draw) {
  if (decodeTarget == nullptr) return 0;
  if (draw->x >= decodeWidth || draw->y >= decodeHeight) return 1;

  int sourceWidth = draw->iWidth;
  int usableWidth = draw->iWidthUsed > 0 ? draw->iWidthUsed : draw->iWidth;
  int copyWidth = min(usableWidth, static_cast<int>(decodeWidth) - draw->x);
  int copyHeight = min(draw->iHeight, static_cast<int>(decodeHeight) - draw->y);

  for (int row = 0; row < copyHeight; row++) {
    uint16_t *destination = decodeTarget + ((draw->y + row) * decodeWidth) + draw->x;
    uint16_t *source = draw->pPixels + (row * sourceWidth);
    memcpy(destination, source, copyWidth * sizeof(uint16_t));
  }
  return 1;
}

bool isValidSnapshotRequest(const SnapshotRequest &request) {
  return isValidIpv4(request.ip) && request.port > 0 &&
    request.width > 0 && request.width <= MAX_CAMERA_WIDTH &&
    request.height > 0 && request.height <= MAX_CAMERA_HEIGHT &&
    request.quality >= 30 && request.quality <= 95 &&
    isfinite(request.zoom) && request.zoom >= 1.0f && request.zoom <= 5.0f &&
    isfinite(request.x) && request.x >= 0.0f && request.x <= 1.0f &&
    isfinite(request.y) && request.y >= 0.0f && request.y <= 1.0f;
}

bool queueSnapshotRequest() {
  if (snapshotTaskHandle == nullptr || WiFi.status() != WL_CONNECTED) return false;
  if (snapshotIp[0] == '\0') {
    setStatus("Configura la IP del proxy en PARAMETROS");
    return false;
  }

  SnapshotRequest request = {};
  snprintf(request.ip, sizeof(request.ip), "%s", snapshotIp);
  request.port = snapshotPort;
  request.width = cameraWidth;
  request.height = cameraHeight;
  request.quality = jpegQuality;
  request.zoom = cameraZoom;
  request.x = cameraX;
  request.y = cameraY;
  request.cacheBust = millis();

  bool queued = false;
  portENTER_CRITICAL(&stateMux);
  if (!downloadBusy && !frameReady) {
    pendingRequest = request;
    downloadBusy = true;
    queued = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (queued) {
    char requestUrl[320];
    buildSnapshotUrl(request, requestUrl, sizeof(requestUrl));
    const char *parameters = strchr(requestUrl, '?');
    setRequestUrl(parameters != nullptr ? parameters : "");
    xTaskNotifyGive(snapshotTaskHandle);
  }
  return queued;
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
    newFrontIndex < 0 || newFrontIndex > 1 ||
    newWidth == 0 || newWidth > MAX_CAMERA_WIDTH ||
    newHeight == 0 || newHeight > MAX_CAMERA_HEIGHT
  ) {
    if (newFrontIndex >= 0) {
      portENTER_CRITICAL(&stateMux);
      pendingFrameIndex = -1;
      frameReady = false;
      portEXIT_CRITICAL(&stateMux);
      setStatus("Frame descartado por dimensiones invalidas");
    }
    return;
  }

  lv_img_cache_invalidate_src(&cameraDescriptor);
  displayedFrameWidth = newWidth;
  displayedFrameHeight = newHeight;
  cameraDescriptor.header.w = newWidth;
  cameraDescriptor.header.h = newHeight;
  cameraDescriptor.data_size = newWidth * newHeight * sizeof(uint16_t);
  cameraDescriptor.data = reinterpret_cast<const uint8_t *>(frameBuffers[newFrontIndex]);
  if (cameraImage != nullptr) lv_img_set_src(cameraImage, &cameraDescriptor);
  if (fullscreenImage != nullptr) lv_img_set_src(fullscreenImage, &cameraDescriptor);
  updateImageLayout();
  if (cameraImage != nullptr) lv_obj_invalidate(cameraImage);
  if (fullscreenImage != nullptr) lv_obj_invalidate(fullscreenImage);

  portENTER_CRITICAL(&stateMux);
  frontBufferIndex = newFrontIndex;
  pendingFrameIndex = -1;
  frameReady = false;
  portEXIT_CRITICAL(&stateMux);
}

bool downloadAndDecodeSnapshot(const SnapshotRequest &request, int targetBufferIndex) {
  if (
    targetBufferIndex < 0 || targetBufferIndex > 1 ||
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
  buildSnapshotUrl(request, url, sizeof(url));
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
      size_t bytesToRead = min(static_cast<size_t>(availableBytes), remaining);
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
    Serial.printf("JPEG incompleto: %u/%d bytes\n", static_cast<unsigned>(totalRead), contentLength);
    setWorkerStatus("Captura JPEG incompleta");
    return false;
  }

  decodeTarget = frameBuffers[targetBufferIndex];
  decodeWidth = request.width;
  decodeHeight = request.height;
  memset(decodeTarget, 0, CAMERA_BUFFER_BYTES);

  if (!jpegDecoder.openRAM(jpegBytes, static_cast<int>(totalRead), drawJpegBlock)) {
    Serial.printf("Error abriendo JPEG: %d\n", jpegDecoder.getLastError());
    decodeTarget = nullptr;
    decodeWidth = 0;
    decodeHeight = 0;
    setWorkerStatus("La captura no es un JPEG valido");
    return false;
  }

  if (jpegDecoder.getWidth() != request.width || jpegDecoder.getHeight() != request.height) {
    Serial.printf("Resolucion inesperada: %dx%d\n", jpegDecoder.getWidth(), jpegDecoder.getHeight());
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

    if (success) setWorkerStatus("Camara activa - toca para controles");
  }
}
}

bool begin() {
  loadSettings();
  return allocateCameraMemory();
}

void startWorker() {
  if (snapshotTaskHandle != nullptr) return;

  BaseType_t result = xTaskCreatePinnedToCore(
    snapshotTask,
    "snapshotTask",
    16384,
    nullptr,
    1,
    &snapshotTaskHandle,
    0
  );

  if (result != pdPASS) {
    snapshotTaskHandle = nullptr;
    setStatus("No se pudo crear la tarea de camara");
    Serial.println("No se pudo crear snapshotTask");
  }
}

void detachTabUi() {
  cameraFrame = nullptr;
  cameraImage = nullptr;
  cameraStatus = nullptr;
  cameraRequestUrl = nullptr;
}

void createTab(lv_obj_t *parent) {
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);

  cameraFrame = lv_obj_create(parent);
  lv_obj_set_size(cameraFrame, LV_PCT(100), LV_PCT(100));
  lv_obj_center(cameraFrame);
  lv_obj_clear_flag(cameraFrame, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cameraFrame, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(cameraFrame, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cameraFrame, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(cameraFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(cameraFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(cameraFrame, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(cameraFrame, onCameraAreaTap, LV_EVENT_CLICKED, nullptr);

  cameraImage = lv_img_create(cameraFrame);
  lv_obj_center(cameraImage);

  cameraStatus = lv_label_create(parent);
  lv_label_set_text(cameraStatus, "Esperando conexion...");
  lv_obj_set_width(cameraStatus, 470);
  lv_obj_set_style_text_align(cameraStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  setReadableText(cameraStatus);
  lv_obj_set_style_bg_color(cameraStatus, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cameraStatus, LV_OPA_70, LV_PART_MAIN);
  lv_obj_align(cameraStatus, LV_ALIGN_BOTTOM_MID, 0, -22);

  cameraRequestUrl = lv_label_create(parent);
  lv_label_set_text(cameraRequestUrl, "");
  lv_obj_set_width(cameraRequestUrl, 470);
  lv_label_set_long_mode(cameraRequestUrl, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(cameraRequestUrl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  setReadableText(cameraRequestUrl);
  lv_obj_set_style_text_opa(cameraRequestUrl, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_bg_color(cameraRequestUrl, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cameraRequestUrl, LV_OPA_70, LV_PART_MAIN);
  lv_obj_align(cameraRequestUrl, LV_ALIGN_BOTTOM_MID, 0, -2);
}

lv_coord_t createSettingsSection(lv_obj_t *parent, lv_coord_t startY) {
  lv_obj_t *proxyTitle = lv_label_create(parent);
  lv_label_set_text(proxyTitle, "Conexion del proxy");
  lv_obj_set_pos(proxyTitle, 160, startY + 5);
  setReadableText(proxyTitle);

  lv_obj_t *ipLabel = lv_label_create(parent);
  lv_label_set_text(ipLabel, "IP");
  lv_obj_set_pos(ipLabel, 10, startY + 43);
  setReadableText(ipLabel);

  snapshotIpTextArea = lv_textarea_create(parent);
  lv_textarea_set_one_line(snapshotIpTextArea, true);
  lv_textarea_set_max_length(snapshotIpTextArea, SNAPSHOT_IP_CAPACITY - 1);
  lv_textarea_set_accepted_chars(snapshotIpTextArea, "0123456789.");
  lv_textarea_set_placeholder_text(snapshotIpTextArea, "192.168.1.10");
  lv_obj_set_pos(snapshotIpTextArea, 45, startY + 32);
  lv_obj_set_size(snapshotIpTextArea, 225, 40);
  lv_obj_add_event_cb(snapshotIpTextArea, onEndpointTextEvent, LV_EVENT_ALL, nullptr);

  lv_obj_t *portLabel = lv_label_create(parent);
  lv_label_set_text(portLabel, "Puerto");
  lv_obj_set_pos(portLabel, 285, startY + 43);
  setReadableText(portLabel);

  snapshotPortTextArea = lv_textarea_create(parent);
  lv_textarea_set_one_line(snapshotPortTextArea, true);
  lv_textarea_set_max_length(snapshotPortTextArea, 5);
  lv_textarea_set_accepted_chars(snapshotPortTextArea, "0123456789");
  lv_obj_set_pos(snapshotPortTextArea, 350, startY + 32);
  lv_obj_set_size(snapshotPortTextArea, 100, 40);
  lv_obj_add_event_cb(snapshotPortTextArea, onEndpointTextEvent, LV_EVENT_ALL, nullptr);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Parametros del snapshot");
  lv_obj_set_pos(title, 130, startY + 95);
  setReadableText(title);

  lv_obj_t *resolutionLabel = lv_label_create(parent);
  lv_label_set_text(resolutionLabel, "Resolucion");
  lv_obj_set_pos(resolutionLabel, 10, startY + 132);
  setReadableText(resolutionLabel);

  resolutionDropdown = lv_dropdown_create(parent);
  lv_dropdown_set_options(resolutionDropdown, "320 x 180\n368 x 207\n480 x 270\n480 x 272");
  lv_obj_set_pos(resolutionDropdown, 100, startY + 122);
  lv_obj_set_size(resolutionDropdown, 125, 40);
  lv_obj_add_event_cb(resolutionDropdown, onResolutionChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *intervalLabel = lv_label_create(parent);
  lv_label_set_text(intervalLabel, "Refresco");
  lv_obj_set_pos(intervalLabel, 245, startY + 132);
  setReadableText(intervalLabel);

  intervalDropdown = lv_dropdown_create(parent);
  lv_dropdown_set_options(intervalDropdown, "0.5 s\n1 s\n2 s\n5 s");
  lv_obj_set_pos(intervalDropdown, 320, startY + 122);
  lv_obj_set_size(intervalDropdown, 120, 40);
  lv_obj_add_event_cb(intervalDropdown, onIntervalChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  const char *names[] = {"Calidad", "Zoom", "X", "Y"};
  lv_obj_t **values[] = {&qualityValueLabel, &zoomValueLabel, &xValueLabel, &yValueLabel};
  lv_obj_t **sliders[] = {&qualitySlider, &zoomSlider, &xSlider, &ySlider};
  ParameterType types[] = {PARAM_QUALITY, PARAM_ZOOM, PARAM_X, PARAM_Y};
  int minimums[] = {30, 10, 0, 0};
  int maximums[] = {95, 50, 100, 100};

  for (uint8_t i = 0; i < 4; i++) {
    lv_coord_t y = startY + 182 + (i * 45);
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, names[i]);
    lv_obj_set_pos(label, 10, y);
    setReadableText(label);

    *values[i] = lv_label_create(parent);
    lv_obj_set_pos(*values[i], 75, y);
    setReadableText(*values[i]);

    *sliders[i] = lv_slider_create(parent);
    lv_obj_set_pos(*sliders[i], 125, y + 1);
    lv_obj_set_size(*sliders[i], 315, 18);
    lv_slider_set_range(*sliders[i], minimums[i], maximums[i]);
    lv_obj_add_event_cb(
      *sliders[i],
      onParameterSlider,
      LV_EVENT_ALL,
      reinterpret_cast<void *>(static_cast<intptr_t>(types[i]))
    );
  }

  lv_obj_t *showStatusLabel = lv_label_create(parent);
  lv_label_set_text(showStatusLabel, "Mostrar estado");
  lv_obj_set_pos(showStatusLabel, 10, startY + 370);
  setReadableText(showStatusLabel);

  showCameraStatusSwitch = lv_switch_create(parent);
  lv_obj_set_pos(showCameraStatusSwitch, 125, startY + 360);
  lv_obj_set_size(showCameraStatusSwitch, 55, 32);
  lv_obj_add_event_cb(
    showCameraStatusSwitch,
    onVisibilityOptionChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

  lv_obj_t *showParamsLabel = lv_label_create(parent);
  lv_label_set_text(showParamsLabel, "Mostrar parametros");
  lv_obj_set_pos(showParamsLabel, 220, startY + 370);
  setReadableText(showParamsLabel);

  showRequestParamsSwitch = lv_switch_create(parent);
  lv_obj_set_pos(showRequestParamsSwitch, 380, startY + 360);
  lv_obj_set_size(showRequestParamsSwitch, 55, 32);
  lv_obj_add_event_cb(
    showRequestParamsSwitch,
    onVisibilityOptionChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

  lv_obj_t *positionLabel = lv_label_create(parent);
  lv_label_set_text(positionLabel, "Posicion de textos");
  lv_obj_set_pos(positionLabel, 10, startY + 420);
  setReadableText(positionLabel);

  overlayPositionDropdown = lv_dropdown_create(parent);
  lv_dropdown_set_options(overlayPositionDropdown, "Inferior\nSuperior");
  lv_obj_set_pos(overlayPositionDropdown, 170, startY + 408);
  lv_obj_set_size(overlayPositionDropdown, 170, 42);
  lv_obj_add_event_cb(
    overlayPositionDropdown,
    onOverlayPositionChanged,
    LV_EVENT_VALUE_CHANGED,
    nullptr
  );

  appCreateButton(parent, "VER CAMARA", 10, startY + 475, 135, 40, onViewCamera);
  appCreateButton(parent, "GUARDAR", 155, startY + 475, 115, 40, onSaveSettings);
  appCreateButton(parent, "RESET", 280, startY + 475, 80, 40, onResetParameters);

  savedStatus = lv_label_create(parent);
  lv_label_set_text(savedStatus, "Valores cargados");
  lv_obj_set_width(savedStatus, 450);
  lv_label_set_long_mode(savedStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(savedStatus, 10, startY + 527);
  setReadableText(savedStatus);

  lv_obj_t *bottomSpacer = lv_obj_create(parent);
  lv_obj_set_pos(bottomSpacer, 0, startY + 580);
  lv_obj_set_size(bottomSpacer, 1, 25);
  lv_obj_set_style_bg_opa(bottomSpacer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(bottomSpacer, 0, LV_PART_MAIN);

  syncEndpointControls();
  syncParameterControls();
  lv_label_set_text(savedStatus, "Valores cargados");
  return startY + 610;
}
void createOverlays() {
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
  setReadableText(fullscreenStatus);
  lv_obj_set_style_bg_color(fullscreenStatus, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(fullscreenStatus, LV_OPA_50, LV_PART_MAIN);
  lv_obj_align(fullscreenStatus, LV_ALIGN_BOTTOM_MID, 0, -24);

  fullscreenRequestUrl = lv_label_create(fullscreenLayer);
  lv_label_set_text(fullscreenRequestUrl, "");
  lv_obj_set_width(fullscreenRequestUrl, 470);
  lv_label_set_long_mode(fullscreenRequestUrl, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(fullscreenRequestUrl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  setReadableText(fullscreenRequestUrl);
  lv_obj_set_style_text_opa(fullscreenRequestUrl, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_bg_color(fullscreenRequestUrl, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(fullscreenRequestUrl, LV_OPA_50, LV_PART_MAIN);
  lv_obj_align(fullscreenRequestUrl, LV_ALIGN_BOTTOM_MID, 0, -4);

  lv_obj_t *exitButton = appCreateButton(
    fullscreenLayer, "SALIR", 414, 5, 60, 34, onFullscreenExit
  );
  lv_obj_move_foreground(exitButton);
  lv_obj_add_flag(fullscreenLayer, LV_OBJ_FLAG_HIDDEN);

  cameraControls = lv_obj_create(lv_layer_top());
  lv_obj_set_size(cameraControls, 340, 244);
  lv_obj_align(cameraControls, LV_ALIGN_CENTER, 0, 0);
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
  setReadableText(title);

  cameraValuesLabel = lv_label_create(cameraControls);
  lv_obj_set_width(cameraValuesLabel, 318);
  lv_obj_set_style_text_align(cameraValuesLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_pos(cameraValuesLabel, 10, 32);
  setReadableText(cameraValuesLabel);

  createActionButton(cameraControls, "X", 298, 5, 34, 30, CAMERA_CLOSE);
  createActionButton(cameraControls, "^", 145, 55, 50, 36, CAMERA_UP);
  createActionButton(cameraControls, "<", 87, 95, 50, 36, CAMERA_LEFT);
  createActionButton(cameraControls, "RESET", 143, 95, 54, 36, CAMERA_RESET);
  createActionButton(cameraControls, ">", 203, 95, 50, 36, CAMERA_RIGHT);
  createActionButton(cameraControls, "v", 145, 135, 50, 36, CAMERA_DOWN);
  createActionButton(cameraControls, "ZOOM -", 10, 135, 100, 36, CAMERA_ZOOM_OUT);
  createActionButton(cameraControls, "ZOOM +", 230, 135, 100, 36, CAMERA_ZOOM_IN);
  createActionButton(cameraControls, "FULLSCREEN", 10, 184, 150, 38, CAMERA_FULLSCREEN);
  createActionButton(cameraControls, "PARAMETROS", 180, 184, 150, 38, CAMERA_SETTINGS);

  updateCameraValuesLabel();
  lv_obj_add_flag(cameraControls, LV_OBJ_FLAG_HIDDEN);

  parametersLayer = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(parametersLayer, 0, 0);
  lv_obj_set_size(parametersLayer, 480, 272);
  lv_obj_clear_flag(parametersLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(parametersLayer, lv_color_hex(0x101010), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(parametersLayer, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(parametersLayer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(parametersLayer, 0, LV_PART_MAIN);

  lv_obj_t *parametersTitle = lv_label_create(parametersLayer);
  lv_label_set_text(parametersTitle, "Parametros OctoPrint");
  lv_obj_set_pos(parametersTitle, 10, 10);
  setReadableText(parametersTitle);

  appCreateButton(
    parametersLayer,
    "CERRAR",
    390,
    3,
    80,
    32,
    onCloseParameters
  );

  parametersContent = lv_obj_create(parametersLayer);
  lv_obj_set_pos(parametersContent, 0, 38);
  lv_obj_set_size(parametersContent, 480, 234);
  lv_obj_add_flag(parametersContent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(parametersContent, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(parametersContent, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_opa(parametersContent, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(parametersContent, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(parametersContent, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(parametersContent, 25, LV_PART_MAIN);

  createSettingsSection(parametersContent, 0);

  parametersKeyboard = lv_keyboard_create(parametersLayer);
  lv_keyboard_set_mode(parametersKeyboard, LV_KEYBOARD_MODE_NUMBER);
  lv_obj_set_size(parametersKeyboard, 480, 146);
  lv_obj_align(parametersKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(
    parametersKeyboard,
    onParametersKeyboardEvent,
    LV_EVENT_ALL,
    nullptr
  );
  lv_obj_add_flag(parametersKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(parametersLayer, LV_OBJ_FLAG_HIDDEN);
}

void showSettings() {
  exitFullscreen();
  showParameterScreen();
}

void loop(bool tabActive) {
  applyWorkerStatus();
  applyPendingFrame();

  bool shouldUpdate = tabActive || fullscreenActive;
  if (shouldUpdate && snapshotIp[0] == '\0') {
    setStatus("Configura la IP del proxy en PARAMETROS");
    forceSnapshot = false;
    return;
  }

  bool intervalElapsed = millis() - lastSnapshotMs >= snapshotIntervalMs;
  if (shouldUpdate && (forceSnapshot || intervalElapsed)) {
    if (queueSnapshotRequest()) {
      forceSnapshot = false;
      lastSnapshotMs = millis();
    }
  }
}

void onTabActivated() {
  hideControls();
  forceSnapshot = true;
}

void onWifiConnectionChanged(bool connected) {
  if (connected) {
    setStatus("WiFi conectado. Cargando camara...");
    forceSnapshot = true;
  } else {
    setStatus("WiFi desconectado");
  }
}

void hideControls() {
  if (cameraControls != nullptr) lv_obj_add_flag(cameraControls, LV_OBJ_FLAG_HIDDEN);
}

bool isFullscreen() {
  return fullscreenActive;
}
}
#endif
