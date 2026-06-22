#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#include "src/core/app_backlight.h"
#include "src/core/app_config.h"
#include "src/core/app_navigation.h"
#include "src/core/app_theme.h"
#include "src/tabs/clock/tab_clock.h"
#include "src/tabs/counter/tab_counter.h"
#include "src/tabs/settings/tab_settings.h"

#if APP_ENABLE_DOMOTICA
#include "src/tabs/domotica/tab_domotica.h"
#endif
#include "touch.h"

#if APP_ENABLE_OCTOPRINT
#include "src/tabs/octoprint/features/octoprint_feature.h"
#include "src/tabs/octoprint/tab_octoprint.h"
#endif

// ============================================================
// HARDWARE JC4827W543
// ============================================================

#define GFX_BL 1

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  45, 47, 21, 48, 40, 39
);

Arduino_GFX *panel = new Arduino_NV3041A(
  bus,
  GFX_NOT_DEFINED,
  0,
  true
);

Arduino_GFX *gfx = new Arduino_Canvas(
  APP_SCREEN_WIDTH,
  APP_SCREEN_HEIGHT,
  panel
);

// ============================================================
// APPLICATION CORE
// ============================================================

static lv_disp_draw_buf_t drawBuffer;
static lv_disp_drv_t displayDriver;
static lv_color_t *lvglBuffer = nullptr;
static bool displayFlushPending = false;
static lv_obj_t *tabView = nullptr;
static lv_obj_t *performanceMonitorLabel = nullptr;
static uint32_t lastPerformanceMonitorSearchMs = 0;

static int8_t counterTabIndex   = -1;
static int8_t domoticaTabIndex  = -1;
static int8_t octoprintTabIndex = -1;
static int8_t clockTabIndex     = -1;
static int8_t settingsTabIndex  = -1;

// Inactivity state (driven by lv_disp_get_inactive_time)
static bool inactivityDimmed      = false;
static bool inactivityClockActive = false;
static AppPage pageBeforeClock    = AppPage::SETTINGS;

void serviceUi() {
  lv_timer_handler();
  if (displayFlushPending) {
    displayFlushPending = false;
    gfx->flush();
  }
}

void appShowPage(AppPage page, lv_anim_enable_t animation) {
  if (tabView == nullptr) return;

  uint8_t target = settingsTabIndex >= 0 ? static_cast<uint8_t>(settingsTabIndex) : 0;
  switch (page) {
    case AppPage::COUNTER:
      if (counterTabIndex < 0) return;
      target = static_cast<uint8_t>(counterTabIndex);
      break;

    case AppPage::DOMOTICA:
      if (domoticaTabIndex < 0) return;
      target = static_cast<uint8_t>(domoticaTabIndex);
      break;

    case AppPage::OCTOPRINT:
      if (octoprintTabIndex < 0) return;
      target = static_cast<uint8_t>(octoprintTabIndex);
      break;

    case AppPage::CLOCK:
      if (clockTabIndex < 0) return;
      target = static_cast<uint8_t>(clockTabIndex);
      break;

    case AppPage::SETTINGS:
      if (settingsTabIndex < 0) return;
      target = static_cast<uint8_t>(settingsTabIndex);
      break;
  }

  lv_tabview_set_act(tabView, target, animation);
}

void appPreviewTabBarHeight(uint16_t height) {
  if (tabView == nullptr) return;

  lv_obj_t *tabButtons = lv_tabview_get_tab_btns(tabView);
  if (tabButtons == nullptr) return;

  lv_obj_set_height(tabButtons, height);
  lv_obj_update_layout(tabView);
}

void appApplyTabViewAppearance() {
  if (tabView == nullptr) return;

  lv_obj_t *tabButtons = lv_tabview_get_tab_btns(tabView);
  if (tabButtons == nullptr) return;

  lv_obj_set_style_text_color(
    tabButtons,
    AppTheme::isDarkMode() ? lv_color_white() : lv_color_black(),
    LV_PART_ITEMS | LV_STATE_CHECKED
  );
  appPreviewTabBarHeight(AppTheme::tabBarHeight());
}

void appApplyPerformanceMonitorVisibility() {
#if LV_USE_PERF_MONITOR && LV_USE_LABEL
  if (performanceMonitorLabel == nullptr) {
    lv_obj_t *systemLayer = lv_layer_sys();
    uint32_t childCount = lv_obj_get_child_cnt(systemLayer);
    for (uint32_t i = 0; i < childCount; i++) {
      lv_obj_t *child = lv_obj_get_child(systemLayer, i);
      if (!lv_obj_check_type(child, &lv_label_class)) continue;

      const char *text = lv_label_get_text(child);
      if (
        text != nullptr &&
        strstr(text, "FPS") != nullptr &&
        strstr(text, "CPU") != nullptr
      ) {
        performanceMonitorLabel = child;
        break;
      }
    }
  }

  if (performanceMonitorLabel != nullptr) {
    if (AppTheme::showPerformanceMonitor()) {
      lv_obj_clear_flag(performanceMonitorLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(performanceMonitorLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
#endif
}

void servicePerformanceMonitorSetting() {
  if (performanceMonitorLabel != nullptr) return;
  if (millis() - lastPerformanceMonitorSearchMs < 500) return;
  lastPerformanceMonitorSearchMs = millis();
  appApplyPerformanceMonitorVisibility();
}

bool isPageAvailable(AppPage page) {
  switch (page) {
    case AppPage::COUNTER:   return counterTabIndex >= 0;
    case AppPage::DOMOTICA:  return domoticaTabIndex >= 0;
    case AppPage::OCTOPRINT: return octoprintTabIndex >= 0;
    case AppPage::CLOCK:     return clockTabIndex >= 0;
    case AppPage::SETTINGS:  return settingsTabIndex >= 0;
  }
  return false;
}

// Returns the logical page that corresponds to the currently active tab index.
AppPage activePageForIndex(int activeIndex) {
  if (counterTabIndex   >= 0 && activeIndex == counterTabIndex)   return AppPage::COUNTER;
  if (domoticaTabIndex  >= 0 && activeIndex == domoticaTabIndex)  return AppPage::DOMOTICA;
  if (octoprintTabIndex >= 0 && activeIndex == octoprintTabIndex) return AppPage::OCTOPRINT;
  if (clockTabIndex     >= 0 && activeIndex == clockTabIndex)     return AppPage::CLOCK;
  return AppPage::SETTINGS;
}

/*
 * Determines the tab to open on startup: the last remembered tab if available;
 * otherwise the first visible tab that is not Settings; or Settings as a fallback.
 */
AppPage resolveStartupPage() {
  AppPage stored = AppTheme::lastActivePage();
  if (stored != AppPage::SETTINGS && isPageAvailable(stored)) return stored;

  if (counterTabIndex >= 0) return AppPage::COUNTER;
  if (domoticaTabIndex >= 0) return AppPage::DOMOTICA;
  if (octoprintTabIndex >= 0) return AppPage::OCTOPRINT;
  return AppPage::SETTINGS;
}

void onTabChanged(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

  int activeIndex = static_cast<int>(lv_tabview_get_tab_act(tabView));
  AppPage activePage = activePageForIndex(activeIndex);

  // Saves the active tab (AppTheme ignores Settings and Clock).
  AppTheme::setLastActivePage(activePage);

#if APP_ENABLE_OCTOPRINT
  OctoPrintFeature::hideControls();
  if (activeIndex == octoprintTabIndex) OctoPrintFeature::onTabActivated();
#endif

  if (clockTabIndex >= 0) {
    if (activeIndex == clockTabIndex) ClockTab::onTabActivated();
    else                              ClockTab::onTabDeactivated();
  }
}

void buildApplicationTabs() {
  if (tabView != nullptr) {
    CounterTab::detachUi();
    ClockTab::detachUi();
#if APP_ENABLE_OCTOPRINT
    OctoPrintFeature::detachTabUi();
#endif
#if APP_ENABLE_DOMOTICA
    DomoticaTab::detachUi();
#endif
    lv_obj_del(tabView);
    tabView = nullptr;
  }

  counterTabIndex   = -1;
  domoticaTabIndex  = -1;
  octoprintTabIndex = -1;
  clockTabIndex     = -1;
  settingsTabIndex  = -1;

  tabView = lv_tabview_create(
    lv_scr_act(),
    LV_DIR_TOP,
    AppTheme::tabBarHeight()
  );
  lv_obj_set_size(tabView, APP_SCREEN_WIDTH, APP_SCREEN_HEIGHT);
  lv_obj_center(tabView);
  lv_obj_add_event_cb(tabView, onTabChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  appApplyTabViewAppearance();

  uint8_t nextIndex = 0;

  if (AppTheme::showCounterTab()) {
    counterTabIndex = static_cast<int8_t>(nextIndex++);
    lv_obj_t *counterTab = lv_tabview_add_tab(tabView, "Contador");
    CounterTab::create(counterTab);
  }

#if APP_ENABLE_DOMOTICA
  if (AppTheme::showDomoticaTab()) {
    domoticaTabIndex = static_cast<int8_t>(nextIndex++);
    lv_obj_t *domoticaTab = lv_tabview_add_tab(tabView, "Domotica");
    DomoticaTab::create(domoticaTab);
  }
#endif

#if APP_ENABLE_OCTOPRINT
  if (AppTheme::showOctoPrintTab()) {
    octoprintTabIndex = static_cast<int8_t>(nextIndex++);
    lv_obj_t *octoprintTab = lv_tabview_add_tab(tabView, "OctoPrint");
    OctoPrintTab::create(octoprintTab);
  }
#endif

  if (AppTheme::showClockTab()) {
    clockTabIndex = static_cast<int8_t>(nextIndex++);
    lv_obj_t *clockTab = lv_tabview_add_tab(tabView, "Reloj");
    ClockTab::create(clockTab);
  }

  settingsTabIndex = static_cast<int8_t>(nextIndex++);
  lv_obj_t *settingsTab = lv_tabview_add_tab(tabView, "Ajustes");
  SettingsTab::create(settingsTab);
}

void appRebuildTabs() {
#if APP_ENABLE_OCTOPRINT
  OctoPrintFeature::hideControls();
#endif
  buildApplicationTabs();
  appShowPage(AppPage::SETTINGS, LV_ANIM_OFF);
}

void createApplicationUi() {
  lv_obj_clean(lv_scr_act());
  buildApplicationTabs();

#if APP_ENABLE_OCTOPRINT
  OctoPrintFeature::createOverlays();
#endif
  SettingsTab::createOverlay();

  appShowPage(resolveStartupPage(), LV_ANIM_OFF);
}
// ============================================================
// DRIVERS LVGL
// ============================================================

void myDisplayFlush(
  lv_disp_drv_t *display,
  const lv_area_t *area,
  lv_color_t *colors
) {
  uint32_t width = area->x2 - area->x1 + 1;
  uint32_t height = area->y2 - area->y1 + 1;

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(
    area->x1,
    area->y1,
    reinterpret_cast<uint16_t *>(&colors->full),
    width,
    height
  );
#else
  gfx->draw16bitRGBBitmap(
    area->x1,
    area->y1,
    reinterpret_cast<uint16_t *>(&colors->full),
    width,
    height
  );
#endif

  displayFlushPending = true;
  lv_disp_flush_ready(display);
}

void myTouchpadRead(lv_indev_drv_t *, lv_indev_data_t *data) {
  if (touch_has_signal() && touch_touched()) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch_last_x;
    data->point.y = touch_last_y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

bool initializeLvgl() {
  touch_init(gfx->width(), gfx->height(), gfx->getRotation());
  lv_init();

  uint32_t screenWidth = gfx->width();
  uint32_t screenHeight = gfx->height();
  uint32_t bufferSize = screenWidth * 40;

  lvglBuffer = static_cast<lv_color_t *>(
    heap_caps_malloc(
      sizeof(lv_color_t) * bufferSize,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    )
  );

  if (lvglBuffer == nullptr) {
    lvglBuffer = static_cast<lv_color_t *>(
      heap_caps_malloc(
        sizeof(lv_color_t) * bufferSize,
        MALLOC_CAP_8BIT
      )
    );
  }

  if (lvglBuffer == nullptr) {
    Serial.println("No se pudo reservar buffer LVGL");
    return false;
  }

  lv_disp_draw_buf_init(&drawBuffer, lvglBuffer, nullptr, bufferSize);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = screenWidth;
  displayDriver.ver_res = screenHeight;
  displayDriver.flush_cb = myDisplayFlush;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);
  AppTheme::begin();

  static lv_indev_drv_t inputDriver;
  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = myTouchpadRead;
  lv_indev_drv_register(&inputDriver);
  return true;
}

// ============================================================
// SETUP AND LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando plantilla JC4827W543...");

  if (!gfx->begin()) {
    Serial.println("gfx->begin() fallo");
  }

  gfx->fillScreen(RGB565_BLACK);

  if (!initializeLvgl()) return;

  // AppBacklight::begin() must come after AppTheme::begin() (called inside initializeLvgl).
  AppBacklight::begin();

#if APP_ENABLE_OCTOPRINT
  if (!OctoPrintFeature::begin()) {
    Serial.println("No se pudo iniciar la feature OctoPrint");
    return;
  }
#endif

  createApplicationUi();
  serviceUi();

#if APP_ENABLE_OCTOPRINT
  OctoPrintFeature::startWorker();
#endif
#if APP_ENABLE_DOMOTICA
  if (!DomoticaTab::begin()) {
    Serial.println("No se pudo iniciar el servicio de domotica");
  }
#endif
  SettingsTab::beginWifi();

  Serial.println("Setup terminado");
}

void handleInactivity() {
  uint32_t inactiveMs = lv_disp_get_inactive_time(nullptr);

  // Dim backlight after configured idle time.
  uint32_t dimSecs = AppTheme::dimTimeoutSecs();
  if (dimSecs > 0 && inactiveMs >= dimSecs * 1000u) {
    if (!inactivityDimmed) { inactivityDimmed = true; AppBacklight::dim(); }
  } else if (inactivityDimmed) {
    inactivityDimmed = false;
    AppBacklight::restore();
  }

  // Switch to clock tab after configured idle time (requires clock tab to be visible).
  uint32_t clockSecs = AppTheme::clockTimeoutSecs();
  if (clockSecs > 0 && clockTabIndex >= 0 && inactiveMs >= clockSecs * 1000u) {
    if (!inactivityClockActive) {
      inactivityClockActive = true;
      if (tabView != nullptr)
        pageBeforeClock = activePageForIndex(lv_tabview_get_tab_act(tabView));
      appShowPage(AppPage::CLOCK, LV_ANIM_OFF);
      appPreviewTabBarHeight(0); // fullscreen — no 5 s delay
      ClockTab::setInactivityMode(true);
    }
  } else if (inactivityClockActive) {
    // Activity detected: restore previous tab and tabbar.
    inactivityClockActive = false;
    ClockTab::setInactivityMode(false);
    appPreviewTabBarHeight(AppTheme::tabBarHeight());
    appShowPage(pageBeforeClock, LV_ANIM_OFF);
  }
}

void loop() {
  serviceUi();
  servicePerformanceMonitorSetting();
  handleInactivity();
  SettingsTab::loop();

#if APP_ENABLE_DOMOTICA
  DomoticaTab::loop();
#endif

#if APP_ENABLE_OCTOPRINT
  bool octoprintActive =
    tabView != nullptr &&
    octoprintTabIndex >= 0 &&
    lv_tabview_get_tab_act(tabView) == octoprintTabIndex;
  OctoPrintFeature::loop(octoprintActive);
#endif

  delay(5);
}