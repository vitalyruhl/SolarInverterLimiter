param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo '.pio' }
$out = Join-Path $OutputDirectory 'plan_retention_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror -I (Join-Path $repo 'test/host/telemetry_stubs') -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/plan_retention_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Plan retention host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Plan retention host tests failed' }
