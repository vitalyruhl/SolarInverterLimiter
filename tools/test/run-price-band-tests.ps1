$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/price_band_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -I (Join-Path $repo 'src') (Join-Path $repo 'test/host/price_band_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Price-band compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Price-band tests failed' }
