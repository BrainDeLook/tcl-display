param([Parameter(Mandatory)][string]$Installer,[Parameter(Mandatory)][string]$TestRoot)
$ErrorActionPreference = 'Stop'
$rootPath = [IO.Path]::GetFullPath($TestRoot)
$installPath = Join-Path $rootPath 'installed-app'
$configPath = Join-Path $rootPath 'user-data'
New-Item -ItemType Directory -Force -Path $rootPath | Out-Null
$oldConfig = Join-Path $rootPath 'portable-agent.ini'
$fixture = "[agent]`nmonitor=synthetic-test-screen`nkey=" + ('a' * 64) + "`nport=18789`n[display]`nmake_primary=1`n"
[IO.File]::WriteAllText($oldConfig, $fixture)
function Invoke-Setup {
    $params = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/NOICONS',
        ('/DIR="' + $installPath + '"'), ('/TESTCONFIGDIR="' + $configPath + '"'),
        ('/IMPORTCONFIG="' + $oldConfig + '"'), ('/LOG="' + (Join-Path $rootPath 'setup.log') + '"'))
    $process = Start-Process -FilePath $Installer -ArgumentList $params -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installer exited $($process.ExitCode)" }
}
Invoke-Setup
if (!(Test-Path (Join-Path $installPath 'TCL-Display.exe'))) { throw 'Application was not installed' }
$config = Join-Path $configPath 'agent.ini'
if ([IO.File]::ReadAllText($config) -ne $fixture) { throw 'Portable migration changed config' }
# Updates preserve existing data even when a different import file is specified.
[IO.File]::WriteAllText($oldConfig, '[agent] changed=do-not-import')
Invoke-Setup
if ([IO.File]::ReadAllText($config) -ne $fixture) { throw 'Update overwrote user settings' }
$uninstall = Join-Path $installPath 'unins000.exe'
$process = Start-Process -FilePath $uninstall -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Uninstaller exited $($process.ExitCode)" }
for ($attempt=0; $attempt -lt 20 -and (Test-Path (Join-Path $installPath 'TCL-Display.exe')); $attempt++) { Start-Sleep -Milliseconds 250 }
if (Test-Path (Join-Path $installPath 'TCL-Display.exe')) { throw 'Uninstaller left the application' }
if ([IO.File]::ReadAllText($config) -ne $fixture) { throw 'Uninstaller removed user settings' }
'Installer tests passed: install, portable migration, update preservation, uninstall; no app/display launched.'
