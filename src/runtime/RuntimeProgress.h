#pragma once
#include <cstdint>

namespace sil {
enum class RuntimeStage : uint32_t { Boot, Wifi, Mqtt, Sbt, Publish, Control, Sensors, Config, Idle };
struct ProgressRecord {
    uint32_t magic, complement, bootCount;
    RuntimeStage stage;
    uint32_t stageAtMs, controlAtMs, loops;
};
struct PreviousProgress {
    bool valid = false;
    RuntimeStage stage = RuntimeStage::Boot;
    uint32_t stageAtMs = 0, controlAtMs = 0;
};
// Volatile RTC writes remain observable even with LTO and a reset inside the next call.
class RuntimeProgress {
public:
    explicit RuntimeProgress(volatile ProgressRecord &record) : record_(record) {}
    PreviousProgress begin(uint32_t now, bool preserve = true) {
        PreviousProgress previous;
        previous.valid = preserve && record_.magic == kMagic && record_.complement == ~kMagic &&
            uint32_t(record_.stage) <= uint32_t(RuntimeStage::Idle);
        uint32_t boots = 1;
        if (previous.valid) {
            previous.stage = record_.stage;
            previous.stageAtMs = record_.stageAtMs;
            previous.controlAtMs = record_.controlAtMs;
            boots = record_.bootCount + 1;
            if (!boots) boots = 1;
        }
        record_.magic = kMagic; record_.complement = ~kMagic; record_.bootCount = boots;
        record_.stage = RuntimeStage::Boot; record_.stageAtMs = now;
        record_.controlAtMs = 0; record_.loops = 0;
        lastReport_ = now; maxStageMs_ = maxLoopGapMs_ = lastLoop_ = 0; loopSeen_ = false;
        return previous;
    }
    void enter(RuntimeStage next, uint32_t now) {
        const uint32_t elapsed = now - record_.stageAtMs;
        if (record_.stage != RuntimeStage::Boot && record_.stage != RuntimeStage::Idle && elapsed > maxStageMs_) {
            maxStageMs_ = elapsed; peakStage_ = record_.stage;
        }
        record_.stageAtMs = now;
        record_.stage = next;
    }
    void loop(uint32_t now) {
        if (loopSeen_ && now - lastLoop_ > maxLoopGapMs_) maxLoopGapMs_ = now - lastLoop_;
        loopSeen_ = true; lastLoop_ = now;
        record_.loops = record_.loops + 1;
    }
    void controlCompleted(uint32_t now) { record_.controlAtMs = now; }
    bool reportDue(uint32_t now) {
        if (now - lastReport_ < 5000) return false;
        lastReport_ = now; return true;
    }
    uint32_t maxStageMs() const { return maxStageMs_; }
    uint32_t maxLoopGapMs() const { return maxLoopGapMs_; }
    RuntimeStage peakStage() const { return peakStage_; }
private:
    static constexpr uint32_t kMagic = 0x53494c44u;
    volatile ProgressRecord &record_;
    uint32_t lastReport_ = 0, lastLoop_ = 0, maxStageMs_ = 0, maxLoopGapMs_ = 0;
    RuntimeStage peakStage_ = RuntimeStage::Boot;
    bool loopSeen_ = false;
};
inline const char *stageName(RuntimeStage stage) {
    switch (stage) {
    case RuntimeStage::Boot: return "boot";
    case RuntimeStage::Wifi: return "wifi";
    case RuntimeStage::Mqtt: return "mqtt";
    case RuntimeStage::Sbt: return "sbt";
    case RuntimeStage::Publish: return "publish";
    case RuntimeStage::Control: return "control";
    case RuntimeStage::Sensors: return "sensors";
    case RuntimeStage::Config: return "config";
    case RuntimeStage::Idle: return "idle";
    }
    return "unknown";
}
static_assert(sizeof(ProgressRecord) <= 32, "RTC progress record must stay bounded");
} // namespace sil
