param([string]$ConfigManagerPath='C:/Daten/_Codding/ESP32/ConfigurationsManager')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$cm=(Resolve-Path -LiteralPath $ConfigManagerPath).Path
$out=Join-Path $repo '.pio/compact-runtime-metadata/host-test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -I (Join-Path $repo 'src') -I (Join-Path $cm 'src') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/compact_runtime_metadata_test.cpp') -o $out
if($LASTEXITCODE -ne 0){throw 'Compact SIL host compile failed'}
& $out
if($LASTEXITCODE -ne 0){throw 'Compact SIL host assertions failed'}
