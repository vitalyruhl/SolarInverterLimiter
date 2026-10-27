"""[MOCKED!] Compile the actual isolated recovery hook against host-only I/O."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output-directory", required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
out = Path(args.output_directory).resolve()
assert out.is_dir()
main = (repo / "src/main.cpp").read_text(encoding="utf-8")
guard = "#if defined(SIL_TEST_SINGLE_STACK_RESET) && SIL_TEST_SINGLE_STACK_RESET"
begin = main.index(guard, main.index("void loop()"))
end = main.index("#endif", begin) + len("#endif")
hook = main[begin:end]
source = r'''
#include <cassert>
#include <cstdint>
uint32_t fakeMillis=100,fakeUtc=1793038770;
unsigned resets=0;
uint32_t millis(){return fakeMillis;}
struct Mqtt {bool connected=true;bool isConnected(){return connected;}} mqtt;
struct Plan {bool valid=true;} futurePlan;
struct Publication {Plan snapshot;} dischargeBudget,chargePlan;
struct SerialStub {void printf(const char*,...){}} Serial;
struct Esp {unsigned getFreeHeap(){return 1;}unsigned getMaxAllocHeap(){return 1;}} ESP;
struct Manager {void performStackReset(){++resets;}} ConfigManager;
[[maybe_unused]] constexpr bool kRs485MockTransport=SIL_MOCK_TRANSPORT;
#define time(...) fakeUtc
template<unsigned Id> void tick(){
HOOK
}
#undef time
int main(){
#if SIL_TEST_SINGLE_STACK_RESET
 constexpr uint32_t t=1793038500;
 fakeMillis=100;fakeUtc=t+269;tick<0>();fakeMillis=10100;tick<0>();assert(resets==0);
 fakeMillis=11100;fakeUtc=t+270;tick<0>();assert(resets==1);tick<0>();assert(resets==1);
 resets=0;fakeMillis=100;fakeUtc=t+294;tick<1>();fakeMillis=10100;tick<1>();assert(resets==1);
 resets=0;fakeMillis=100;fakeUtc=t+295;tick<2>();fakeMillis=10100;tick<2>();assert(resets==0);
 fakeMillis=100;fakeUtc=0;tick<3>();fakeMillis=10100;tick<3>();assert(resets==0);
 fakeMillis=100;fakeUtc=t+270;mqtt.connected=false;tick<4>();mqtt.connected=true;
 fakeMillis=200;tick<4>();fakeMillis=10100;tick<4>();assert(resets==0);fakeMillis=10200;tick<4>();assert(resets==1);
 resets=0;fakeMillis=100;fakeUtc=t+569;tick<5>();fakeMillis=10100;tick<5>();assert(resets==0);
 fakeMillis=11100;fakeUtc=t+570;tick<5>();assert(resets==1);
 resets=0;fakeMillis=100;fakeUtc=t+594;tick<6>();fakeMillis=10100;tick<6>();assert(resets==1);
 resets=0;fakeMillis=100;fakeUtc=t+595;tick<7>();fakeMillis=10100;tick<7>();assert(resets==0);
 fakeMillis=100;fakeUtc=t+870;tick<8>();fakeMillis=10100;tick<8>();assert(resets==0);
 fakeMillis=100;fakeUtc=t+894;tick<9>();fakeMillis=10100;tick<9>();assert(resets==0);
#endif
}
'''.replace("HOOK", hook)
cpp = out / "recovery_boundary_547.cpp"
cpp.write_text(source, encoding="utf-8")
compiler = "C:/Program Files/LLVM/bin/clang++.exe"
for enabled, mock, success in ((1, 1, True), (0, 0, True), (1, 0, False)):
    exe = out / f"recovery_boundary_547_{enabled}_{mock}.exe"
    command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
               f"-DSIL_TEST_SINGLE_STACK_RESET={enabled}", f"-DSIL_MOCK_TRANSPORT={mock}", str(cpp), "-o", str(exe)]
    compiled = subprocess.run(command, capture_output=True, text=True)
    if success:
        assert compiled.returncode == 0, compiled.stderr
        subprocess.run([str(exe)], check=True)
    else:
        assert compiled.returncode != 0 and "Recovery injection requires the hard mock transport" in compiled.stderr
print("[MOCKED!] Actual recovery hook UTC 270..294/570..594 only, quarter-boundary exclusion, valid clock, 10-second qualification and hard-mock compile guard passed")
