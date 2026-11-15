#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t nvs_flash_init(void);
esp_err_t __real_nvs_flash_init(void);
#ifdef __cplusplus
}
#endif
