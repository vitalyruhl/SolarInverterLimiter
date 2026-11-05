$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/telemetry_numeric_safety_test.exe'
$source = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
$disconnect = [regex]::Match($source, '(?s)mqtt.onDisconnected\(\[\]\(\) \{(.*?)\r?\n    \}\);')
if (!$disconnect.Success) { throw 'Production MQTT disconnect extraction missing' }
$header = Join-Path $repo '.pio/telemetry_disconnect_application.h'
[IO.File]::WriteAllText($header, 'static void productionMqttDisconnect(control::TelemetryAge &telemetryAge) { (void)telemetryAge;' + $disconnect.Groups[1].Value + '}')
# Trap mode checks undefined behavior without the unavailable Windows UBSan runtime.
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror '-fsanitize=undefined,float-cast-overflow' -fsanitize-trap=all -I (Join-Path $repo 'test/host/telemetry_stubs') -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/telemetry_numeric_safety_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Numeric safety host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Numeric safety host tests failed' }
