$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.Temp/test/shadow-core-nvs-guard'
$stub = Join-Path $repo 'test/host/shadow_nvs_stubs'
$compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
$coreDir = Join-Path (Split-Path $repo -Parent) '.platformio-codex'
$core = Join-Path $coreDir 'packages/framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c'
if (!(Test-Path -LiteralPath $core)) { throw 'Selected Arduino Core source is unavailable' }
New-Item -ItemType Directory -Path $out -Force | Out-Null

function Get-UniqueMatch([string] $source, [string] $pattern, [string] $description) {
    $matches = [regex]::Matches($source, $pattern)
    if ($matches.Count -ne 1) { throw "Expected one $description, found $($matches.Count)" }
    return $matches[0].Value
}
$coreSource = [IO.File]::ReadAllText($core)
$coreInit = Get-UniqueMatch $coreSource '(?ms)^void initArduino\(\)\r?\n\{.*?^\}' 'selected Arduino Core initArduino'
[IO.File]::WriteAllText((Join-Path $out 'shadow_core_actual.cpp'), "#include `"shadow_core_runtime.h`"`n" + $coreInit + "`n")
$silSource = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
$nvs = Get-UniqueMatch $silSource '(?ms)^static bool ensureNvsReady\(\)\r?\n\{.*?^\}' 'SIL NVS initialization'
$setup = Get-UniqueMatch $silSource '(?ms)^void setup\(\)\r?\n\{.*?^    setupLogging\(\);' 'SIL setup prefix'
$loop = Get-UniqueMatch $silSource '(?ms)^void loop\(\)\r?\n\{.*?^    ConfigManager\.getWiFiManager\(\)\.update\(\);' 'SIL loop prefix'
$state = [regex]::Match($silSource, '(?m)^static bool shadowStartupBlocked\s*=\s*(?:true|false)\s*;')
$stateText = if ($state.Success) { $state.Value } else { 'static bool shadowStartupBlocked = false; // RED fixture: production gate not added yet.' }
[IO.File]::WriteAllText((Join-Path $out 'shadow_startup_actual.h'), $stateText + "`n" + $nvs + "`n" + $setup + "`n}`n" + $loop + "`n}`n")

# Before implementation, an explicit pass-through stub gives a behavioral RED
# rather than a missing-file compiler error. Once present, use the real file.
$guard = Join-Path $repo 'src/runtime/ShadowNvsGuard.cpp'
if (!(Test-Path -LiteralPath $guard)) {
    $guard = Join-Path $out 'red_passthrough_guard.cpp'
    [IO.File]::WriteAllText($guard, '#include "nvs_flash.h"' + "`n" +
        'extern "C" esp_err_t __wrap_nvs_flash_init() { return __real_nvs_flash_init(); }' + "`n" +
        'esp_err_t shadowNvsBootError() { return ESP_OK; }' + "`n")
    Write-Output '[MOCKED!] RED: production guard absent; pass-through fixture used'
} else { Write-Output '[MOCKED!] Using actual src/runtime/ShadowNvsGuard.cpp' }

$common = @('-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-I', $stub, '-I', $out)
$fake = Join-Path $out 'shadow_core_fake.obj'
& $compiler @common -c (Join-Path $stub 'shadow_core_fake.cpp') -o $fake
if ($LASTEXITCODE -ne 0) { throw 'NVS boundary compilation failed' }
$failed = @()
foreach ($profile in @('normal', 'planner_shadow')) {
    $flags = @()
    if ($profile -eq 'planner_shadow') { $flags += '-DSIL_PLANNER_SHADOW=1', '-DSIL_MOCK_TRANSPORT=1', '-Dnvs_flash_init=__wrap_nvs_flash_init' }
    $coreObject = Join-Path $out "core_$profile.obj"
    $testObject = Join-Path $out "test_$profile.obj"
    $exe = Join-Path $out "shadow_core_nvs_$profile.exe"
    & $compiler @common @flags -c (Join-Path $out 'shadow_core_actual.cpp') -o $coreObject
    if ($LASTEXITCODE -ne 0) { throw "Actual Core compilation failed: $profile" }
    & $compiler @common @flags -c (Join-Path $repo 'test/host/shadow_core_nvs_guard_test.cpp') -o $testObject
    if ($LASTEXITCODE -ne 0) { throw "Actual SIL setup test compilation failed: $profile" }
    $objects = @($fake, $coreObject, $testObject)
    if ($profile -eq 'planner_shadow') {
        $guardObject = Join-Path $out 'shadow_guard.obj'
        & $compiler @common -DSIL_PLANNER_SHADOW=1 -DSIL_MOCK_TRANSPORT=1 -c $guard -o $guardObject
        if ($LASTEXITCODE -ne 0) { throw 'Shadow guard compilation failed' }
        $objects += $guardObject
    }
    & $compiler @objects -o $exe
    if ($LASTEXITCODE -ne 0) { throw "Host link failed: $profile" }
    $scenarios = @('success', 'no-free', 'new-version', 'other')
    if ($profile -eq 'planner_shadow') { $scenarios += 'later', 'latch' }
    foreach ($scenario in $scenarios) {
        Write-Output "PROFILE $profile SCENARIO $scenario"
        & $exe $scenario
        if ($LASTEXITCODE -ne 0) { $failed += "$profile/$scenario" }
    }
}
if ($failed.Count) { Write-Output "FAIL scenarios: $($failed -join ', ')"; exit 1 }
exit 0
