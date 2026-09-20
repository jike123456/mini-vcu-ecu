[CmdletBinding()]
param(
    [string]$Gcc = 'C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin\arm-none-eabi-gcc.exe'
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$appRoot = Join-Path $repoRoot 'firmware\KaoYa_Project'
$bootRoot = Join-Path $repoRoot 'firmware\Bootloader'

if (-not (Test-Path -LiteralPath $Gcc -PathType Leaf)) {
    throw "ARM GNU GCC not found: $Gcc"
}

# Scope is intentionally limited to code owned by this project. CubeMX output,
# HAL, CMSIS, FreeRTOS, LCD legacy code and the vendor IMU register driver are
# dependencies, not findings owned by the Mini VCU application.
$applicationSources = @(
    'app_init.c', 'can_task.c', 'comm_task.c', 'command_broker.c',
    'control_task.c', 'dtc_manager.c', 'fault_manager.c', 'iwdg_task.c',
    'lcd_task.c', 'led_task.c', 'log_task.c', 'monitor_task.c', 'nvm_dtc.c',
    'safety_manager.c', 'sensor_task.c', 'uds_server.c', 'uplink_task.c',
    'vehicle_state.c'
) | ForEach-Object { Join-Path $appRoot "App\Src\$_" }

$bspSources = @(
    'battery.c', 'can_bus.c', 'capture.c', 'car.c', 'comm_parser.c',
    'encoder.c', 'motor.c', 'ms6dsv.c', 'ms6dsv_iic.c', 'pid.c',
    'protocol.c', 'uplink_proto.c', 'vehicle_can.c'
) | ForEach-Object { Join-Path $appRoot "Bsp\Src\$_" }

$bootSources = @(
    (Join-Path $bootRoot 'Src\boot_diag.c'),
    (Join-Path $bootRoot 'Src\boot_image.c'),
    (Join-Path $bootRoot 'Src\main.c')
)
$sources = @($applicationSources + $bspSources + $bootSources)

$missing = @($sources | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missing.Count -ne 0) {
    throw "Static-analysis source is missing: $($missing -join ', ')"
}

$firstPartyIncludes = @(
    (Join-Path $PSScriptRoot 'static_analysis_stubs'),
    (Join-Path $appRoot 'Core\Inc'),
    (Join-Path $appRoot 'App\Inc'),
    (Join-Path $appRoot 'Bsp\Inc'),
    (Join-Path $bootRoot 'Inc'),
    (Join-Path $repoRoot 'firmware\Shared')
)
$systemIncludes = @(
    (Join-Path $appRoot 'Drivers\STM32F4xx_HAL_Driver\Inc'),
    (Join-Path $appRoot 'Drivers\STM32F4xx_HAL_Driver\Inc\Legacy'),
    (Join-Path $appRoot 'Middlewares\Third_Party\FreeRTOS\Source\include'),
    (Join-Path $appRoot 'Middlewares\Third_Party\FreeRTOS\Source\CMSIS_RTOS_V2'),
    (Join-Path $appRoot 'Drivers\CMSIS\Device\ST\STM32F4xx\Include'),
    (Join-Path $appRoot 'Drivers\CMSIS\Include')
)

$arguments = @(
    '-std=c11', '-fsyntax-only', '-fanalyzer', '-fdiagnostics-color=never',
    '-fno-diagnostics-show-caret', '-Wall', '-Wextra', '-Wshadow',
    '-Wconversion', '-Wsign-conversion', '-Wdouble-promotion', '-Wformat=2',
    '-Wundef', '-Wstrict-prototypes', '-Wmissing-prototypes',
    '-Wcast-align=strict', '-Wcast-qual', '-Wvla', '-Wswitch-enum',
    '-Wlogical-op', '-Wduplicated-cond', '-Wduplicated-branches',
    '-Wnull-dereference', '-Wpointer-arith', '-Wwrite-strings',
    '-Werror=implicit-function-declaration', '-Wno-unused-parameter',
    '-Wno-system-headers', '-mthumb', '-mcpu=cortex-m4',
    '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard', '-DUSE_HAL_DRIVER',
    '-DSTM32F407xx', '-DUSER_VECT_TAB_ADDRESS',
    '-DVECT_TAB_OFFSET=0x00020000U'
)
foreach ($include in $firstPartyIncludes) {
    $arguments += "-I$include"
}
foreach ($include in $systemIncludes) {
    $arguments += @('-isystem', $include)
}

$diagnostics = [Collections.Generic.List[string]]::new()
$failedFiles = [Collections.Generic.List[string]]::new()
foreach ($source in $sources) {
    $output = & $Gcc @arguments $source 2>&1 | ForEach-Object { $_.ToString() }
    foreach ($line in $output) { $diagnostics.Add($line) }
    if ($LASTEXITCODE -ne 0) {
        $failedFiles.Add([IO.Path]::GetFileName($source))
    }
}

$warnings = @($diagnostics | Where-Object { $_ -match ': warning:' })
$errors = @($diagnostics | Where-Object { $_ -match ': error:' })
$policyPattern = '\b(goto|malloc|calloc|realloc|free|strcpy|strcat|sprintf|gets)\s*\('
$policyHits = [Collections.Generic.List[string]]::new()
foreach ($source in $sources) {
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $source) {
        $lineNumber++
        if ($line -match $policyPattern) {
            $policyHits.Add("$source`:$lineNumber`: prohibited-call pattern: $line")
        }
    }
}

Write-Host "STATIC ANALYSIS: files=$($sources.Count) warnings=$($warnings.Count) errors=$($errors.Count) policy=$($policyHits.Count)"
foreach ($line in $diagnostics) { Write-Host $line }
foreach ($line in $policyHits) { Write-Host $line }

if ($warnings.Count -ne 0 -or $errors.Count -ne 0 -or
    $failedFiles.Count -ne 0 -or $policyHits.Count -ne 0) {
    Write-Error "STATIC ANALYSIS FAIL: warnings/errors or policy violations detected"
    exit 1
}

Write-Host 'STATIC ANALYSIS COMPLETE'
