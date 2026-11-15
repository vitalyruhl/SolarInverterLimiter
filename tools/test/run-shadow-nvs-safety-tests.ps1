$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDirectory = Join-Path $repo '.Temp/test/shadow-nvs-safety'
$compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$source = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
function Get-UniqueProductionMatch([string] $pattern, [string] $description) {
    $matches = [regex]::Matches($source, $pattern)
    if ($matches.Count -ne 1) { throw "Expected one production $description, found $($matches.Count)" }
    return $matches[0].Value
}

# Keep the complete actual NVS and reset-button functions. For setup/loop,
# preserve the real prefix through the first normal-setup / WiFi boundary.
$nvs = Get-UniqueProductionMatch '(?ms)^static bool ensureNvsReady\(\)\r?\n\{.*?^\}' 'NVS initialization'
$setup = Get-UniqueProductionMatch '(?ms)^void setup\(\)\r?\n\{.*?^    setupLogging\(\);' 'setup prefix'
$loop = Get-UniqueProductionMatch '(?ms)^void loop\(\)\r?\n\{.*?^    ConfigManager\.getWiFiManager\(\)\.update\(\);' 'loop prefix'
$io = Get-UniqueProductionMatch '(?ms)^static void registerIOBindings\(\)\r?\n\{.*?^\}' 'IO registration'
$state = [regex]::Match($source, '(?m)^static bool shadowStartupBlocked\s*=\s*(?:true|false)\s*;')
$stateText = if ($state.Success) { $state.Value } else { 'static bool shadowStartupBlocked = false; // RED fixture: production gate not added yet.' }
$header = Join-Path $outputDirectory 'shadow_nvs_production.h'
[IO.File]::WriteAllText($header, $stateText + "`n" + $nvs + "`n" + $setup + "`n}`n" + $loop + "`n}`n" + $io)

$failed = @()
foreach ($profile in @('normal', 'planner_shadow')) {
    $flags = @('-DFEATURE_FAN_ENABLED=0', '-DFEATURE_HEATER_ENABLED=0', '-DFEATURE_OLED_DISPLAY_ENABLED=0')
    if ($profile -eq 'planner_shadow') { $flags += '-DSIL_PLANNER_SHADOW=1', '-DSIL_MOCK_TRANSPORT=1' }
    $output = Join-Path $outputDirectory ("shadow_nvs_safety_{0}.exe" -f $profile)
    & $compiler -std=c++17 -Wall -Wextra -Werror -Wno-c++20-designator -Wno-unused-variable `
        @flags -I $outputDirectory (Join-Path $repo 'test/host/shadow_nvs_safety_test.cpp') -o $output
    if ($LASTEXITCODE -ne 0) { throw "Shadow NVS test compilation failed: $profile" }
    Write-Output "PROFILE $profile"
    & $output
    if ($LASTEXITCODE -ne 0) { $failed += $profile }
}
if ($failed.Count) { Write-Output "FAIL profiles: $($failed -join ', ')"; exit 1 }
exit 0
