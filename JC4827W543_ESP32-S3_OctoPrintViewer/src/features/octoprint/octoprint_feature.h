#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace OctoPrintFeature {
bool begin();
void startWorker();
void createTab(lv_obj_t *parent);
lv_coord_t createSettingsSection(lv_obj_t *parent, lv_coord_t startY);
void createOverlays();
void loop(bool tabActive);
void onTabActivated();
void onWifiConnectionChanged(bool connected);
void hideControls();
bool isFullscreen();
}