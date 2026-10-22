$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/current_control_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -I (Join-Path $repo 'src') (Join-Path $repo 'test/host/current_control_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Current control host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Current control host tests failed' }

# Compile the unchanged production PID and final command application with host-only I/O stubs.
$source = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
$pidSource = [regex]::Match($source, '(?s)static void resetPidController\(\)\r?\n\{.*?(?=//----------------------------------------)')
if (!$pidSource.Success) { throw 'Production PID extraction boundary missing' }
$decisionStart = $source.IndexOf('    currentDecisionAvailable = true;')
if ($decisionStart -lt 0) { throw 'Production decision anchor missing' }
$decisionStart += '    currentDecisionAvailable = true;'.Length
$decisionEnd = $source.IndexOf('#if CM_ENABLE_LOGGING && CM_ENABLE_VERBOSE_LOGGING', $decisionStart)
if ($decisionStart -lt 0 -or $decisionEnd -le $decisionStart) { throw 'Production command extraction boundary missing' }
$command = $source.Substring($decisionStart, $decisionEnd - $decisionStart)
if (!$command.Contains('issueInverterCommand') -or !$command.Contains('currentDecision.pvOnly')) { throw 'Production command extraction incomplete' }
$issueSource = [regex]::Match($source, '(?s)static void issueInverterCommand\(int watts, int maximumW, int outputOffsetW\)\r?\n\{.*?(?=static void processRS485Tick\(\)\r?\n\{)')
if (!$issueSource.Success -or !$issueSource.Value.Contains('correctOutputCommand')) { throw 'Production issue function extraction missing' }
$heldSource = [regex]::Match($source, 'configuredMax, ([^\r\n]+), nowMs\);')
if (!$heldSource.Success) { throw 'Production held-command extraction missing' }
$heldFunction = 'static int heldControlCommand() { return ' + $heldSource.Groups[1].Value + '; }'
$header = Join-Path $repo '.pio/current_control_application.h'
[IO.File]::WriteAllText($header, $pidSource.Value + [Environment]::NewLine + $issueSource.Value + [Environment]::NewLine + $heldFunction + [Environment]::NewLine + 'static void applyDecision() {' + $command + '}' + [Environment]::NewLine)
$applicationOut = Join-Path $repo '.pio/current_control_application_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio') (Join-Path $repo 'test/host/current_control_application_test.cpp') -o $applicationOut
if ($LASTEXITCODE -ne 0) { throw 'Current control application compilation failed' }
& $applicationOut
if ($LASTEXITCODE -ne 0) { throw 'Current control application tests failed' }
