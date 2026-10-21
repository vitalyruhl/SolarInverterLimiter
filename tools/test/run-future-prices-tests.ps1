$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/future_prices_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/future_prices_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Future prices host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Future prices host tests failed' }
