$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDirectory = Join-Path $repo '.Temp/test/telemetry-age-wrap'
$compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
$json = Join-Path $repo '.pio/plan-retention-5.4.7-final/libdeps/usb/ArduinoJson/src'
if (!(Test-Path -LiteralPath $compiler)) { throw 'Existing Clang compiler not found' }
if (!(Test-Path -LiteralPath (Join-Path $json 'ArduinoJson.h'))) { throw 'Existing ArduinoJson headers not found' }
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$output = Join-Path $outputDirectory 'telemetry_age_wrap_test.exe'
# Host JSON slots use wider pointers. Leave room for both parser documents in the ESP arena.
& $compiler -std=c++17 -O2 -DARDUINOJSON_POOL_CAPACITY=64 -Wall -Wextra -Werror `
    -I (Join-Path $repo 'test/host/telemetry_stubs') -I (Join-Path $repo 'src') -I $json `
    (Join-Path $repo 'test/host/telemetry_age_wrap_test.cpp') -o $output
if ($LASTEXITCODE -ne 0) { throw 'TelemetryAge wrap test compilation failed' }
& $output
exit $LASTEXITCODE
