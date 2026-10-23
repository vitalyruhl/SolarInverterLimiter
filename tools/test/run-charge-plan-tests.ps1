$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/charge_plan_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/charge_plan_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Charge plan host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Charge plan host tests failed' }
