$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.Temp/test'
[void](New-Item -ItemType Directory -Force -Path $out)
$binary = Join-Path $out 'plan_settings_runtime_test.exe'
$json = Join-Path $repo '.pio/plan-retention-5.4.7-final/libdeps/usb/ArduinoJson/src'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -Wno-c++20-designator -I (Join-Path $repo 'test/host/mode_settings_stubs') -I (Join-Path $repo 'src') -I $json (Join-Path $repo 'test/host/plan_settings_runtime_test.cpp') -o $binary
if ($LASTEXITCODE -ne 0) { throw 'Plan settings compilation failed' }
& $binary
if ($LASTEXITCODE -ne 0) { throw 'Plan settings tests failed' }
