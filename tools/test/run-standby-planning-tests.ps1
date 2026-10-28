$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$source = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
$patterns = @(
    '(?m)^    const bool planSourceGrace = [^;]+;',
    '(?m)^    const bool chargerPlanningAvailable = [^;]+;',
    '(?m)^    const float planningVoltageV = [^;]+;',
    '(?m)^    const float planningMaxChargeW = [^;]+;',
    '(?m)^    projection.sourcesQualified = [^;]+;',
    '(?m)^    projection.maxChargeW = [^;]+;'
)
$statements = foreach ($pattern in $patterns) {
    $match = [regex]::Match($source, $pattern)
    if (!$match.Success) { throw "Production planning extraction missing: $pattern" }
    $match.Value
}
$chargeLimit = [regex]::Match($source, '(?m)^#define SIL_QUALIFIED_MPPT_MAX_CHARGE_A[^\r\n]+').Value
if (!$chargeLimit) { throw 'Qualified MPPT charge limit extraction missing' }
$header = $chargeLimit + [Environment]::NewLine + @'
static control::ChargePlanInputs productionProjection(const StandbySource &source,
    const control::CurrentInputs &inputs, const control::CurrentDecision &currentDecision, bool knownSourceFault = false) {
    control::PreparedSettings preparedSettings; preparedSettings.valid = true;
    control::ChargePlanInputs projection; projection.controlEnabled = true;
'@ + [Environment]::NewLine + ($statements -join [Environment]::NewLine) + [Environment]::NewLine + '(void)planSourceGrace; (void)knownSourceFault; return projection; }'
$header += [Environment]::NewLine + 'static bool productionPlanGrace(const StandbySource &source) {' + $statements[0] + 'return planSourceGrace; }'
[IO.File]::WriteAllText((Join-Path $repo '.pio/standby_planning_application.h'), $header)
$output = Join-Path $repo '.pio/standby_planning_application_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/standby_planning_application_test.cpp') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Standby planning application compilation failed' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Standby planning application tests failed' }
