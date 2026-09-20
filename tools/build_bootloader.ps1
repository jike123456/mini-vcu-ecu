[CmdletBinding()]
param(
    [string]$ArmccBin = 'D:\Keil_v5\ARM\ARMCC\bin'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$bootRoot = Join-Path $repoRoot 'firmware\Bootloader'
$appRoot = Join-Path $repoRoot 'firmware\KaoYa_Project'
$output = Join-Path $bootRoot 'MDK-ARM\Build'

$armcc = Join-Path $ArmccBin 'armcc.exe'
$armasm = Join-Path $ArmccBin 'armasm.exe'
$armlink = Join-Path $ArmccBin 'armlink.exe'
$fromelf = Join-Path $ArmccBin 'fromelf.exe'
foreach ($tool in @($armcc, $armasm, $armlink, $fromelf)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "ARM Compiler tool not found: $tool"
    }
}

New-Item -ItemType Directory -Path $output -Force | Out-Null

$deviceInclude = Join-Path $appRoot 'Drivers\CMSIS\Device\ST\STM32F4xx\Include'
$cmsisInclude = Join-Path $appRoot 'Drivers\CMSIS\Include'
$bootInclude = Join-Path $bootRoot 'Inc'
$sharedInclude = Join-Path $repoRoot 'firmware\Shared'

$commonCompile = @(
    '--c99', '--cpu', 'Cortex-M4.fp.sp', '--fpu', 'FPv4-SP',
    '--apcs=/interwork', '--split_sections', '--library_type=microlib',
    '-O1', '-g', '-DSTM32F407xx',
    "-I$deviceInclude", "-I$cmsisInclude", "-I$bootInclude", "-I$sharedInclude",
    '-c'
)

$objects = @()
$sources = @(
    (Join-Path $bootRoot 'Src\main.c'),
    (Join-Path $bootRoot 'Src\boot_image.c'),
    (Join-Path $bootRoot 'Src\boot_diag.c'),
    (Join-Path $appRoot 'Core\Src\system_stm32f4xx.c')
)
foreach ($source in $sources) {
    $object = Join-Path $output (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
    & $armcc @commonCompile $source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}

$startupObject = Join-Path $output 'startup_stm32f407xx.o'
$startup = Join-Path $appRoot 'MDK-ARM\startup_stm32f407xx.s'
& $armasm '--cpu' 'Cortex-M4.fp.sp' '--fpu' 'FPv4-SP' '--apcs=/interwork' `
    '--pd' '__MICROLIB SETA 1' '-g' '-o' $startupObject $startup
if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
$objects += $startupObject

$scatter = Join-Path $bootRoot 'MDK-ARM\bootloader.sct'
$axf = Join-Path $output 'KaoYa_Bootloader.axf'
$map = Join-Path $output 'KaoYa_Bootloader.map'
& $armlink '--cpu' 'Cortex-M4.fp.sp' '--fpu' 'FPv4-SP' `
    '--library_type=microlib' '--strict' '--summary_stderr' '--map' '--list' $map `
    '--scatter' $scatter '--output' $axf @objects
if ($LASTEXITCODE -ne 0) { throw 'Bootloader link failed' }

$hex = Join-Path $output 'KaoYa_Bootloader.hex'
$bin = Join-Path $output 'KaoYa_Bootloader.bin'
& $fromelf '--i32combined' '--output' $hex $axf
if ($LASTEXITCODE -ne 0) { throw 'Bootloader HEX generation failed' }
& $fromelf '--bin' '--output' $bin $axf
if ($LASTEXITCODE -ne 0) { throw 'Bootloader BIN generation failed' }

Write-Host 'Bootloader build passed.'
Write-Host "AXF: $axf"
Write-Host "HEX: $hex"
Write-Host "BIN: $bin"
