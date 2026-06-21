#pragma once

#include <lvgl.h>

namespace DomoticaTab {
void create(lv_obj_t *parent);
void detachUi();
bool begin();
void loop();
}