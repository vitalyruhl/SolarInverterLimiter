#include "shadow_core_runtime.h"

// [MOCKED!] Only the ESP-IDF NVS/partition and startup hardware boundaries.
static esp_err_t results[8];
static unsigned resultCount, calls, erases;
static const esp_partition_t partition{4096};

extern "C" void fakeNvsSet(const esp_err_t *values, unsigned count) {
    resultCount = count;
    calls = erases = 0;
    for (unsigned i = 0; i < count && i < 8; ++i) results[i] = values[i];
}
extern "C" unsigned fakeNvsCalls(void) { return calls; }
extern "C" unsigned fakePartitionErases(void) { return erases; }
extern "C" esp_err_t nvs_flash_init(void) {
    const unsigned index = calls++;
    return index < resultCount ? results[index] : ESP_OK;
}
extern "C" esp_err_t __real_nvs_flash_init(void) { return nvs_flash_init(); }
extern "C" esp_err_t nvs_flash_erase(void) { ++erases; return ESP_OK; }
extern "C" const esp_partition_t *esp_partition_find_first(int, int, const char *) { return &partition; }
extern "C" esp_err_t esp_partition_erase_range(const esp_partition_t *, size_t, size_t) { ++erases; return ESP_OK; }
extern "C" void esp_log_level_set(const char *, int) {}
extern "C" void init(void) {}
extern "C" void initVariant(void) {}
