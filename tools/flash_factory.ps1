[CmdletBinding()]
param(
    [string]$GnuArmBin = 'C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin',
    [string]$ArmccBin = 'D:\Keil_v5\ARM\ARMCC\bin',
    [string]$PythonExecutable = 'D:\ProgramData\anaconda3\python.exe'
)

$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$applicationOutput = [IO.Path]::GetFullPath((Join-Path $repoRoot `
    'firmware\KaoYa_Project\MDK-ARM\KaoYa_Application'))
$applicationAxf = Join-Path $applicationOutput 'KaoYa_Application.axf'
$applicationPackage = Join-Path $applicationOutput 'KaoYa_Application.kya'
$applicationBin = Join-Path $applicationOutput 'KaoYa_Application.bin'
$bootloaderBin = [IO.Path]::GetFullPath((Join-Path $repoRoot `
    'firmware\Bootloader\MDK-ARM\Build\KaoYa_Bootloader.bin'))
$factoryDir = [IO.Path]::GetFullPath((Join-Path $repoRoot 'firmware\factory'))
$factoryHex = Join-Path $factoryDir 'KaoYa_Factory.hex'
$factoryAxf = Join-Path $factoryDir 'KaoYa_Factory.axf'
$manifestBin = Join-Path $factoryDir 'KaoYa_Manifest.bin'
$applicationObject = Join-Path $factoryDir 'KaoYa_Application.factory.o'
$manifestObject = Join-Path $factoryDir 'KaoYa_Manifest.factory.o'
$factoryScatter = Join-Path $factoryDir 'factory.sct'
$backupAxf = Join-Path $applicationOutput 'KaoYa_Application.debug-backup.axf'
$objcopy = Join-Path $GnuArmBin 'arm-none-eabi-objcopy.exe'
$linker = Join-Path $ArmccBin 'armlink.exe'

foreach ($path in @($applicationOutput, $factoryDir)) {
    if (-not $path.StartsWith($repoRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to write outside repository: $path"
    }
}
foreach ($tool in @($objcopy, $linker)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "GNU Arm tool not found: $tool"
    }
}
if (-not (Test-Path -LiteralPath $PythonExecutable -PathType Leaf)) {
    throw "Python not found: $PythonExecutable"
}

& (Join-Path $PSScriptRoot 'build_bootloader.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Bootloader build failed' }
& (Join-Path $PSScriptRoot 'build_firmware.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Application build failed' }

New-Item -ItemType Directory -Path $factoryDir -Force | Out-Null
& $PythonExecutable (Join-Path $PSScriptRoot 'boot_image.py') factory `
    --bootloader $bootloaderBin --application $applicationPackage `
    --output $factoryHex --manifest-output $manifestBin
if ($LASTEXITCODE -ne 0) { throw 'Factory image validation failed' }

& $objcopy -I binary -O elf32-littlearm -B arm `
    --rename-section '.data=.factory_application,alloc,load,readonly,code,contents' `
    $applicationBin $applicationObject
if ($LASTEXITCODE -ne 0) { throw 'Application object conversion failed' }
& $objcopy -I binary -O elf32-littlearm -B arm `
    --rename-section '.data=.factory_manifest,alloc,load,readonly,data,contents' `
    $manifestBin $manifestObject
if ($LASTEXITCODE -ne 0) { throw 'Manifest object conversion failed' }
$bootBuild = Join-Path $repoRoot 'firmware\Bootloader\MDK-ARM\Build'
$bootObjects = @(
    (Join-Path $bootBuild 'main.o'),
    (Join-Path $bootBuild 'boot_image.o'),
    (Join-Path $bootBuild 'boot_diag.o'),
    (Join-Path $bootBuild 'system_stm32f4xx.o'),
    (Join-Path $bootBuild 'startup_stm32f407xx.o')
)
& $linker --cpu Cortex-M4.fp.sp --fpu FPv4-SP --library_type=microlib `
    --strict --no_remove --scatter $factoryScatter --output $factoryAxf `
    @bootObjects $applicationObject $manifestObject
if ($LASTEXITCODE -ne 0) { throw 'Factory executable AXF link failed' }

if (-not (Test-Path -LiteralPath $applicationAxf -PathType Leaf)) {
    throw "Application AXF not found: $applicationAxf"
}
Copy-Item -LiteralPath $applicationAxf -Destination $backupAxf -Force
try {
    Copy-Item -LiteralPath $factoryAxf -Destination $applicationAxf -Force
    & (Join-Path $PSScriptRoot 'flash_firmware.ps1') -FactoryProvisioning
    if ($LASTEXITCODE -ne 0) { throw 'Factory SWD flash failed' }
}
finally {
    Copy-Item -LiteralPath $backupAxf -Destination $applicationAxf -Force
    Remove-Item -LiteralPath $backupAxf -Force
}

Write-Host 'Factory SWD flash passed; application debug AXF restored.'
