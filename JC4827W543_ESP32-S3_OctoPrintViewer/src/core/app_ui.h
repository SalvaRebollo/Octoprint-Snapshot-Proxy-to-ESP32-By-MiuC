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