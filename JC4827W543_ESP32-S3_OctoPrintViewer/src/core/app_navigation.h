#pragma once

#include <lvgl.h>

enum class AppPage : uint8_t {
  COUNTER,
  DOMOTICA,
  OCTOPRINT,
  SETTINGS
};

void appShowPage(AppPage page, lv_anim_enable_t animation = LV_ANIM_ON);
void appApplyTabViewAppearance();
void appPreviewTabBarHeight(uint16_t height);
void appApplyPerformanceMonitorVisibility();
void appRebuildTabs();