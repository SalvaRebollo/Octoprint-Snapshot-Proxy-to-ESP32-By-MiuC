#include "../../../core/app_config.h"

#if APP_ENABLE_DOMOTICA
#include "webhook_service.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "../features/domotica_config.h"

namespace WebhookService {
namespace {
static_assert(
  DOMOTICA_WEBHOOK_COUNT <= UINT8_MAX,
  "Hay demasiados webhooks para usar indices de 8 bits"
);

QueueHandle_t requestQueue = nullptr;
TaskHandle_t workerHandle = nullptr;
portMUX_TYPE resultMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool workerBusy = false;
volatile bool resultReady = false;
Result pendingResult = {0, false, 0};

bool isValidIndex(uint8_t index) {
  return index < DOMOTICA_WEBHOOK_COUNT;
}

Result executeRequest(uint8_t index) {
  Result result = {index, false, 0};
  if (!isValidIndex(index) || WiFi.status() != WL_CONNECTED) return result;

  const char *url = DOMOTICA_WEBHOOKS[index].url;
  if (url == nullptr || url[0] == '\0') return result;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) return result;

  result.httpCode = http.GET();
  result.success = result.httpCode >= 200 && result.httpCode < 300;
  http.end();

  // Solo se muestra el titulo. La URL y sus tokens nunca se imprimen.
  Serial.printf(
    "Webhook '%s': HTTP %d\n",
    DOMOTICA_WEBHOOKS[index].title,
    result.httpCode
  );
  return result;
}

void workerTask(void *) {
  uint8_t index;
  for (;;) {
    if (xQueueReceive(requestQueue, &index, portMAX_DELAY) != pdTRUE) continue;

    workerBusy = true;
    Result result = executeRequest(index);

    portENTER_CRITICAL(&resultMux);
    pendingResult = result;
    resultReady = true;
    portEXIT_CRITICAL(&resultMux);

    workerBusy = false;
  }
}
}

bool begin() {
  if (workerHandle != nullptr) return true;

  requestQueue = xQueueCreate(1, sizeof(uint8_t));
  if (requestQueue == nullptr) {
    Serial.println("No se pudo crear la cola de webhooks");
    return false;
  }

  BaseType_t taskResult = xTaskCreatePinnedToCore(
    workerTask,
    "webhookTask",
    8192,
    nullptr,
    1,
    &workerHandle,
    0
  );

  if (taskResult != pdPASS) {
    vQueueDelete(requestQueue);
    requestQueue = nullptr;
    workerHandle = nullptr;
    Serial.println("No se pudo crear webhookTask");
    return false;
  }
  return true;
}

uint8_t count() {
  return static_cast<uint8_t>(DOMOTICA_WEBHOOK_COUNT);
}

const char *title(uint8_t index) {
  if (!isValidIndex(index)) return "Accion desconocida";
  const char *value = DOMOTICA_WEBHOOKS[index].title;
  return value != nullptr && value[0] != '\0' ? value : "Accion sin titulo";
}

bool trigger(uint8_t index) {
  if (requestQueue == nullptr || isBusy() || !isValidIndex(index)) return false;
  return xQueueSend(requestQueue, &index, 0) == pdTRUE;
}

bool takeResult(Result &result) {
  bool available = false;

  portENTER_CRITICAL(&resultMux);
  if (resultReady) {
    result = pendingResult;
    resultReady = false;
    available = true;
  }
  portEXIT_CRITICAL(&resultMux);

  return available;
}

bool isBusy() {
  return workerBusy ||
    (requestQueue != nullptr && uxQueueMessagesWaiting(requestQueue) > 0);
}
}
#endif