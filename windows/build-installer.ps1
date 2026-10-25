param(
    [string]$Iscc = 'ISCC.exe',
    [string]$BuildDirectory = 'dist\TCL-Display-Windows',
    [string]$OutputDirectory = 'dist',
    [switch]$TestBuild
)
$ErrorActionPreference = 'Stop'
$build = [IO.Path]::GetFullPath($BuildDirectory)
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$compilerArgs = @('/Qp', "/DBuildDir=$build", "/DOutputDir=$output")
if ($TestBuild) { $compilerArgs += '/DTestBuild' }
& $Iscc @compilerArgs (Join-Path $PSScriptRoot 'installer\setup.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
