#pragma once

#include <Arduino.h>

namespace WebhookService {
struct Result {
  uint8_t index;
  bool success;
  int httpCode;
};

bool begin();
uint8_t count();
const char *title(uint8_t index);
bool trigger(uint8_t index);
bool takeResult(Result &result);
bool isBusy();
}