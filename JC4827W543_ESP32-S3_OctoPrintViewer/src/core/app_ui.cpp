#include "app_ui.h"

lv_obj_t *appCreateButton(
  lv_obj_t *parent,
  const char *text,
  lv_coord_t x,
  lv_coord_t y,
  lv_coord_t width,
  lv_coord_t height,
  lv_event_cb_t callback,
  void *userData
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

lv_obj_t *appCreateSpacer(
  lv_obj_t *parent,
  lv_coord_t x,
  lv_coord_t y,
  lv_coord_t width,
  lv_coord_t height
) {
  lv_obj_t *spacer = lv_obj_create(parent);
  lv_obj_set_pos(spacer, x, y);
  lv_obj_set_size(spacer, width, height);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
  return spacer;
}