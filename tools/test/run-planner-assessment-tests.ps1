$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo '.pio/planner_assessment_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -DARDUINOJSON_POOL_CAPACITY=128 -Wall -Wextra -Werror -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/planner_assessment_test.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw 'Planner assessment host compilation failed' }
& $out
if ($LASTEXITCODE -ne 0) { throw 'Planner assessment host tests failed' }

# Compile the actual pre-mutation HTTP gate and missing/stale input hold.
$source = [IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
$http = [regex]::Match($source, '(?s)static bool shadowWriteConfig\([^\r\n]+\) \{.*?\r?\n\}')
$hold = [regex]::Match($source, '(?s)    if\(control::priceInputRequiredForMode\([^\r\n]+\) && !futurePrices.priceFresh\(utc\)\) \{.*?\r?\n    \}')
if (!$http.Success -or !$hold.Success) { throw 'Actual shadow HTTP/source gate extraction missing' }
if ($hold.Index -lt $source.IndexOf('modeDecision = nextModeDecision;')) { throw 'Shadow feed hold must follow mode policy selection' }
[IO.File]::WriteAllText((Join-Path $repo '.pio/shadow_http_gate.h'), $http.Value + [Environment]::NewLine + 'static void applyShadowSourceGate(uint32_t utc) {' + $hold.Value + '}')
$common = @('-std=c++17','-Wall','-Wextra','-Werror','-I',(Join-Path $repo 'src'))
& 'C:/Program Files/LLVM/bin/clang++.exe' @common (Join-Path $repo 'test/host/shadow_isolation_test.cpp') -o (Join-Path $repo '.pio/shadow_isolation_test.exe')
if ($LASTEXITCODE -ne 0) { throw 'Shadow routing compilation failed' }
& (Join-Path $repo '.pio/shadow_isolation_test.exe')
if ($LASTEXITCODE -ne 0) { throw 'Shadow routing tests failed' }
& 'C:/Program Files/LLVM/bin/clang++.exe' @common -DARDUINOJSON_POOL_CAPACITY=128 -DSIL_PLANNER_SHADOW=1 -DSIL_MOCK_TRANSPORT=1 -I (Join-Path $repo '.pio') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/shadow_http_gate_test.cpp') -o (Join-Path $repo '.pio/shadow_http_gate_test.exe')
if ($LASTEXITCODE -ne 0) { throw 'Shadow production gate compilation failed' }
& (Join-Path $repo '.pio/shadow_http_gate_test.exe')
if ($LASTEXITCODE -ne 0) { throw 'Shadow production gate tests failed' }
# Compile actual application wrappers in both profiles; deferred plan attempts stay pending.
$start=$source.IndexOf('// Application publication pacing;')
$end=$source.IndexOf('static String buildPlanningStatePayload()', $start)
if($start-lt 0-or $end-lt $start){throw 'Shadow publication wrapper extraction missing'}
[IO.File]::WriteAllText((Join-Path $repo '.pio/shadow_publish_wrappers.h'),$source.Substring($start,$end-$start))
foreach($plan in @('dischargeBudget','chargePlan')) {
 if($source -notmatch ('if\(!deferred\)'+$plan+'\.published\(sent, nowMs\);')){throw 'Deferred plan incorrectly acknowledges a publish attempt'}
}
foreach($profile in @('normal','shadow','trace')) {
 $profileFlags=@();if($profile-ne 'normal'){$profileFlags=@('-DSIL_PLANNER_SHADOW=1','-DSIL_MOCK_TRANSPORT=1')}
 if($profile-eq 'trace'){$profileFlags+='-DSIL_SHADOW_STAGE_TRACE=1'}
 $pacingOut=Join-Path $repo ('.pio/shadow_publish_pacing_'+$profile+'.exe')
 & 'C:/Program Files/LLVM/bin/clang++.exe' @common @profileFlags -I (Join-Path $repo '.pio') (Join-Path $repo 'test/host/shadow_publish_pacing_test.cpp') -o $pacingOut
 if($LASTEXITCODE-ne 0){throw 'App pacing compilation failed'}
 & $pacingOut
 if($LASTEXITCODE-ne 0){throw 'App pacing regression failed'}
}
# Diagnostic-only stage trace must stay silent unless a TX failure counter changes.
$stage=[regex]::Match($source,'(?s)static void shadowTraceStage\([^\r\n]+\) \{.*?\r?\n\}')
$stageHeader='';if($stage.Success){$stageHeader='#define SHADOW_STAGE_TRACE_PRESENT 1'+[Environment]::NewLine+$stage.Value}
[IO.File]::WriteAllText((Join-Path $repo '.pio/shadow_stage_trace.h'),$stageHeader)
$stageOut=Join-Path $repo '.pio/shadow_stage_trace_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' @common -I (Join-Path $repo '.pio') (Join-Path $repo 'test/host/shadow_stage_trace_test.cpp') -o $stageOut
if($LASTEXITCODE-ne 0){throw 'Stage trace compilation failed'}
& $stageOut
if($LASTEXITCODE-ne 0){throw 'Stage trace regression failed'}
# A shadow profile without the hard hardware mock must be rejected by the compiler.
$ErrorActionPreference = 'Continue'
$negative = & 'C:/Program Files/LLVM/bin/clang++.exe' @common -DSIL_PLANNER_SHADOW=1 -fsyntax-only (Join-Path $repo 'test/host/shadow_isolation_test.cpp') 2>&1
$negativeStatus = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($negativeStatus -eq 0 -or ($negative -join [Environment]::NewLine) -notmatch 'requires hard mocked hardware transport') { throw 'Missing hard-mock compile gate' }
Write-Output '[MOCKED!] Shadow without hard hardware mock is rejected at compile time'
# The expected negative compilation must not leak a failure exit status to callers.
$global:LASTEXITCODE = 0
