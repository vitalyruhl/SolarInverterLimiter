#include "runtime/RuntimeProgress.h"
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    // [MOCKED!] RTC storage and uptime; the actual progress tracker is exercised.
    sil::ProgressRecord rtc{};
    sil::RuntimeProgress first(rtc);
    const auto cold = first.begin(10);
    assert(!cold.valid && rtc.bootCount == 1);
    first.enter(sil::RuntimeStage::Mqtt, 100);
    first.enter(sil::RuntimeStage::Control, 150);
    first.controlCompleted(160);
    first.loop(200);
    first.loop(230);
    assert(first.maxStageMs() == 50 && first.maxLoopGapMs() == 30);
    assert(first.peakStage() == sil::RuntimeStage::Mqtt && first.peakStageEndedAtMs() == 150);
    assert(first.controlAgeMs(230) == 70 && first.loops() == 2);
    first.enter(sil::RuntimeStage::Mqtt, 240);
    // A reset during a call must preserve the last entered stage without an END.
    sil::RuntimeProgress rebooted(rtc);
    const auto previous = rebooted.begin(5);
    assert(previous.valid && previous.stage == sil::RuntimeStage::Mqtt);
    assert(previous.stageAtMs == 240 && previous.controlAtMs == 160);
    assert(rtc.bootCount == 2 && rtc.stage == sil::RuntimeStage::Boot);
    assert(!rebooted.reportDue(4999) && rebooted.reportDue(5005));
    assert(!rebooted.reportDue(5006));
    rebooted.enter(sil::RuntimeStage::Publish, UINT32_MAX - 10);
    rebooted.enter(sil::RuntimeStage::Control, 15);
    assert(rebooted.maxStageMs() == 26);
    rtc.complement ^= 1;
    sil::RuntimeProgress corrupted(rtc);
    assert(!corrupted.begin(1).valid && rtc.bootCount == 1);
    static_assert(sizeof(sil::ProgressRecord) == 28, "RTC layout must remain unchanged");
    sil::RuntimeProgress slow(rtc);
    slow.begin(0, false);
    slow.enter(sil::RuntimeStage::Wifi, 10);
    slow.enter(sil::RuntimeStage::Mqtt, 6100);
    slow.enter(sil::RuntimeStage::Publish, 8099);
    assert(slow.maxStageMs() == 6090 && slow.peakStageEndedAtMs() == 6100);
    assert(slow.lastSlowStage() == sil::RuntimeStage::Mqtt);
    assert(slow.lastSlowStageMs() == 1999 && slow.lastSlowStageEndedAtMs() == 8099);
    slow.enter(sil::RuntimeStage::Control, 8598); // Below the inclusive 500 ms threshold.
    assert(slow.lastSlowStage() == sil::RuntimeStage::Mqtt);
    slow.enter(sil::RuntimeStage::Sensors, 9098);
    assert(slow.lastSlowStage() == sil::RuntimeStage::Control && slow.lastSlowStageMs() == 500);
    slow.loop(10000); slow.loop(16000); slow.loop(18000); slow.loop(18499);
    assert(slow.maxLoopGapMs() == 6000 && slow.lastSlowLoopMs() == 2000);
    assert(slow.lastSlowLoopEndedAtMs() == 18000);
    slow.loop(18999);
    assert(slow.lastSlowLoopMs() == 500 && slow.lastSlowLoopEndedAtMs() == 18999);
    slow.begin(UINT32_MAX - 600, false);
    assert(slow.maxStageMs() == 0 && slow.peakStage() == sil::RuntimeStage::Boot);
    assert(slow.peakStageEndedAtMs() == 0 && slow.lastSlowStageMs() == 0 && slow.lastSlowLoopMs() == 0);
    assert(slow.lastSlowStageEndedAtMs() == 0 && slow.lastSlowLoopEndedAtMs() == 0 && slow.loops() == 0);
    slow.enter(sil::RuntimeStage::Mqtt, UINT32_MAX - 550);
    slow.enter(sil::RuntimeStage::Control, 49);
    slow.loop(UINT32_MAX - 550); slow.loop(49);
    assert(slow.lastSlowStageMs() == 600 && slow.lastSlowStageEndedAtMs() == 49);
    assert(slow.lastSlowLoopMs() == 600 && slow.lastSlowLoopEndedAtMs() == 49);
    slow.controlCompleted(UINT32_MAX - 10);
    assert(slow.controlAgeMs(15) == 26);
    slow.begin(0); // A warm boot preserves RTC history, not current-boot slow observations.
    assert(slow.lastSlowStage() == sil::RuntimeStage::Boot && slow.lastSlowStageMs() == 0);
    assert(slow.lastSlowLoopMs() == 0 && slow.peakStageEndedAtMs() == 0);
    std::cout << "[MOCKED!] Runtime persistence, slow observations, threshold, reset and rollover passed\n";
}
