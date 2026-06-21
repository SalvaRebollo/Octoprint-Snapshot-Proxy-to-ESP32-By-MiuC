#pragma once

#include <lvgl.h>

namespace OctoPrintFeature {
bool begin();
void startWorker();
void createTab(lv_obj_t *parent);
void createOverlays();
void loop(bool tabActive);
void onTabActivated();
void onWifiConnectionChanged(bool connected);
void hideControls();
bool isFullscreen();
}