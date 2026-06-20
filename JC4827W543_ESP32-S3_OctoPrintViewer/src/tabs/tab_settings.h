#pragma once

#include <lvgl.h>

namespace SettingsTab {
void create(lv_obj_t *parent);
void createOverlay();
void beginWifi();
void loop();
}