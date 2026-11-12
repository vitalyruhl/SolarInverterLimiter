$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=Join-Path $repo '.pio/relative_tiers_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -DARDUINOJSON_POOL_CAPACITY=128 -DSIL_PLANNER_SHADOW=1 -DSIL_MOCK_TRANSPORT=1 -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/relative_tiers_test.cpp') -o $out
if($LASTEXITCODE-ne 0){throw 'Relative tiers host compile failed'}
& $out
if($LASTEXITCODE-ne 0){throw 'Relative tiers regression failed'}

# Compile actual main order/bindings; schedule positivity must not depend on a BMS-backed new plan.
$source=[IO.File]::ReadAllText((Join-Path $repo 'src/main.cpp'))
$binding=[regex]::Match($source,'(?s)    float relativeRawPrice=0;\r?\n    inputs.relativePricePositive=[^\r\n]+;')
$mode=[regex]::Match($source,'(?s)    modeInputs.automaticSourceGrace = currentDecision.sourceGrace;.*?        if \(!modeDecision\.automaticPvFallback\) currentControl\.resetTransient\(\);\r?\n    \}')
$hold=[regex]::Match($source,'(?s)    if\(control::priceInputRequiredForMode\([^\r\n]+\) && !futurePrices.priceFresh\(utc\)\) \{.*?\r?\n    \}')
if(!$binding.Success-or !$mode.Success-or !$hold.Success){throw 'Actual relative main binding/order extraction missing'}
$header='static void bindActualRelativePrice(uint32_t utc,bool priceFresh,float measuredPrice){'+$binding.Value+'}'+[Environment]::NewLine+'static void applyActualRelativeMode(){'+$mode.Value+'}'+[Environment]::NewLine+'static void applyActualRelativeFeedHold(uint32_t utc){'+$hold.Value+'}'
[IO.File]::WriteAllText((Join-Path $repo '.pio/relative_control_bindings.h'),$header)
$out=Join-Path $repo '.pio/relative_control_integration_test.exe'
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -DARDUINOJSON_POOL_CAPACITY=128 -DSIL_PLANNER_SHADOW=1 -DSIL_MOCK_TRANSPORT=1 -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo 'test/host/relative_control_integration_test.cpp') -o $out
if($LASTEXITCODE-ne 0){throw 'Relative control integration compile failed'}
& $out
if($LASTEXITCODE-ne 0){throw 'Relative control integration failed'}

foreach($test in @('relative_tiers_test','relative_control_integration_test','application_profile_test')) {
 foreach($profile in @('normal','shadow')) {
  $flags=@();if($profile-eq 'shadow'){$flags=@('-DSIL_PLANNER_SHADOW=1','-DSIL_MOCK_TRANSPORT=1')}
  $target=Join-Path $repo ('.pio/'+$test+'_'+$profile+'.exe')
  & 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -DARDUINOJSON_POOL_CAPACITY=128 @flags -I (Join-Path $repo 'src') -I (Join-Path $repo '.pio') -I (Join-Path $repo '.pio/libdeps/usb/ArduinoJson/src') (Join-Path $repo ('test/host/'+$test+'.cpp')) -o $target
  if($LASTEXITCODE-ne 0){throw ('Profile compilation failed: '+$test+' '+$profile)}
  & $target
  if($LASTEXITCODE-ne 0){throw ('Profile regression failed: '+$test+' '+$profile)}
 }
}
