[CmdletBinding()]
param(
    [string]$KeilUv4 = 'D:\Keil_v5\UV4\UV4.exe',
    [string]$PythonExecutable = 'D:\ProgramData\anaconda3\python.exe',
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $repoRoot 'firmware\KaoYa_Project\MDK-ARM'
$projectFile = Join-Path $projectDir 'KaoYa_Project.uvprojx'
$buildLog = Join-Path $projectDir 'automated-build.log'

if (-not (Test-Path -LiteralPath $KeilUv4 -PathType Leaf)) {
    throw "Keil UV4 not found: $KeilUv4"
}

if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    throw "Keil project not found: $projectFile"
}

if (Test-Path -LiteralPath $buildLog) {
    Remove-Item -LiteralPath $buildLog -Force
}

$arguments = @(
    '-r', $projectFile,
    '-j0',
    '-t', 'KaoYa_Project',
    '-o', $buildLog
)

Start-Process -FilePath $KeilUv4 -ArgumentList $arguments -WindowStyle Hidden | Out-Null

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
do {
    Start-Sleep -Milliseconds 500
    if (Test-Path -LiteralPath $buildLog) {
        $content = Get-Content -LiteralPath $buildLog -Raw
        if ($content -match '\d+ Error\(s\), \d+ Warning\(s\)') {
            break
        }
    }
} while ((Get-Date) -lt $deadline)

if (-not (Test-Path -LiteralPath $buildLog)) {
    throw "Build timed out before Keil created the log: $buildLog"
}

$content = Get-Content -LiteralPath $buildLog -Raw
$summary = [regex]::Match($content, '"[^\r\n]+\.axf" - \d+ Error\(s\), \d+ Warning\(s\)\.').Value
if ($summary) {
    Write-Host $summary
}

if ($content -notmatch ' - 0 Error\(s\), 0 Warning\(s\)\.') {
    Write-Host "Build log: $buildLog"
    exit 1
}

Write-Host 'Firmware rebuild passed.'
Write-Host "Build log: $buildLog"

$fromelf = Join-Path (Split-Path -Parent $KeilUv4) '..\ARM\ARMCC\bin\fromelf.exe'
$fromelf = [IO.Path]::GetFullPath($fromelf)
$outputDir = Join-Path $projectDir 'KaoYa_Application'
$axf = Join-Path $outputDir 'KaoYa_Application.axf'
$bin = Join-Path $outputDir 'KaoYa_Application.bin'
$package = Join-Path $outputDir 'KaoYa_Application.kya'
if (-not (Test-Path -LiteralPath $fromelf -PathType Leaf)) {
    throw "fromelf not found: $fromelf"
}
& $fromelf '--bin' '--output' $bin $axf
if ($LASTEXITCODE -ne 0) { throw 'Application BIN generation failed' }
if (-not (Test-Path -LiteralPath $PythonExecutable -PathType Leaf)) {
    throw "Python not found: $PythonExecutable"
}
& $PythonExecutable (Join-Path $repoRoot 'tools\boot_image.py') pack `
    --input $bin --output $package --version '0.6.0'
if ($LASTEXITCODE -ne 0) { throw 'Application package validation failed' }
