#include "shadow_core_runtime.h"
#include <cstdio>
#include <cstring>

// [MOCKED!] Hardware and storage boundaries only. The selected Arduino Core
// initArduino() and SIL startup functions are compiled from their real source.
static unsigned setupLoggingCalls, wifiUpdateCalls, runtimeLoopCalls;
static struct SerialBoundary {
    void begin(unsigned) {}
    template <class... A> void printf(const char *, A...) {}
    template <class T> void println(T) {}
} Serial;
namespace sil { enum class RuntimeStage { Wifi }; }
static struct RuntimeBoundary {
    void begin(const char *, bool) {}
    void loop() { ++runtimeLoopCalls; }
    void enter(sil::RuntimeStage) {}
} runtimeDiagnostics;
static struct ConfigBoundary {
    struct WiFiBoundary { void update() { ++wifiUpdateCalls; } } wifi;
    WiFiBoundary &getWiFiManager() { return wifi; }
} ConfigManager;
static void setupLogging() { ++setupLoggingCalls; }
[[maybe_unused]] static void delay(unsigned) {}
static constexpr char VERSION[] = "host-fixture";
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
static constexpr bool kRs485MockTransport = true;
esp_err_t shadowNvsBootError();
#else
static constexpr bool kRs485MockTransport = false;
#endif
extern "C" esp_err_t nvs_flash_erase(void);

#include "shadow_startup_actual.h"

static unsigned failures;
static void check(bool condition, const char *description) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++failures;
}
static void runStartup() { setup(); loop(); }
static void checkStarted() {
    check(setupLoggingCalls == 1 && runtimeLoopCalls == 1 && wifiUpdateCalls == 1,
          "qualified boot reaches normal setup and WiFi loop");
}
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
static void checkBlocked() {
    check(setupLoggingCalls == 0 && runtimeLoopCalls == 0 && wifiUpdateCalls == 0,
          "unqualified boot blocks normal setup and WiFi loop");
}
#endif

static void successfulBoot() {
    const esp_err_t events[] = {ESP_OK, ESP_OK};
    fakeNvsSet(events, 2);
    initArduino();
    runStartup();
    check(fakeNvsCalls() == 2 && fakePartitionErases() == 0, "successful core and SIL NVS calls do not erase");
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
    check(shadowNvsBootError() == ESP_OK, "successful boot has no latched error");
#endif
    checkStarted();
}

static void earlyFault(esp_err_t fault, bool recoverable) {
    // Without the shadow guard the core erases on recoverable errors, then the
    // fake second initialization succeeds; this is the actual RED behavior.
    const esp_err_t events[] = {fault, ESP_OK, ESP_OK};
    fakeNvsSet(events, 3);
    initArduino();
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
    check(fakePartitionErases() == 0, "core did not erase NVS after early error");
    check(shadowNvsBootError() == fault, "early original error is latched");
    runStartup();
    check(fakeNvsCalls() == 1, "SIL setup does not retry after an early core error");
    checkBlocked();
#else
    runStartup();
    check(fakePartitionErases() == (recoverable ? 1u : 0u), "normal core retains existing erase behavior");
    check(fakeNvsCalls() == (recoverable ? 3u : 2u), "normal core/SIL retain initialization sequence");
    checkStarted();
#endif
    (void)recoverable;
}

#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
static void laterFault() {
    const esp_err_t events[] = {ESP_OK, ESP_ERR_NVS_NO_FREE_PAGES, ESP_OK};
    fakeNvsSet(events, 3);
    initArduino();
    runStartup();
    check(fakeNvsCalls() == 2 && fakePartitionErases() == 0,
          "SIL-stage error after successful core init does not erase or retry");
    check(shadowNvsBootError() == ESP_ERR_NVS_NO_FREE_PAGES, "later error preserves its original code");
    checkBlocked();
}

static void firstErrorRemainsLatched() {
    constexpr esp_err_t other = 0x1234;
    const esp_err_t events[] = {other, ESP_ERR_NVS_INVALID_STATE, ESP_ERR_NVS_NO_FREE_PAGES,
                                ESP_ERR_NVS_NEW_VERSION_FOUND, ESP_OK};
    fakeNvsSet(events, 5);
    initArduino();
    check(shadowNvsBootError() == other && fakePartitionErases() == 0, "first generic core error is latched");
    check(nvs_flash_init() == ESP_ERR_NVS_INVALID_STATE, "other NVS error passes through unchanged");
    check(shadowNvsBootError() == other, "second error cannot replace first error");
    check(nvs_flash_init() == ESP_FAIL, "NO_FREE_PAGES maps to ESP_FAIL");
    check(nvs_flash_init() == ESP_FAIL, "NEW_VERSION_FOUND maps to ESP_FAIL");
    check(nvs_flash_init() == ESP_OK, "later successful initialization remains ESP_OK");
    check(shadowNvsBootError() == other, "later errors and success preserve first error");
    runStartup();
    check(fakeNvsCalls() == 5 && fakePartitionErases() == 0, "latched error blocks SIL without another init");
    checkBlocked();
}
#endif

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (!std::strcmp(argv[1], "success")) successfulBoot();
    else if (!std::strcmp(argv[1], "no-free")) earlyFault(ESP_ERR_NVS_NO_FREE_PAGES, true);
    else if (!std::strcmp(argv[1], "new-version")) earlyFault(ESP_ERR_NVS_NEW_VERSION_FOUND, true);
    else if (!std::strcmp(argv[1], "other")) earlyFault(0x1234, false);
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
    else if (!std::strcmp(argv[1], "later")) laterFault();
    else if (!std::strcmp(argv[1], "latch")) firstErrorRemainsLatched();
#endif
    else return 2;
    std::printf("[MOCKED!] %s: %u failure(s)\n", argv[1], failures);
    return failures ? 1 : 0;
}
