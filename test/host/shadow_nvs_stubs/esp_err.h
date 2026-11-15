#pragma once
// [MOCKED!] ESP-IDF NVS error codes as defined by the selected ESP32 SDK.
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NVS_INVALID_STATE 0x110b
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
