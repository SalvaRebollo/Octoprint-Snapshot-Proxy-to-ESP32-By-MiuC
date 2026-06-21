#pragma once

#include <stddef.h>

#include "domotica_types.h"

// Archivo privado: añade, elimina o reordena entradas libremente.
// Puedes añadir tantas entradas como necesites.
// No se imprime ninguna URL en el monitor serie.
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