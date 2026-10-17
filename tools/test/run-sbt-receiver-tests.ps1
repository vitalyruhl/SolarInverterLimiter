param([string]$SbtRepository)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = Join-Path $repo '.pio/sbt_receiver_test.exe'
$compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
$common = @('-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', (Join-Path $repo 'src'), '-I', (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src'))
& $compiler @common (Join-Path $repo 'test/host/sbt_receiver_test.cpp') -o $output
if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Receiver tests failed' }
if ($SbtRepository) {
    $source = (Resolve-Path (Join-Path $SbtRepository 'src')).Path
    $contractOutput = Join-Path $repo '.pio/sbt_publisher_contract_test.exe'
    & $compiler @common -I $source (Join-Path $repo 'test/host/sbt_publisher_contract_test.cpp') -o $contractOutput
    if ($LASTEXITCODE -ne 0) { throw 'Publisher contract compilation failed' }
    & $contractOutput
    if ($LASTEXITCODE -ne 0) { throw 'Publisher contract tests failed' }
}
