$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/telemetry_numeric_safety_test.exe'
# Trap mode checks undefined behavior without the unavailable Windows UBSan runtime.
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror '-fsanitize=undefined,float-cast-overflow' -fsanitize-trap=all -I (Join-Path $repo 'test/host/telemetry_stubs') -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/telemetry_numeric_safety_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Numeric safety host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Numeric safety host tests failed' }
