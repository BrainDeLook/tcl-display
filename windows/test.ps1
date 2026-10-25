param([string]$Zig = 'zig', [string]$OutputDirectory = "$env:TEMP\TCL-Display-tests")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$testExe = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) 'display_test.exe'
& $Zig cc -Os -Wall -Wextra -o $testExe (Join-Path $PSScriptRoot 'tests/display_test.c') -luser32 -lws2_32 -lbcrypt
if ($LASTEXITCODE -ne 0) { throw 'Native test compilation failed.' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
