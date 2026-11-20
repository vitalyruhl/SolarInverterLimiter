"""[MOCKED!] Compile the unchanged production serializers with host-only state."""
import json
from pathlib import Path
import re
import subprocess
import unittest
import uuid


REPO = Path(__file__).resolve().parents[2]
CLANG = Path("C:/Program Files/LLVM/bin/clang++.exe")
BASE = r'''
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
using String = std::string;
'''


def compile_run(source, extra_flags=()):
    # Keep unique generated files as local test evidence in the approved workspace.
    output_dir = REPO / ".Temp" / "test" / "mqtt-payload" / uuid.uuid4().hex
    output_dir.mkdir(parents=True)
    source_path, executable = output_dir / "test.cpp", output_dir / "test.exe"
    source_path.write_text(BASE + source, encoding="utf-8")
    result = subprocess.run(
        [str(CLANG), "-std=c++17", "-Wall", "-Wextra", "-Werror",
         *extra_flags,
         str(source_path), "-o", str(executable)],
        text=True, capture_output=True, timeout=30)
    if result.returncode:
        raise AssertionError(result.stderr)
    result = subprocess.run([str(executable)], text=True, capture_output=True,
                            timeout=10, check=True)
    return [json.loads(line) for line in result.stdout.splitlines()]


class PayloadSerialization(unittest.TestCase):
    def test_planning_state_reports_local_rs485_write_status(self):
        source = (REPO / "src/main.cpp").read_text(encoding="utf-8")
        number_start = source.index("struct JsonNumberText\n")
        number_end = source.index("\n};", number_start) + len("\n};")
        state_start = source.index("static String buildPlanningStatePayload()")
        state_end = source.index("static void publishMqttNow()", state_start)
        rows = compile_run(r'''
struct Decision {
 const char *strategy="automatic", *reason="forecast_hold";
 int ceilingW=900;
 bool chargeTargetValid=false, reserveKnown=false, reserveAllowed=false, economicPlanApplied=false;
 float chargeTargetW=0;
} currentDecision;
struct Plan {
 bool valid=true; const char *reason="forecast_hold";
 float reservedWh=500, availableWh=900, shortfallWh=0, chargeTargetW=0;
 int batteryCeilingW=900; uint32_t targetAt=1700000000;
} futurePlan;
struct {
 bool available=false, valid=false;
 bool economicAvailable() const { return available; }
 bool economicValid() const { return valid; }
 bool economicStale() const { return available; }
 bool economicRetained() const { return available; }
 uint32_t economicGeneratedAt() const { return UINT32_MAX; }
 uint32_t economicAgeS() const { return UINT32_MAX; }
 uint32_t revision() const { return UINT32_MAX; }
} futurePrices;
namespace control {
 enum class OperatingMode { Automatic, ChargePriority };
 const char *operatingModeName(OperatingMode mode) {
  return mode == OperatingMode::Automatic ? "automatic" : "charge_priority";
 }
}
struct { struct Mode { control::OperatingMode mode=control::OperatingMode::Automatic; };
 Mode confirmed() const { return {}; }
} operatingModes;
struct Publication {
 struct { uint32_t generatedAt=1700000000; } snapshot;
 bool available=false, held=false;
 uint32_t since=0;
 bool planAvailable() const { return available; }
 bool retained() const { return held; }
 uint32_t degradedSince() const { return since; }
} dischargeBudget, chargePlan;
uint32_t millis() { return 300000; }
int inverterSetValue=0;
bool currentDecisionAvailable=false;
const bool kRs485MockTransport=false;
const char *lastTransportWriteStatus="unknown";
''' + source[number_start:number_end] + source[state_start:state_end] + r'''
int main() {
 std::cout << buildPlanningStatePayload() << '\n';
 currentDecisionAvailable=true;
 for (const char *status : {"accepted", "write_failed", "disabled", "mock"}) {
  lastTransportWriteStatus=status;
  futurePrices.available=true;
  futurePrices.valid=std::string(status)!="disabled";
  currentDecision.economicPlanApplied=std::string(status)=="accepted";
  dischargeBudget.available=chargePlan.available=true;
  dischargeBudget.held=chargePlan.held=std::string(status)=="write_failed";
  std::cout << buildPlanningStatePayload() << '\n';
 }
 // Upper-bound fixture for protocol counters, numeric fields and real reason strings.
 futurePlan.targetAt=UINT32_MAX; futurePlan.batteryCeilingW=INT32_MAX;
 futurePlan.reason="current_price_missing";
 futurePlan.reservedWh=futurePlan.availableWh=futurePlan.shortfallWh=2147483647.0f;
 currentDecision.strategy="pv_limited_fallback"; currentDecision.reason="reserve_grace_unavailable";
 currentDecision.ceilingW=INT32_MAX; currentDecision.chargeTargetValid=true;
 currentDecision.chargeTargetW=2147483647.0f; inverterSetValue=INT32_MAX;
 std::cout << buildPlanningStatePayload() << '\n';
 std::string longReason(900, 'x'); currentDecision.reason=longReason.c_str();
 std::cout << buildPlanningStatePayload() << '\n';
}
''')
        self.assertEqual([row.get("transportWriteStatus") for row in rows[:5]],
                         ["unknown", "accepted", "write_failed", "disabled", "mock"])
        self.assertIsNone(rows[0]["strategy"])
        self.assertEqual(rows[0]["economic"], {"available":False,"valid":False,"stale":False,
            "retained":False,"g":None,"ageS":None,"applied":False})
        self.assertTrue(rows[1]["economic"]["applied"])
        self.assertFalse(rows[2]["economic"]["applied"])
        self.assertTrue(rows[2]["economic"]["valid"])
        self.assertFalse(rows[3]["economic"]["valid"])
        self.assertEqual(rows[1]["economic"]["ageS"], 4294967295)
        self.assertFalse(rows[0].get("diagnosticPlansAvailable"))
        self.assertTrue(rows[2].get("diagnosticPlansAvailable"))
        self.assertTrue(rows[2].get("diagnosticPlansRetained"))
        self.assertEqual((rows[0]["dischargeG"], rows[0]["chargeG"]), (0, 0))
        self.assertEqual((rows[2]["dischargeG"], rows[2]["chargeG"]), (1700000000, 1700000000))
        self.assertEqual(rows[2].get("diagnosticRetentionAgeS"), 300)
        self.assertFalse(rows[1].get("diagnosticPlansRetained"))
        capacity = int(re.search(r"char payload\[(\d+)\]", source[state_start:state_end])[1])
        for row in rows[1:5]:
            self.assertEqual(row["strategy"], "automatic")
            self.assertEqual(row["limitW"], 900)
            self.assertLess(len(json.dumps(row, separators=(",", ":"))), capacity)
        maximum=rows[5]
        self.assertEqual(maximum["reason"], "reserve_grace_unavailable")
        size=len(json.dumps(maximum,separators=(",", ":")).encode())
        self.assertLess(size, capacity)
        self.assertLessEqual(size+7+len("SolarLimiterTest/Planning/State"), 2304)
        self.assertEqual(rows[6], {"v": 1, "diagnosticError": "format"})

    def test_runtime_counters_preserve_existing_fields_and_fit_uint32(self):
        source = (REPO / "src/runtime/RuntimeDiagnostics.h").read_text(encoding="utf-8")
        match = re.search(r"(?m)^    (?:template<class Stats> )?String mqttPayload", source)
        self.assertIsNotNone(match, "Production diagnostic serializer boundary missing")
        end = source.index("\n    }\nprivate:", match.start()) + len("\n    }")
        method = source[match.start():end]
        rows = compile_run(r'''
struct Stats {
 uint32_t rxTimeouts=0, rxMalformed=0, rxOversize=0, txFailures=0, connectFailures=0;
 uint32_t txBackpressure=0, txPartialWrites=0, txTimeouts=0, txOverflows=0;
};
uint32_t millis() { return UINT32_MAX; }
struct { uint32_t bootCount=UINT32_MAX; } retainedProgress;
const char* stageName(int) { return "control"; }
struct Diagnostics {
 struct { bool valid=true; int stage=0; uint32_t stageAtMs=UINT32_MAX; } previous_;
 struct {
  uint32_t maxLoopGapMs() const { return UINT32_MAX; }
  uint32_t maxStageMs() const { return UINT32_MAX; }
  int peakStage() const { return 0; }
  uint32_t peakStageEndedAtMs() const { return UINT32_MAX; }
  uint32_t controlAgeMs(uint32_t) const { return UINT32_MAX; }
  uint32_t loops() const { return UINT32_MAX; }
  int lastSlowStage() const { return 0; }
  uint32_t lastSlowStageMs() const { return UINT32_MAX; }
  uint32_t lastSlowStageEndedAtMs() const { return UINT32_MAX; }
  uint32_t lastSlowLoopMs() const { return UINT32_MAX; }
  uint32_t lastSlowLoopEndedAtMs() const { return UINT32_MAX; }
 } progress_;
 uint32_t reset_=UINT32_MAX;
 static unsigned long ulong(uint32_t value) { return static_cast<unsigned long>(value); }
''' + method + r'''
};
int main() {
 Diagnostics d; Stats stats;
 std::cout << d.mqttPayload(stats) << '\n';
 stats.rxTimeouts=1; stats.rxMalformed=2; stats.rxOversize=3;
 stats.txFailures=4; stats.connectFailures=5;
 stats.txBackpressure=6; stats.txPartialWrites=7; stats.txTimeouts=8; stats.txOverflows=9;
 std::cout << d.mqttPayload(stats) << '\n';
 stats.rxTimeouts=stats.rxMalformed=stats.rxOversize=stats.txFailures=stats.connectFailures=UINT32_MAX;
 stats.txBackpressure=stats.txPartialWrites=stats.txTimeouts=stats.txOverflows=UINT32_MAX;
 std::cout << d.mqttPayload(stats) << '\n';
 d.previous_.valid=false;
 std::cout << d.mqttPayload(stats) << '\n';
}
''')
        counters = ("rxTimeouts", "rxMalformed", "rxOversize", "txFailures", "connectFailures",
                    "txBackpressure", "txPartialWrites", "txTimeouts", "txOverflows")
        for row, values in zip(rows[:3], ([0]*9, list(range(1, 10)), [2**32-1]*9)):
            self.assertEqual([row[k] for k in counters], values)
            self.assertEqual(row["boot"], 2**32-1)
            self.assertEqual(row["previousStage"], "control")
            self.assertEqual(row["maxStageMs"], 2**32-1)
            for key in ("peakStageEndedAtMs", "controlAgeMs", "loops", "lastSlowStageMs",
                        "lastSlowStageEndedAtMs", "lastSlowLoopMs", "lastSlowLoopEndedAtMs"):
                self.assertEqual(row[key], 2**32-1)
            self.assertEqual(row["peakStage"], "control")
            self.assertEqual(row["lastSlowStage"], "control")
            capacity = int(re.search(r"char out\[(\d+)\]", method)[1])
            self.assertLess(len(json.dumps(row, separators=(",", ":"))), capacity)
        # Every decimal field at UINT32_MAX and all stage strings at the maximum length.
        self.assertEqual(len(json.dumps(rows[2], separators=(",", ":"))), 631)
        self.assertEqual(rows[3]["previousStage"], "unknown")
        stages = (REPO / "src/runtime/RuntimeProgress.h").read_text(encoding="utf-8")
        self.assertLessEqual(max(map(len, re.findall(r'return "([a-z]+)"', stages))), len("control"))
        main = (REPO / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn("runtimeDiagnostics.mqttPayload(mqtt.getBoundedIoStats())", main)

    def test_uart_uses_the_same_transport_counter_values(self):
        source = (REPO / "src/runtime/RuntimeDiagnostics.h").read_text(encoding="utf-8")
        start = source.index("    template<class Stats> void report(")
        end = source.index("\n    }", start) + len("\n    }")
        rows = compile_run(r'''
#include <cstdarg>
#include <iomanip>
struct Stats {
 uint32_t rxTimeouts=1, rxMalformed=2, rxOversize=3, txFailures=4, connectFailures=5;
 uint32_t txBackpressure=6, txPartialWrites=7, txTimeouts=8, txOverflows=UINT32_MAX;
};
uint32_t millis() { return 1000; }
unsigned uxTaskGetStackHighWaterMark(void*) { return 100; }
bool heap_caps_check_integrity_all(bool) { return true; }
struct IPAddress { unsigned operator[](int) const { return 1; } };
struct { IPAddress localIP() { return {}; } } WiFi;
struct {
 unsigned getFreeHeap() { return 100; }
 unsigned getMinFreeHeap() { return 100; }
 unsigned getMaxAllocHeap() { return 100; }
} ESP;
struct {
 void printf(const char* format, ...) {
  char output[1024]; va_list args; va_start(args, format);
  vsnprintf(output, sizeof(output), format, args); va_end(args);
  std::string text(output); if (!text.empty() && text.back()=='\n') text.pop_back();
  std::cout << std::quoted(text) << '\n';
 }
} Serial;
struct { uint32_t bootCount=1, controlAtMs=0; } retainedProgress;
const char* stageName(int) { return "control"; }
struct Diagnostics {
 struct {
  bool reportDue(uint32_t) { return true; }
  uint32_t maxLoopGapMs() { return 1; }
  uint32_t maxStageMs() { return 1; }
  int peakStage() { return 0; }
  uint32_t peakStageEndedAtMs() { return 101; }
  uint32_t controlAgeMs(uint32_t now) { return now; }
  uint32_t loops() { return 102; }
  int lastSlowStage() { return 0; }
  uint32_t lastSlowStageMs() { return 103; }
  uint32_t lastSlowStageEndedAtMs() { return 104; }
  uint32_t lastSlowLoopMs() { return 105; }
  uint32_t lastSlowLoopEndedAtMs() { return 106; }
 } progress_;
 uint32_t reset_=0;
 static unsigned long ulong(uint32_t value) { return static_cast<unsigned long>(value); }
''' + source[start:end] + r'''
};
int main() { Diagnostics d; d.report(Stats{}, 12, 13, "write_failed"); }
''')
        values = dict(token.split("=", 1) for token in rows[0].split() if "=" in token)
        for key, expected in {"rx_to": 1, "rx_bad": 2, "rx_big": 3, "tx_fail": 4,
                              "conn_fail": 5, "txBackpressure": 6, "txPartialWrites": 7,
                              "txTimeouts": 8, "txOverflows": 2**32-1,
                              "price_rev": 12, "pv_rev": 13,
                              "peak_at": 101, "loops": 102, "slow_stage_ms": 103,
                              "slow_stage_at": 104, "slow_loop_ms": 105, "slow_loop_at": 106}.items():
            self.assertEqual(int(values[key]), expected)
        self.assertEqual(values["slow_stage"], "control")
        self.assertEqual(values.get("rs485_tx"), "write_failed")

    def test_runtime_serializer_reports_truncation_instead_of_partial_json(self):
        source = (REPO / "src/runtime/RuntimeDiagnostics.h").read_text(encoding="utf-8")
        start = source.index("    template<class Stats> String mqttPayload")
        end = source.index("\n    }\nprivate:", start) + len("\n    }")
        method = re.sub(r"char out\[\d+\]", "char out[8]", source[start:end])
        rows = compile_run(r'''
struct Stats {
 uint32_t rxTimeouts=0, rxMalformed=0, rxOversize=0, txFailures=0, connectFailures=0;
 uint32_t txBackpressure=0, txPartialWrites=0, txTimeouts=0, txOverflows=0;
};
uint32_t millis() { return 0; }
struct { uint32_t bootCount=0; } retainedProgress;
const char* stageName(int) { return "unknown"; }
struct Diagnostics {
 struct { bool valid=false; int stage=0; uint32_t stageAtMs=0; } previous_;
 struct {
  uint32_t maxLoopGapMs() const { return 0; }
  uint32_t maxStageMs() const { return 0; }
  int peakStage() const { return 0; }
  uint32_t peakStageEndedAtMs() const { return 0; }
  uint32_t controlAgeMs(uint32_t) const { return 0; }
  uint32_t loops() const { return 0; }
  int lastSlowStage() const { return 0; }
  uint32_t lastSlowStageMs() const { return 0; }
  uint32_t lastSlowStageEndedAtMs() const { return 0; }
  uint32_t lastSlowLoopMs() const { return 0; }
  uint32_t lastSlowLoopEndedAtMs() const { return 0; }
 } progress_;
 uint32_t reset_=0;
 static unsigned long ulong(uint32_t value) { return static_cast<unsigned long>(value); }
''' + method + r'''
};
int main() { Diagnostics d; std::cout << d.mqttPayload(Stats{}) << '\n'; }
''', extra_flags=("-Wno-format-truncation",))  # Deliberately undersized formatter fault injection.
        self.assertEqual(rows, [{"diagnosticError": "format"}])

    def test_planning_config_uses_current_physical_output_setting(self):
        source = (REPO / "src/main.cpp").read_text(encoding="utf-8")
        number_start = source.index("struct JsonNumberText\n")
        number_end = source.index("\n};", number_start) + len("\n};")
        config_start = source.index("static String buildPlanningConfigPayload()")
        config_end = source.index("static String buildPlanningAssessmentPayload()", config_start)
        config_serializer = source[config_start:config_end]
        serializer = source[number_start:number_end] + config_serializer
        rows = compile_run(r'''
#include "control/PlanLifecycleSettings.h"
struct {
 control::PlanLifecycleSettings values;
 control::PlanLifecycleSettings snapshot() const { return values; }
 bool usingDefaultsAfterInvalidLoad() const { return false; }
} planSettings;
struct Settings {
 bool valid=true, mosManualMode=true, inverterEnabled=true;
 float reserveOffSocPercent=2, reserveOnSocPercent=10;
 float priceComfortEurPerKwh=.21f, priceDeadbandEurPerKwh=.2f, priceHysteresisEurPerKwh=.0667f;
 float deferralToleranceEurKwh=0, predictionPremium12hPct=0, predictionPremium24hPct=0, predictionPremiumPct=0;
} preparedSettings;
namespace control { constexpr bool kPlannerShadowProfile=false; constexpr unsigned kPriceInputVersion=2; }
float forecastPriceFactor=.95f;
struct Setting { int value=1100; int get() const { return value; } };
struct { Setting maxOutput; } limiterSettings;
''' + serializer + r'''
int main() {
 for (int watts : {650, 1100, 1600, 0, INT32_MAX}) {
  limiterSettings.maxOutput.value=watts;
  if (watts == INT32_MAX) planSettings.values.planMaxAgeS = 604800;
  std::cout << buildPlanningConfigPayload() << '\n';
 }
}
''', extra_flags=("-I", str(REPO / "src")))
        self.assertEqual([row["maxOutputW"] for row in rows], [650, 1100, 1600, 0, 2**31-1])
        for row in rows:
            self.assertTrue(row["valid"])
            self.assertEqual(row["v"], 1)
            self.assertAlmostEqual(row["fcPriceF"], .95)
            self.assertEqual(row["planMaxAgeS"], 604800 if row["maxOutputW"] == 2**31-1 else 7200)
            self.assertEqual(row["planRetentionS"], 300)
            self.assertEqual(row["priceScheduleFreshnessS"], 180)
            self.assertFalse(row["planSettingsInvalidAtBoot"])
            capacity = int(re.search(r"char payload\[(\d+)\]", config_serializer)[1])
            self.assertLess(len(json.dumps(row, separators=(",", ":"))), capacity)


if __name__ == "__main__":
    unittest.main(verbosity=2)
