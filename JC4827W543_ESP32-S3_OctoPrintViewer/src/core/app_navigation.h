#pragma once

#include <lvgl.h>

enum class AppPage : uint8_t {
  COUNTER  = 0,
  DOMOTICA = 1,
  OCTOPRINT = 2,
  SETTINGS = 3,
  CLOCK    = 4  // kept after SETTINGS to preserve existing NVS values
};

void appShowPage(AppPage page, lv_anim_enable_t animation = LV_ANIM_ON);
void appApplyTabViewAppearance();
void appPreviewTabBarHeight(uint16_t height);
void appApplyPerformanceMonitorVisibility();
void appRebuildTabs();