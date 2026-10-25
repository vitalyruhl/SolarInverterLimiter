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
    std::cout << "[MOCKED!] Runtime stage persistence, reset, report cadence and rollover passed\n";
}
