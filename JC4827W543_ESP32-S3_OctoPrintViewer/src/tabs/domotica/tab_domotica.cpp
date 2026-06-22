#include "../../core/app_config.h"

#if APP_ENABLE_DOMOTICA
#include "tab_domotica.h"

#include <WiFi.h>

#include "../../core/app_ui.h"
#include "services/webhook_service.h"

namespace DomoticaTab {
namespace {
constexpr lv_coord_t BUTTON_WIDTH = 214;
constexpr lv_coord_t BUTTON_HEIGHT = 56;
constexpr lv_coord_t COLUMN_GAP = 10;
constexpr lv_coord_t ROW_GAP = 10;

lv_obj_t *statusLabel = nullptr;
lv_obj_t *actionsContainer = nullptr;

void setButtonsEnabled(bool enabled) {
  if (actionsContainer == nullptr) return;

  uint32_t childCount = lv_obj_get_child_cnt(actionsContainer);
  for (uint32_t i = 0; i < childCount; i++) {
    lv_obj_t *button = lv_obj_get_child(actionsContainer, i);
    if (enabled) lv_obj_clear_state(button, LV_STATE_DISABLED);
    else lv_obj_add_state(button, LV_STATE_DISABLED);
  }
}

void requestAction(uint8_t index) {
  if (WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(statusLabel, "Sin WiFi: no se puede enviar la orden");
    return;
  }

  if (!WebhookService::trigger(index)) {
    lv_label_set_text(statusLabel, "Hay otra orden en curso. Espera un momento");
    return;
  }

  lv_label_set_text_fmt(
    statusLabel,
    "Enviando: %s...",
    WebhookService::title(index)
  );
  setButtonsEnabled(false);
}

void onWebhookClick(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

  uint8_t index = static_cast<uint8_t>(
    reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))
  );
  requestAction(index);
}

void configureButtonLabel(lv_obj_t *button) {
  lv_obj_t *label = lv_obj_get_child(button, 0);
  if (label == nullptr) return;

  lv_obj_set_width(label, BUTTON_WIDTH - 16);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(label);
}
}

bool begin() {
  return WebhookService::begin();
}

void detachUi() {
  statusLabel = nullptr;
  actionsContainer = nullptr;
}

void create(lv_obj_t *parent) {
  lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(parent, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_pad_bottom(parent, 20, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Domotica");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *explanation = lv_label_create(parent);
  lv_label_set_text(explanation, "Acciones configuradas en domotica_config.h");
  lv_obj_set_width(explanation, 450);
  lv_obj_set_style_text_align(explanation, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_pos(explanation, 7, 38);

  uint8_t actionCount = WebhookService::count();
  uint8_t rowCount = (actionCount + 1) / 2;
  lv_coord_t containerHeight =
    rowCount > 0 ? rowCount * (BUTTON_HEIGHT + ROW_GAP) : 1;

  actionsContainer = lv_obj_create(parent);
  lv_obj_set_pos(actionsContainer, 7, 65);
  lv_obj_set_size(actionsContainer, 450, containerHeight);
  lv_obj_clear_flag(actionsContainer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(actionsContainer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(actionsContainer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(actionsContainer, 0, LV_PART_MAIN);

  for (uint8_t i = 0; i < actionCount; i++) {
    lv_coord_t column = i % 2;
    lv_coord_t row = i / 2;
    lv_coord_t x = column * (BUTTON_WIDTH + COLUMN_GAP);
    lv_coord_t y = row * (BUTTON_HEIGHT + ROW_GAP);

    lv_obj_t *button = appCreateButton(
      actionsContainer,
      WebhookService::title(i),
      x,
      y,
      BUTTON_WIDTH,
      BUTTON_HEIGHT,
      onWebhookClick,
      reinterpret_cast<void *>(static_cast<uintptr_t>(i))
    );
    configureButtonLabel(button);
  }

  if (actionCount == 0) {
    lv_obj_t *emptyLabel = lv_label_create(actionsContainer);
    lv_label_set_text(emptyLabel, "No hay acciones configuradas");
    lv_obj_center(emptyLabel);
  }

  statusLabel = lv_label_create(parent);
  lv_label_set_text(statusLabel, "Lista para enviar ordenes");
  lv_obj_set_width(statusLabel, 450);
  lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_pos(statusLabel, 7, 75 + containerHeight);

  appCreateSpacer(parent, 0, 110 + containerHeight, 1, 25);
}

void loop() {
  WebhookService::Result result;
  if (!WebhookService::takeResult(result)) return;

  if (statusLabel == nullptr) return;
  setButtonsEnabled(true);
  const char *actionTitle = WebhookService::title(result.index);

  if (result.success) {
    lv_label_set_text_fmt(
      statusLabel,
      "Orden enviada correctamente: %s",
      actionTitle
    );
  } else if (result.httpCode == 0 && WiFi.status() != WL_CONNECTED) {
    lv_label_set_text(statusLabel, "La orden fallo porque se perdio el WiFi");
  } else {
    lv_label_set_text_fmt(
      statusLabel,
      "Fallo al ejecutar %s (HTTP %d)",
      actionTitle,
      result.httpCode
    );
  }
}
}
#endif