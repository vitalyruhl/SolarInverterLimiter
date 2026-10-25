#pragma once
#include "RuntimeProgress.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace sil {
static RTC_NOINIT_ATTR ProgressRecord retainedProgress;
class RuntimeDiagnostics {
public:
    RuntimeDiagnostics() : progress_(retainedProgress) {}
    void begin(const char *version, bool mock) {
        reset_ = uint32_t(esp_reset_reason());
        previous_ = progress_.begin(millis(), reset_ != ESP_RST_POWERON && reset_ != ESP_RST_BROWNOUT);
        Serial.printf("[I] DIAG boot version=%s mac=%s mock=%u reset=%lu boot=%lu prev=%s prev_ms=%lu prev_control_ms=%lu\n",
            version, WiFi.macAddress().c_str(), unsigned(mock), ulong(reset_), ulong(retainedProgress.bootCount),
            previous_.valid ? stageName(previous_.stage) : "unknown", ulong(previous_.stageAtMs), ulong(previous_.controlAtMs));
    }
    void loop() { progress_.loop(millis()); }
    void enter(RuntimeStage stage) { progress_.enter(stage, millis()); }
    void controlCompleted() { progress_.controlCompleted(millis()); }
    template<class Stats> void report(const Stats &stats, uint32_t priceRevision, uint32_t pvRevision) {
        const uint32_t now = millis();
        if (!progress_.reportDue(now)) return;
        const IPAddress ip = WiFi.localIP();
        Serial.printf("[I] DIAG ms=%lu boot=%lu reset=%lu ip=%u.%u.%u.%u gap=%lu peak=%s stage_ms=%lu control_age=%lu heap=%u min=%u block=%u stack=%u integrity=%u rx_to=%lu rx_bad=%lu rx_big=%lu tx_fail=%lu conn_fail=%lu price_rev=%lu pv_rev=%lu\n",
            ulong(now), ulong(retainedProgress.bootCount), ulong(reset_), unsigned(ip[0]), unsigned(ip[1]), unsigned(ip[2]), unsigned(ip[3]), ulong(progress_.maxLoopGapMs()),
            stageName(progress_.peakStage()), ulong(progress_.maxStageMs()), ulong(now - retainedProgress.controlAtMs),
            unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()), unsigned(ESP.getMaxAllocHeap()),
            unsigned(uxTaskGetStackHighWaterMark(nullptr)), unsigned(heap_caps_check_integrity_all(false)),
            ulong(stats.rxTimeouts), ulong(stats.rxMalformed), ulong(stats.rxOversize), ulong(stats.txFailures),
            ulong(stats.connectFailures), ulong(priceRevision), ulong(pvRevision));
    }
    String mqttPayload() const {
        char out[224];
        snprintf(out, sizeof(out), "{\"boot\":%lu,\"resetReason\":%lu,\"previousStage\":\"%s\",\"previousStageMs\":%lu,\"maxLoopMs\":%lu,\"maxStageMs\":%lu}",
            ulong(retainedProgress.bootCount), ulong(reset_), previous_.valid ? stageName(previous_.stage) : "unknown",
            ulong(previous_.stageAtMs), ulong(progress_.maxLoopGapMs()), ulong(progress_.maxStageMs()));
        return String(out);
    }
private:
    static unsigned long ulong(uint32_t value) { return static_cast<unsigned long>(value); }
    RuntimeProgress progress_;
    PreviousProgress previous_;
    uint32_t reset_ = 0;
};
} // namespace sil
