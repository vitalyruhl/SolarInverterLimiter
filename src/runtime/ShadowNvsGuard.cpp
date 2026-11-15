#include "ShadowNvsGuard.h"

#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
#include <atomic>

namespace {
std::atomic<esp_err_t> firstNvsError{ESP_OK};
}

extern "C" esp_err_t __real_nvs_flash_init(void);

// The shadow linker wraps Core calls before setup(), including Arduino startup.
extern "C" __attribute__((noinline, used)) esp_err_t __wrap_nvs_flash_init(void) {
    const esp_err_t result = __real_nvs_flash_init();
    if (result != ESP_OK) {
        esp_err_t expected = ESP_OK;
        firstNvsError.compare_exchange_strong(expected, result, std::memory_order_relaxed);
    }
    // Arduino Core erases the entire NVS partition for these two return codes.
    return result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND
        ? ESP_FAIL : result;
}

esp_err_t shadowNvsBootError() {
    return firstNvsError.load(std::memory_order_relaxed);
}
#endif
