param([string] $BuildRoot = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$BuildRoot) { $BuildRoot = Join-Path $repo '.pio/operating-modes-5.6.0' }
$core = if ($env:PLATFORMIO_CORE_DIR) { $env:PLATFORMIO_CORE_DIR } else {
    Join-Path (Split-Path $repo -Parent) '.platformio-codex'
}
$bin = Join-Path $core 'packages/toolchain-xtensa-esp32/bin'
$objdump = Join-Path $bin 'xtensa-esp32-elf-objdump.exe'
$nm = Join-Path $bin 'xtensa-esp32-elf-nm.exe'
if (!(Test-Path -LiteralPath $objdump) -or !(Test-Path -LiteralPath $nm)) {
    throw 'Xtensa binutils are unavailable in the selected PlatformIO Core.'
}

foreach ($profile in @('usb', 'planner_shadow', 'planner_shadow_trace')) {
    $elf = Join-Path $BuildRoot "$profile/firmware.elf"
    if (!(Test-Path -LiteralPath $elf)) { throw "Missing firmware ELF: $elf" }
    $app = @(& $objdump -d --disassemble=app_main $elf)
    if ($LASTEXITCODE -ne 0) { throw "app_main disassembly failed: $profile" }
    $symbols = @(& $nm $elf)
    if ($LASTEXITCODE -ne 0) { throw "Symbol inspection failed: $profile" }
    $wrappedCalls = @($app | Where-Object { $_ -match '\bcall(?:0|4|8|12)\b.*<__wrap_nvs_flash_init>' })
    $directCalls = @($app | Where-Object { $_ -match '\bcall(?:0|4|8|12)\b.*<nvs_flash_init>' })
    $wrapperSymbols = @($symbols | Where-Object { $_ -match '\b__wrap_nvs_flash_init$' })
    if ($profile -eq 'usb') {
        if ($directCalls.Count -lt 1 -or $wrappedCalls.Count -ne 0 -or $wrapperSymbols.Count -ne 0) {
            throw 'USB startup does not preserve the unwrapped Core NVS path.'
        }
        Write-Output "PASS $profile`: Core NVS calls remain direct ($($directCalls.Count)); no shadow wrapper symbol"
        $directCalls | ForEach-Object { Write-Output $_.Trim() }
        continue
    }
    if ($wrappedCalls.Count -lt 1 -or $directCalls.Count -ne 0 -or $wrapperSymbols.Count -ne 1) {
        throw "Core startup is not routed exclusively through the shadow wrapper: $profile"
    }
    $wrapper = @(& $objdump -d --disassemble=__wrap_nvs_flash_init $elf)
    if ($LASTEXITCODE -ne 0) { throw "Wrapper disassembly failed: $profile" }
    $realCalls = @($wrapper | Where-Object { $_ -match '\bcall(?:0|4|8|12)\b.*<nvs_flash_init>' })
    if ($realCalls.Count -ne 1) { throw "Shadow wrapper does not call the real NVS initializer once: $profile" }
    Write-Output "PASS $profile`: Core NVS calls route through shadow wrapper ($($wrappedCalls.Count)); wrapper calls real initializer"
    $wrappedCalls | ForEach-Object { Write-Output $_.Trim() }
    $realCalls | ForEach-Object { Write-Output $_.Trim() }
}
Write-Output 'ELF inspection checks link targets only; it does not exercise physical boot or NVS writes.'
