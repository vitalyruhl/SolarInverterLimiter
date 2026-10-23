$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/pv_forecast_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/pv_forecast_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'PV forecast host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'PV forecast host tests failed' }
