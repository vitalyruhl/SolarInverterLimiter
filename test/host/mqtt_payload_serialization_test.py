"""[MOCKED!] Compile the unchanged production serializers with host-only state."""
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


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


def compile_run(source):
    # Keep generated executables within the consumer's existing local build tree.
    with tempfile.TemporaryDirectory(prefix="payload-", dir=REPO / ".pio") as tmp:
        source_path, executable = Path(tmp) / "test.cpp", Path(tmp) / "test.exe"
        source_path.write_text(BASE + source, encoding="utf-8")
        result = subprocess.run(
            [str(CLANG), "-std=c++17", "-Wall", "-Wextra", "-Werror",
             str(source_path), "-o", str(executable)],
            text=True, capture_output=True, timeout=30)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(executable)], text=True, capture_output=True,
                                timeout=10, check=True)
        return [json.loads(line) for line in result.stdout.splitlines()]


class PayloadSerialization(unittest.TestCase):
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
struct { uint32_t bootCount=UINT32_MAX; } retainedProgress;
const char* stageName(int) { return "control"; }
struct Diagnostics {
 struct { bool valid=true; int stage=0; uint32_t stageAtMs=UINT32_MAX; } previous_;
 struct {
  uint32_t maxLoopGapMs() const { return UINT32_MAX; }
  uint32_t maxStageMs() const { return UINT32_MAX; }
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
            self.assertLess(len(json.dumps(row, separators=(",", ":"))), 512)
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
 } progress_;
 uint32_t reset_=0;
 static unsigned long ulong(uint32_t value) { return static_cast<unsigned long>(value); }
''' + source[start:end] + r'''
};
int main() { Diagnostics d; d.report(Stats{}, 12, 13); }
''')
        values = dict(token.split("=", 1) for token in rows[0].split() if "=" in token)
        for key, expected in {"rx_to": 1, "rx_bad": 2, "rx_big": 3, "tx_fail": 4,
                              "conn_fail": 5, "txBackpressure": 6, "txPartialWrites": 7,
                              "txTimeouts": 8, "txOverflows": 2**32-1,
                              "price_rev": 12, "pv_rev": 13}.items():
            self.assertEqual(int(values[key]), expected)

    def test_planning_config_uses_current_physical_output_setting(self):
        source = (REPO / "src/main.cpp").read_text(encoding="utf-8")
        start = source.index("struct JsonNumberText\n")
        end = source.index("static String buildPlanningStatePayload()", start)
        serializer = source[start:end]
        rows = compile_run(r'''
struct Settings {
 bool valid=true, mosManualMode=true, inverterEnabled=true;
 float reserveOffSocPercent=2, reserveOnSocPercent=10;
 float priceComfortEurPerKwh=.21f, priceDeadbandEurPerKwh=.2f, priceHysteresisEurPerKwh=.0667f;
} preparedSettings;
float forecastPriceFactor=.95f;
struct Setting { int value=1100; int get() const { return value; } };
struct { Setting maxOutput; } limiterSettings;
''' + serializer + r'''
int main() {
 for (int watts : {650, 1100, 1600, 0, INT32_MAX}) {
  limiterSettings.maxOutput.value=watts;
  std::cout << buildPlanningConfigPayload() << '\n';
 }
}
''')
        self.assertEqual([row["maxOutputW"] for row in rows], [650, 1100, 1600, 0, 2**31-1])
        for row in rows:
            self.assertTrue(row["valid"])
            self.assertEqual(row["v"], 1)
            self.assertAlmostEqual(row["fcPriceF"], .95)
            self.assertLess(len(json.dumps(row, separators=(",", ":"))), 256)


if __name__ == "__main__":
    unittest.main(verbosity=2)
