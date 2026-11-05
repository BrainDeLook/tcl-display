param([string]$Zig = 'zig', [string]$OutputDirectory = "$env:TEMP\TCL-Display-tests")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$testExe = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) 'display_test.exe'
& $Zig cc -Os -Wall -Wextra -o $testExe (Join-Path $PSScriptRoot 'tests/display_test.c') -luser32 -lws2_32 -lbcrypt -lshell32
if ($LASTEXITCODE -ne 0) { throw 'Native test compilation failed.' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }

$desktopTest = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) 'desktop_test.exe'
& $Zig cc -Os -Wall -Wextra -o $desktopTest (Join-Path $PSScriptRoot 'tests/desktop_test.c') -luser32 -lshell32 -lole32 -loleaut32 -luuid -lshlwapi -lshcore
if ($LASTEXITCODE -ne 0) { throw 'Desktop test compilation failed.' }
& $desktopTest
if ($LASTEXITCODE -ne 0) { throw 'Desktop tests failed.' }
