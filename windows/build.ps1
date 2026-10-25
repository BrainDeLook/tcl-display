param([string]$Zig = 'zig', [string]$OutputDirectory = 'dist\TCL-Display-Windows')
$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path $PSScriptRoot 'src'
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
Push-Location $sourcePath
try {
    & $Zig rc /fo app.res app.rc
    if ($LASTEXITCODE -ne 0) { throw 'Resource build failed.' }
    & $Zig cc -Os -s -Wall -Wextra '-Wl,--subsystem,windows' -o (Join-Path $outputPath 'TCL-Display.exe') app.c app.res -luser32 -lws2_32 -lbcrypt -lcomctl32 -lshell32 -ladvapi32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'Application build failed.' }
} finally { Pop-Location }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'НАСТРОЙКА-HA.txt') -Destination $outputPath -Force
