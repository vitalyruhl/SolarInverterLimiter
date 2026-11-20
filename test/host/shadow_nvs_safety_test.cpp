#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>

// [MOCKED!] Hardware, Preferences side effects and the WiFi boundary only.
// The actual SIL NVS, setup, loop and IO-registration code is extracted by
// run-shadow-nvs-safety-tests.ps1 and compiled below for both profiles.
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NVS_NO_FREE_PAGES = 1;
constexpr esp_err_t ESP_ERR_NVS_NEW_VERSION_FOUND = 2;
constexpr esp_err_t ESP_ERR_NVS_INVALID_STATE = 3;
constexpr esp_err_t OTHER_NVS_ERROR = 4;

static esp_err_t firstInit = ESP_OK, secondInit = ESP_OK, eraseResult = ESP_OK;
static unsigned initCalls = 0, eraseCalls = 0, setupLoggingCalls = 0;
static unsigned wifiUpdateCalls = 0, runtimeLoopCalls = 0;

static esp_err_t nvs_flash_init() {
    return initCalls++ == 0 ? firstInit : secondInit;
}
[[maybe_unused]] static esp_err_t nvs_flash_erase() {
    ++eraseCalls;
    return eraseResult;
}

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

static void setupLogging() { ++setupLoggingCalls; }
static void delay(unsigned) {}
static void analogReadResolution(int) {}
static constexpr char VERSION[] = "host-fixture";
static constexpr char IO_RESET_ID[] = "reset_btn";
static constexpr char IO_AP_ID[] = "ap_btn";
static constexpr char APMODE_SSID[] = "test-ap";
static constexpr char APMODE_PASSWORD[] = "test-password";
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
static constexpr bool kRs485MockTransport = true;
// This focused SIL-only test starts without an earlier Core initialization error.
static esp_err_t shadowNvsBootError() { return ESP_OK; }
#else
static constexpr bool kRs485MockTransport = false;
#endif

enum class LL { Warn };
static struct LogBoundary {
    void scopedTag(const char *) {}
    template <class... A> void logTag(LL, const char *, const char *, A...) {}
} lmg;

static unsigned clearCalls = 0, saveCalls = 0, restartCalls = 0;
static unsigned detachCalls = 0;
static bool detachedBeforeClear = false;
[[maybe_unused]] static struct TelemetryBoundary { void detach() { ++detachCalls; } } telemetryAge;
static struct ConfigBoundary {
    struct WiFiBoundary { void update() { ++wifiUpdateCalls; } } wifi;
    WiFiBoundary &getWiFiManager() { return wifi; }
    void clearAllFromPrefs() {
        detachedBeforeClear = detachCalls == 1;
        ++clearCalls;
    }
    void saveAll() { ++saveCalls; }
    void startAccessPoint(const char *, const char *) {}
} ConfigManager;
static struct EspBoundary { void restart() { ++restartCalls; } } ESP;

namespace cm {
struct IOManager {
    struct DigitalInputEventOptions { int longClickMs = 0; };
    struct DigitalInputEventCallbacks {
        std::function<void()> onPress;
        std::function<void()> onLongPressOnStartup;
    };
};
} // namespace cm
static struct IoBoundary {
    std::function<void()> resetOnStartup;
    void addDigitalInput(const char *, const char *, int, bool, bool, bool, bool) {}
    void addDigitalInputToSettingsGroup(const char *, const char *, const char *, const char *, int) {}
    void configureDigitalInputEvents(const char *id, cm::IOManager::DigitalInputEventCallbacks callbacks,
                                     const cm::IOManager::DigitalInputEventOptions &) {
        if (!std::strcmp(id, IO_RESET_ID)) resetOnStartup = callbacks.onLongPressOnStartup;
    }
} ioManager;

#include "shadow_nvs_production.h"

static unsigned failures = 0;
static void check(bool condition, const char *description) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++failures;
}
static void setNvs(esp_err_t first, esp_err_t second = ESP_OK, esp_err_t erase = ESP_OK) {
    firstInit = first; secondInit = second; eraseResult = erase;
    initCalls = eraseCalls = 0;
}

static void checkInitialization() {
    setNvs(ESP_OK);
    const bool ready = ensureNvsReady();
    check(ready && initCalls == 1 && eraseCalls == 0, "successful NVS init does not erase");
    const esp_err_t errors[] = {ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND,
                                ESP_ERR_NVS_INVALID_STATE, OTHER_NVS_ERROR};
    const char *names[] = {"NO_FREE_PAGES", "NEW_VERSION_FOUND", "INVALID_STATE", "OTHER_ERROR"};
    for (unsigned i = 0; i < 4; ++i) {
        setNvs(errors[i]);
        const bool actual = ensureNvsReady();
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
        const bool ok = !actual && initCalls == 1 && eraseCalls == 0;
#else
        const bool recoverable = i < 3;
        const bool ok = actual == recoverable && initCalls == (recoverable ? 2u : 1u) &&
                        eraseCalls == (recoverable ? 1u : 0u);
#endif
        std::printf("%s %s: ready=%d init=%u erase=%u\n", ok ? "PASS" : "FAIL", names[i],
                    actual, initCalls, eraseCalls);
        if (!ok) ++failures;
    }
    setNvs(ESP_ERR_NVS_NO_FREE_PAGES, OTHER_NVS_ERROR);
    const bool failedRetry = ensureNvsReady();
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
    check(!failedRetry && initCalls == 1 && eraseCalls == 0,
          "shadow does not erase or retry after an NVS error");
#else
    check(!failedRetry && initCalls == 2 && eraseCalls == 1,
          "normal profile reports failure when initialization after erase fails");
#endif
}

static void checkSuccessfulStartup() {
    setNvs(ESP_OK);
    setupLoggingCalls = wifiUpdateCalls = runtimeLoopCalls = 0;
    setup();
    loop();
    check(initCalls == 1 && eraseCalls == 0 && setupLoggingCalls == 1 &&
              wifiUpdateCalls == 1 && runtimeLoopCalls == 1,
          "successful NVS permits normal setup and WiFi loop");
}

static void checkFactoryReset() {
    detachCalls = clearCalls = saveCalls = restartCalls = 0;
    detachedBeforeClear = false;
    ioManager.resetOnStartup = {};
    registerIOBindings();
    check(bool(ioManager.resetOnStartup), "reset button has a startup callback");
    if (ioManager.resetOnStartup) ioManager.resetOnStartup();
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
    check(detachCalls == 0 && !detachedBeforeClear && clearCalls == 0 && saveCalls == 0 && restartCalls == 0,
          "shadow reset button preserves telemetry and settings");
#else
    check(detachCalls == 1 && detachedBeforeClear && clearCalls == 1 && saveCalls == 1 && restartCalls == 1,
          "normal reset button detaches telemetry before restoring defaults");
#endif
}

static void checkFailedStartup() {
    const esp_err_t errors[] = {ESP_ERR_NVS_NO_FREE_PAGES, ESP_ERR_NVS_NEW_VERSION_FOUND,
                                ESP_ERR_NVS_INVALID_STATE, OTHER_NVS_ERROR};
    const char *names[] = {"NO_FREE_PAGES", "NEW_VERSION_FOUND", "INVALID_STATE", "OTHER_ERROR"};
    for (unsigned i = 0; i < 4; ++i) {
        // Each iteration models a fresh boot, including zero-initialized state.
        shadowStartupBlocked = false;
        setNvs(errors[i], ESP_OK, OTHER_NVS_ERROR);
        setupLoggingCalls = wifiUpdateCalls = runtimeLoopCalls = 0;
        setup();
        loop();
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
        const bool ok = eraseCalls == 0 && setupLoggingCalls == 0 && wifiUpdateCalls == 0 && runtimeLoopCalls == 0;
#else
        const bool ok = eraseCalls == (i < 3 ? 1u : 0u) && setupLoggingCalls == 1 && wifiUpdateCalls == 1;
#endif
        std::printf("%s %s startup: erase=%u setup=%u wifi=%u loop=%u\n", ok ? "PASS" : "FAIL", names[i],
                    eraseCalls, setupLoggingCalls, wifiUpdateCalls, runtimeLoopCalls);
        if (!ok) ++failures;
    }
}

int main() {
    checkInitialization();
    checkSuccessfulStartup();
    checkFactoryReset();
    checkFailedStartup();
    std::printf("[MOCKED!] NVS startup safety: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
