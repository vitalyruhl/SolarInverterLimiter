$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDirectory = Join-Path $repo '.Temp/test/independent-planning'
$compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
$json = Join-Path $repo '.pio/plan-retention-5.4.7-final/libdeps/usb/ArduinoJson/src'
if (!(Test-Path -LiteralPath (Join-Path $json 'ArduinoJson.h'))) { throw 'Existing ArduinoJson headers not found' }
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$source = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
function Get-UniqueProductionMatch([string] $pattern, [string] $description) {
    $matches = [regex]::Matches($source, $pattern)
    if ($matches.Count -ne 1) { throw "Expected one production $description, found $($matches.Count)" }
    return $matches[0].Value
}
$chargeLimit = Get-UniqueProductionMatch '(?m)^#define SIL_QUALIFIED_MPPT_MAX_CHARGE_A[^\r\n]+' 'MPPT limit'
$pidSource = Get-UniqueProductionMatch '(?s)static void resetPidController\(\)\r?\n\{.*?(?=//----------------------------------------)' 'PID section'
$issue = Get-UniqueProductionMatch '(?s)static void issueInverterCommand\(int watts, int maximumW, int outputOffsetW, int minimumW = 0\)\r?\n\{.*?(?=static void processRS485Tick\(\)\r?\n\{)' 'command function'
$tick = Get-UniqueProductionMatch '(?ms)^static void processRS485Tick\(\)\r?\n\{.*?^\}\r?\n(?=\r?\n#if CM_ENABLE_LOGGING && CM_ENABLE_VERBOSE_LOGGING\r?\nstatic void verboseLogging\(\))' 'control tick'
$numberText = Get-UniqueProductionMatch '(?ms)^struct JsonNumberText\r?\n\{.*?^\};' 'JSON number formatter'
$planningState = Get-UniqueProductionMatch '(?ms)^static String buildPlanningStatePayload\(\)\r?\n\{.*?^\}\r?\n(?=\r?\nstatic void publishMqttNow\(\))' 'planning state payload'
$modeBlock = Get-UniqueProductionMatch '(?ms)^    if \(operatingModes\.loop\(uint32_t\(time\(nullptr\)\)\)\) \{\r?\n.*?^    \}' 'mode-change branch'
$modeThreshold = Get-UniqueProductionMatch '(?m)^    preparedSettings\.chargePriorityThreshold\s*=\s*operatingModes\.confirmed\(\)\.chargeThreshold;' 'mode threshold refresh'
$modeShare = Get-UniqueProductionMatch '(?m)^    preparedSettings\.chargeInverterSharePct\s*=\s*operatingModes\.confirmed\(\)\.inverterSharePercent;' 'mode share refresh'
if ([regex]::Matches($tick, '\btime\(nullptr\)').Count -ne 1) { throw 'Expected one UTC source in production tick' }
$tick = [regex]::Replace($tick, '\btime\(nullptr\)', 'testFixtureTime(nullptr)')
$originalModeCondition = 'operatingModes.loop(uint32_t(time(nullptr)))'
if (!$modeBlock.Contains($originalModeCondition)) { throw 'Mode-change condition not found' }
$modeBlock = $modeBlock.Replace($originalModeCondition, 'modeChanged')
$modeRefresh = "static void refreshPreparedSettings() {`n$modeThreshold`n$modeShare`n}`n"
$modeApply = "static void applyConfirmedModeChange(bool modeChanged) {`n$modeBlock`n}`n"
foreach ($required in @('futurePrices.plan(', 'currentControl.update(', 'modePolicy.update(',
                        'chargePlan.candidate.project(', 'dischargeBudget.select(', 'chargePlan.select(')) {
    if (!$tick.Contains($required)) { throw "Production tick extraction missing: $required" }
}
$header = Join-Path $outputDirectory 'limiter_disabled_production.h'
[IO.File]::WriteAllText($header, $chargeLimit + "`n" + $numberText + "`n" + $planningState + "`n" + $pidSource + "`n" + $issue + "`n" + $tick + "`n" + $modeRefresh + $modeApply)

$failedProfiles = @()
foreach ($profile in @('production_encoder', 'planner_shadow')) {
    $output = Join-Path $outputDirectory ("independent_planning_{0}.exe" -f $profile)
    $profileFlags = @()
    if ($profile -eq 'planner_shadow') { $profileFlags += '-DSIL_PLANNER_SHADOW=1', '-DSIL_MOCK_TRANSPORT=1' }
    & $compiler -std=c++17 -Wall -Wextra -Werror -Wno-c++20-designator `
        -Wno-tautological-constant-out-of-range-compare -Wno-tautological-unsigned-zero-compare -Wno-sign-compare `
        -DCM_ENABLE_LOGGING=0 -DCM_ENABLE_VERBOSE_LOGGING=0 -DARDUINOJSON_POOL_CAPACITY=128 @profileFlags `
        -I (Join-Path $repo 'test/host/rs485_stubs') -I (Join-Path $repo 'src') -I $json -I $outputDirectory `
        (Join-Path $repo 'src/RS485Module/RS485Module.cpp') `
        (Join-Path $repo 'test/host/independent_planning_test.cpp') -o $output
    if ($LASTEXITCODE -ne 0) { throw "Independent planning application compilation failed: $profile" }
    & $output
    if ($LASTEXITCODE -ne 0) { $failedProfiles += $profile }
}
if ($failedProfiles.Count) { throw "Independent planning behavior failed: $($failedProfiles -join ', ')" }
