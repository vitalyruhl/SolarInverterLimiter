$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
$outputDirectory = Join-Path $repo '.Temp/test/retained-plan-visibility'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$output = Join-Path $outputDirectory 'retained_plan_visibility_test.exe'

& $compiler -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror `
    -I (Join-Path $repo 'src') `
    -I (Join-Path $repo '.pio/plan-retention-5.4.7-final/libdeps/usb/ArduinoJson/src') `
    (Join-Path $repo 'test/host/retained_plan_visibility_test.cpp') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Retained plan visibility compilation failed' }

& $output
if ($LASTEXITCODE -ne 0) { throw 'Retained plan visibility tests failed' }
