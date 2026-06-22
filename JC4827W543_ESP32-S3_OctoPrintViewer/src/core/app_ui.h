#pragma once

#include <lvgl.h>

lv_obj_t *appCreateButton(
  lv_obj_t *parent,
  const char *text,
  lv_coord_t x,
  lv_coord_t y,
  lv_coord_t width,
  lv_coord_t height,
  lv_event_cb_t callback,
  void *userData = nullptr
);

// Invisible object that reserves space (e.g. bottom padding in scrollable content).
// No background or border.
lv_obj_t *appCreateSpacer(
  lv_obj_t *parent,
  lv_coord_t x,
  lv_coord_t y,
  lv_coord_t width,
  lv_coord_t height
);