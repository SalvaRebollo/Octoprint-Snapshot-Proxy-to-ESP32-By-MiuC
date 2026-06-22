#pragma once

#include <stddef.h>

#include "domotica_types.h"

// Private file: add, remove, or reorder entries freely.
// You can add as many entries as you need.
// No URLs are printed to the serial monitor.
constexpr DomoticaWebhookConfig DOMOTICA_WEBHOOKS[] = {

  {
    "Luz del salon",
    "https://example.com/webhook-luz-salon"
  },
  {
    "Persiana del dormitorio",
    "https://example.com/webhook-persiana"
  }
  
};

constexpr size_t DOMOTICA_WEBHOOK_COUNT =
  sizeof(DOMOTICA_WEBHOOKS) / sizeof(DOMOTICA_WEBHOOKS[0]);