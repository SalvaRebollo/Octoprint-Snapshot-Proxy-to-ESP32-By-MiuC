#include "../../../core/app_config.h"

#if APP_ENABLE_OCTOPRINT
#include "snapshot_service.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

namespace SnapshotService {
namespace {
constexpr const char *SNAPSHOT_PATH = "/snapshot-lite.jpg";
constexpr uint16_t MAX_CAMERA_WIDTH = 480;
constexpr uint16_t MAX_CAMERA_HEIGHT = 272;
constexpr size_t CAMERA_BUFFER_BYTES =
  MAX_CAMERA_WIDTH * MAX_CAMERA_HEIGHT * sizeof(uint16_t);
constexpr size_t MAX_JPEG_BYTES = 512 * 1024;
constexpr size_t WORKER_STATUS_CAPACITY = 96;

// Image and JPEG buffers in PSRAM. front = the currently displayed buffer; the worker
// always decodes into the other one to avoid overwriting the visible frame.
uint16_t *frameBuffers[2] = {nullptr, nullptr};
uint8_t *jpegBytes = nullptr;
JPEGDEC jpegDecoder;
int frontBufferIndex = 0;
int pendingFrameIndex = -1;
uint16_t pendingFrameWidth = 0;
uint16_t pendingFrameHeight = 0;

// Active destination for the decode callback (used only by the worker task).
uint16_t *decodeTarget = nullptr;
uint16_t decodeWidth = 0;
uint16_t decodeHeight = 0;

Request pendingRequest = {};
TaskHandle_t snapshotTaskHandle = nullptr;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool downloadBusy = false;
volatile bool frameReady = false;
volatile bool workerStatusDirty = false;
char workerStatus[WORKER_STATUS_CAPACITY] = "";

// Index of the frame delivered by takeReadyFrame() that is pending confirmation.
// Only touched by takeReadyFrame()/commitFrame(), both on the main thread.
int acceptedFrameIndex = -1;

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

bool isValidRequest(const Request &request) {
  return isValidIpv4(request.ip) && request.port > 0 &&
    request.width > 0 && request.width <= MAX_CAMERA_WIDTH &&
    request.height > 0 && request.height <= MAX_CAMERA_HEIGHT &&
    request.quality >= 30 && request.quality <= 95 &&
    isfinite(request.zoom) && request.zoom >= 1.0f && request.zoom <= 5.0f &&
    isfinite(request.x) && request.x >= 0.0f && request.x <= 1.0f &&
    isfinite(request.y) && request.y >= 0.0f && request.y <= 1.0f;
}

void setWorkerStatus(const char *text) {
  portENTER_CRITICAL(&stateMux);
  if (strncmp(workerStatus, text, sizeof(workerStatus)) != 0) {
    snprintf(workerStatus, sizeof(workerStatus), "%s", text);
    workerStatusDirty = true;
  }
  portEXIT_CRITICAL(&stateMux);
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

bool downloadAndDecodeSnapshot(const Request &request, int targetBufferIndex) {
  if (
    targetBufferIndex < 0 || targetBufferIndex > 1 ||
    frameBuffers[targetBufferIndex] == nullptr ||
    !isValidRequest(request)
  ) {
    setWorkerStatus("Peticion de captura invalida");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    setWorkerStatus("WiFi desconectado");
    return false;
  }

  char url[320];
  buildUrl(request, url, sizeof(url));
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
    Request request;
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
}  // namespace

void buildUrl(const Request &request, char *url, size_t capacity) {
  snprintf(
    url,
    capacity,
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

bool begin() {
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
  return true;
}

bool startWorker() {
  if (snapshotTaskHandle != nullptr) return true;

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
    Serial.println("No se pudo crear snapshotTask");
    return false;
  }
  return true;
}

bool request(const Request &request) {
  if (snapshotTaskHandle == nullptr) return false;

  bool queued = false;
  portENTER_CRITICAL(&stateMux);
  if (!downloadBusy && !frameReady) {
    pendingRequest = request;
    downloadBusy = true;
    queued = true;
  }
  portEXIT_CRITICAL(&stateMux);

  if (queued) xTaskNotifyGive(snapshotTaskHandle);
  return queued;
}

bool takeReadyFrame(FrameView &out) {
  int readyIndex = -1;
  uint16_t readyWidth = 0;
  uint16_t readyHeight = 0;

  portENTER_CRITICAL(&stateMux);
  if (frameReady) {
    readyIndex = pendingFrameIndex;
    readyWidth = pendingFrameWidth;
    readyHeight = pendingFrameHeight;
  }
  portEXIT_CRITICAL(&stateMux);

  if (readyIndex < 0) return false;

  if (
    readyIndex > 1 ||
    readyWidth == 0 || readyWidth > MAX_CAMERA_WIDTH ||
    readyHeight == 0 || readyHeight > MAX_CAMERA_HEIGHT
  ) {
    portENTER_CRITICAL(&stateMux);
    pendingFrameIndex = -1;
    frameReady = false;
    portEXIT_CRITICAL(&stateMux);
    setWorkerStatus("Frame descartado por dimensiones invalidas");
    return false;
  }

  acceptedFrameIndex = readyIndex;
  out.pixels = frameBuffers[readyIndex];
  out.width = readyWidth;
  out.height = readyHeight;
  return true;
}

void commitFrame() {
  if (acceptedFrameIndex < 0) return;

  portENTER_CRITICAL(&stateMux);
  frontBufferIndex = acceptedFrameIndex;
  pendingFrameIndex = -1;
  frameReady = false;
  portEXIT_CRITICAL(&stateMux);

  acceptedFrameIndex = -1;
}

bool takeStatus(char *buffer, size_t capacity) {
  bool hasStatus = false;

  portENTER_CRITICAL(&stateMux);
  if (workerStatusDirty) {
    snprintf(buffer, capacity, "%s", workerStatus);
    workerStatusDirty = false;
    hasStatus = true;
  }
  portEXIT_CRITICAL(&stateMux);

  return hasStatus;
}

}  // namespace SnapshotService
#endif
