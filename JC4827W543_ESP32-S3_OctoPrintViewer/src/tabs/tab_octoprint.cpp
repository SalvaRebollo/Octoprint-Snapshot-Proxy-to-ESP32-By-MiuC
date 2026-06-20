#include "../core/app_config.h"

#if APP_ENABLE_OCTOPRINT
#include "tab_octoprint.h"
#include "../features/octoprint/octoprint_feature.h"

namespace OctoPrintTab {
void create(lv_obj_t *parent) {
  OctoPrintFeature::createTab(parent);
}
}
#endif