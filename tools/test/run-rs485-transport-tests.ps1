$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$compiler = 'C:/Program Files/LLVM/bin/clang++.exe'
$outputDirectory = Join-Path $repo '.Temp/test/rs485-transport'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$common = @(
    '-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-c++20-designator', '-Wno-tautological-constant-out-of-range-compare',
    '-Wno-tautological-unsigned-zero-compare', '-Wno-sign-compare',
    '-I', (Join-Path $repo 'test/host/rs485_stubs'),
    '-I', (Join-Path $repo 'src'),
    (Join-Path $repo 'src/RS485Module/RS485Module.cpp'),
    (Join-Path $repo 'test/host/rs485_transport_test.cpp')
)

foreach ($profile in @('real_encoder', 'hard_mock')) {
    $output = Join-Path $outputDirectory ("rs485_transport_{0}.exe" -f $profile)
    $flags = @()
    if ($profile -eq 'hard_mock') { $flags += '-DSIL_MOCK_TRANSPORT=1' }
    & $compiler @common @flags -o $output
    if ($LASTEXITCODE -ne 0) { throw "RS485 $profile host compilation failed" }
    & $output
    if ($LASTEXITCODE -ne 0) { throw "RS485 $profile host test failed" }
}
