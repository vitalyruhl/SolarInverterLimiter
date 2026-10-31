$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
$output = Join-Path $repo '.local/operating_modes_test.exe'
& $compiler -std=c++17 -Wall -Wextra -Werror -I (Join-Path $repo 'src') (Join-Path $repo 'test/host/operating_modes_test.cpp') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Operating modes compilation failed' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Operating modes tests failed' }
$json = Join-Path $repo '.pio/plan-retention-5.4.7-final/libdeps/usb/ArduinoJson/src'
$adapterOutput = Join-Path $repo '.local/mode_settings_runtime_test.exe'
& $compiler -std=c++17 -Wall -Wextra -Werror -Wno-c++20-designator -I (Join-Path $repo 'test/host/mode_settings_stubs') -I (Join-Path $repo 'src') -I $json (Join-Path $repo 'test/host/mode_settings_runtime_test.cpp') -o $adapterOutput
if ($LASTEXITCODE -ne 0) { throw 'Mode settings adapter compilation failed' }
& $adapterOutput
if ($LASTEXITCODE -ne 0) { throw 'Mode settings adapter tests failed' }

# Compile the actual production manual dispatch and its unchanged output boundary.
$source = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
$issue = [regex]::Match($source, '(?s)static void issueInverterCommand\(int watts, int maximumW, int outputOffsetW, int minimumW = 0\)\r?\n\{.*?(?=static void processRS485Tick\(\)\r?\n\{)')
$dispatch = [regex]::Match($source, '(?s)    if \(modeDecision.handled\) \{\r?\n        // Policy demand.*?\r?\n    \}')
if (!$issue.Success -or !$dispatch.Success) { throw 'Production mode dispatch extraction missing' }
$header = Join-Path $repo '.local/operating_modes_application.h'
$dischargeFault = [regex]::Match($source, 'modeInputs.genuineFault = ([^;]+);')
if (!$dischargeFault.Success) { throw 'Production directional discharge fault mapping missing' }
$faultFunction = 'static bool productionDischargeFault(const SourceFlags &source) { return ' + $dischargeFault.Groups[1].Value + '; }'
[IO.File]::WriteAllText($header, $issue.Value + [Environment]::NewLine + 'static void applyModeDecision() {' + $dispatch.Value + '}' + [Environment]::NewLine + $faultFunction)
$applicationOutput = Join-Path $repo '.local/operating_modes_application_test.exe'
& $compiler -std=c++17 -Wall -Wextra -Werror -I (Join-Path $repo 'src') -I (Join-Path $repo '.local') (Join-Path $repo 'test/host/operating_modes_application_test.cpp') -o $applicationOutput
if ($LASTEXITCODE -ne 0) { throw 'Mode output application compilation failed' }
& $applicationOutput
if ($LASTEXITCODE -ne 0) { throw 'Mode output application tests failed' }
