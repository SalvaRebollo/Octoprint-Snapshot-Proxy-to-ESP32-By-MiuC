#pragma once

#include <lvgl.h>

namespace ClockTab {

void create(lv_obj_t *parent);
void detachUi();

// Called by .ino's onTabChanged when this tab becomes active/inactive.
void onTabActivated();
void onTabDeactivated();

// Tells the tab that the .ino switched to it due to inactivity (not user).
// In inactivity mode the tabbar is hidden immediately; .ino restores it on touch.
void setInactivityMode(bool active);

}
