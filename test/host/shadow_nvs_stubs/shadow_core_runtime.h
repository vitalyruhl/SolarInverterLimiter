#pragma once
#include <stddef.h>
#include "nvs_flash.h"

#define CONFIG_LOG_DEFAULT_LEVEL 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
#define log_e(...) ((void)0)

typedef struct esp_partition_t { size_t size; } esp_partition_t;
#ifdef __cplusplus
extern "C" {
#endif
void esp_log_level_set(const char *, int);
const esp_partition_t *esp_partition_find_first(int, int, const char *);
esp_err_t esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
void init(void);
void initVariant(void);
void initArduino(void);
void fakeNvsSet(const esp_err_t *, unsigned);
unsigned fakeNvsCalls(void);
unsigned fakePartitionErases(void);
#ifdef __cplusplus
}
#endif
