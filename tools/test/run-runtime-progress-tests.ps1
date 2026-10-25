$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/runtime_progress_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -I (Join-Path $repo 'src') (Join-Path $repo 'test/host/runtime_progress_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Runtime progress host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Runtime progress host tests failed' }
