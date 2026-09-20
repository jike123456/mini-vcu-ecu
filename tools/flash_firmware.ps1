[CmdletBinding()]
param(
    [string]$KeilUv4 = 'D:\Keil_v5\UV4\UV4.exe',
    [int]$TimeoutSeconds = 60,
    [switch]$FactoryProvisioning
)

$ErrorActionPreference = 'Stop'

if (-not $FactoryProvisioning) {
    throw 'Application is relocated and requires a matching manifest. Use tools\flash_factory.ps1.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $repoRoot 'firmware\KaoYa_Project\MDK-ARM'
$projectFile = Join-Path $projectDir 'KaoYa_Project.uvprojx'
$flashLog = Join-Path $projectDir 'automated-flash.log'

if (-not (Test-Path -LiteralPath $KeilUv4 -PathType Leaf)) {
    throw "Keil UV4 not found: $KeilUv4"
}
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    throw "Keil project not found: $projectFile"
}
if (Test-Path -LiteralPath $flashLog) {
    Remove-Item -LiteralPath $flashLog -Force
}

$arguments = @(
    '-f', $projectFile,
    '-j0',
    '-t', 'KaoYa_Project',
    '-o', $flashLog
)

$process = Start-Process -FilePath $KeilUv4 -ArgumentList $arguments -PassThru -WindowStyle Hidden
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 250
    $process.Refresh()
}

if (-not $process.HasExited) {
    throw "Keil flash timed out after $TimeoutSeconds seconds. Check whether another uVision window is busy."
}
if (-not (Test-Path -LiteralPath $flashLog)) {
    throw "Keil did not create the flash log: $flashLog"
}

$content = Get-Content -LiteralPath $flashLog -Raw
Write-Host $content
if (($content -match 'Flash Download failed|Programming Failed|RDDI-DAP Error|Error:') -or
    ($content -notmatch 'Flash Load finished')) {
    Write-Host "Flash log: $flashLog"
    exit 1
}

Write-Host 'Firmware flash passed.'
Write-Host "Flash log: $flashLog"
