#include "tab_counter.h"

#include "../core/app_ui.h"

namespace CounterTab {
namespace {
lv_obj_t *counterLabel = nullptr;
int counterValue = 0;

void onCounterClick(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  counterValue++;
  lv_label_set_text_fmt(counterLabel, "%d", counterValue);
}
}

void create(lv_obj_t *parent) {
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(parent);
  lv_label_set_text(title, "Mi primer contador");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  counterLabel = lv_label_create(parent);
  lv_label_set_text(counterLabel, "0");
  lv_obj_align(counterLabel, LV_ALIGN_CENTER, 0, -25);

  appCreateButton(parent, "SUMAR", 135, 125, 180, 65, onCounterClick);
}
}