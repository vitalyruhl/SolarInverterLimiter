#pragma once

#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
#include <nvs_flash.h>

// Returns the first NVS initialization error observed during this boot.
esp_err_t shadowNvsBootError();
#endif
