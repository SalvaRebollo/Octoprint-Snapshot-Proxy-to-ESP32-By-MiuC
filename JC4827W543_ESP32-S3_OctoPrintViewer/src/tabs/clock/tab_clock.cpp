#include "tab_clock.h"

#include <Arduino.h>  // provides getLocalTime() for ESP32
#include <time.h>

#include "../../core/app_navigation.h"
#include "../../core/app_theme.h"

// Require large fonts for the clock face. Enable them in lv_conf.h:
  #define LV_FONT_MONTSERRAT_48  1
  #define LV_FONT_MONTSERRAT_20  1
#if LV_FONT_MONTSERRAT_48
#define CLOCK_FONT_TIME (&lv_font_montserrat_48)
#else
#define CLOCK_FONT_TIME (LV_FONT_DEFAULT)
#endif

#if LV_FONT_MONTSERRAT_20
#define CLOCK_FONT_DATE (&lv_font_montserrat_20)
#else
#define CLOCK_FONT_DATE (LV_FONT_DEFAULT)
#endif

namespace ClockTab {
namespace {

lv_obj_t   *timeLabel       = nullptr;
lv_obj_t   *dateLabel       = nullptr;
lv_timer_t *clockTimer      = nullptr;
lv_timer_t *hideTabbarTimer = nullptr;

bool tabbarHidden  = false;
bool inactivityMode = false;

void syncTabbarState(bool hidden) {
  if (tabbarHidden == hidden) return;
  tabbarHidden = hidden;
  appPreviewTabBarHeight(hidden ? 0 : AppTheme::tabBarHeight());
  // Re-center labels when the available height changes.
  if (timeLabel != nullptr) lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -18);
  if (dateLabel != nullptr) lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0,  40);
}

void onHideTabbarTimer(lv_timer_t *) {
  // Timer is auto-deleted by LVGL after this callback (repeat_count = 1).
  hideTabbarTimer = nullptr;
  syncTabbarState(true);
}

void startHideTimer() {
  if (hideTabbarTimer != nullptr) {
    lv_timer_del(hideTabbarTimer);
    hideTabbarTimer = nullptr;
  }
  hideTabbarTimer = lv_timer_create(onHideTabbarTimer, 5000, nullptr);
  lv_timer_set_repeat_count(hideTabbarTimer, 1);
}

// User touched the clock screen while in manual mode (not inactivity).
void onClockTouch(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (inactivityMode) return; // .ino owns restoration in inactivity mode
  if (tabbarHidden) {
    syncTabbarState(false);
    startHideTimer(); // restart 5 s countdown
  }
}

void onClockTimer(lv_timer_t *) {
  struct tm info = {};
  bool ok = getLocalTime(&info, 0);

  if (timeLabel != nullptr) {
    if (ok) lv_label_set_text_fmt(timeLabel, "%02d:%02d:%02d",
                                  info.tm_hour, info.tm_min, info.tm_sec);
    else    lv_label_set_text(timeLabel, "--:--:--");
  }

  if (dateLabel != nullptr) {
    if (ok) lv_label_set_text_fmt(dateLabel, "%02d/%02d/%04d",
                                  info.tm_mday, info.tm_mon + 1, info.tm_year + 1900);
    else    lv_label_set_text(dateLabel, "--/--/----");
  }
}

} // namespace (anonymous)

void create(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(parent, onClockTouch, LV_EVENT_CLICKED, nullptr);

  timeLabel = lv_label_create(parent);
  lv_label_set_text(timeLabel, "--:--:--");
  lv_obj_set_style_text_color(timeLabel, lv_color_hex(0x787878), LV_PART_MAIN);
  lv_obj_set_style_text_font(timeLabel, CLOCK_FONT_TIME, LV_PART_MAIN);
  lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -18);

  dateLabel = lv_label_create(parent);
  lv_label_set_text(dateLabel, "--/--/----");
  lv_obj_set_style_text_color(dateLabel, lv_color_hex(0x4A4A4A), LV_PART_MAIN);
  lv_obj_set_style_text_font(dateLabel, CLOCK_FONT_DATE, LV_PART_MAIN);
  lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 40);

  // Populate immediately so there's no blank frame on first render.
  onClockTimer(nullptr);
  clockTimer = lv_timer_create(onClockTimer, 500, nullptr);
}

void detachUi() {
  if (clockTimer != nullptr) {
    lv_timer_del(clockTimer);
    clockTimer = nullptr;
  }
  if (hideTabbarTimer != nullptr) {
    lv_timer_del(hideTabbarTimer);
    hideTabbarTimer = nullptr;
  }
  timeLabel      = nullptr;
  dateLabel      = nullptr;
  tabbarHidden   = false;
  inactivityMode = false;
}

void onTabActivated() {
  if (!inactivityMode) {
    startHideTimer(); // start 5 s countdown to hide tabbar
  }
}

void onTabDeactivated() {
  if (hideTabbarTimer != nullptr) {
    lv_timer_del(hideTabbarTimer);
    hideTabbarTimer = nullptr;
  }
  syncTabbarState(false); // always restore tabbar when leaving clock
  inactivityMode = false;
}

void setInactivityMode(bool active) {
  inactivityMode = active;
  if (active) {
    // .ino already hid the tabbar; cancel the 5 s timer to avoid conflicts.
    if (hideTabbarTimer != nullptr) {
      lv_timer_del(hideTabbarTimer);
      hideTabbarTimer = nullptr;
    }
    tabbarHidden = true; // sync local state
  }
}

} // namespace ClockTab
