#ifndef AppVersion
#define AppVersion "1.6.0"
#endif
#ifndef BuildDir
#define BuildDir "..\..\dist\TCL-Display-Windows"
#endif
#ifndef OutputDir
#define OutputDir "..\..\dist"
#endif
[Setup]
#ifdef TestBuild
AppId=TCLDisplayInstallerTest
AppName=TCL Display Installer Test
DefaultDirName={localappdata}\Programs\TCL Display Installer Test
AppMutex=Local\TCLDisplayAppBehaviorTest
#else
AppId={{1B61B93C-DF53-4A48-9A19-B40B62A9D216}
AppName=TCL Display
DefaultDirName={localappdata}\Programs\TCL Display
AppMutex=Local\TCLDisplayAppGUI
#endif
AppVersion={#AppVersion}
AppPublisher=BrainDeLook
AppPublisherURL=https://github.com/BrainDeLook/tcl-display
AppSupportURL=https://github.com/BrainDeLook/tcl-display/issues
AppUpdatesURL=https://github.com/BrainDeLook/tcl-display/releases/latest
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DefaultGroupName=TCL Display
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=TCL-Display-Setup
SetupIconFile=..\src\app.ico
UninstallDisplayIcon={app}\TCL-Display.exe
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
CloseApplicationsFilter=TCL-Display.exe
RestartApplications=no
LicenseFile=..\..\LICENSE
[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; Flags: unchecked
[Files]
Source: "{#BuildDir}\TCL-Display.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\НАСТРОЙКА-HA.txt"; DestDir: "{app}"; Flags: ignoreversion
[Icons]
Name: "{group}\TCL Display"; Filename: "{app}\TCL-Display.exe"
Name: "{group}\Настройка Home Assistant"; Filename: "{app}\НАСТРОЙКА-HA.txt"
Name: "{autodesktop}\TCL Display"; Filename: "{app}\TCL-Display.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\TCL-Display.exe"; Description: "Открыть TCL Display"; Flags: nowait postinstall skipifsilent
[Code]
var
  ImportPage: TInputFileWizardPage;
  ConfigDir: String;

function RunExecutable(Command: String): String;
var P: Integer;
begin
  if Copy(Command, 1, 1) = '"' then begin
    Delete(Command, 1, 1);
    P := Pos('"', Command);
  end else P := Pos(' ', Command);
  if P > 0 then Result := Copy(Command, 1, P - 1) else Result := Command;
end;

procedure InitializeWizard;
var Command, Candidate: String;
begin
#ifdef TestBuild
  ConfigDir := ExpandConstant('{param:TESTCONFIGDIR|{localappdata}\TCL Display Installer Test}');
#else
  ConfigDir := ExpandConstant('{localappdata}\TCL Display');
#endif
  ImportPage := CreateInputFilePage(wpSelectDir, 'Существующие настройки',
    'Перенос подключения из старой portable-версии',
    'Если раньше использовалась portable-версия, выберите её agent.ini. ' +
    'Ключ подключения, выбранный экран и настройки будут сохранены. Для новой установки оставьте поле пустым.');
  ImportPage.Add('Файл agent.ini (необязательно):', 'Файл настроек|agent.ini', '.ini');
#ifndef TestBuild
  if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'TCLDisplayApp', Command) then begin
    Candidate := ExtractFileDir(RunExecutable(Command)) + '\agent.ini';
    if FileExists(Candidate) then ImportPage.Values[0] := Candidate;
  end;
#endif
  Candidate := ExpandConstant('{param:IMPORTCONFIG|}');
  if Candidate <> '' then ImportPage.Values[0] := Candidate;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = ImportPage.ID) and FileExists(ConfigDir + '\agent.ini');
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not FileExists(ConfigDir + '\agent.ini') and (ImportPage.Values[0] <> '') then begin
    if not FileExists(ImportPage.Values[0]) then begin Result := 'Выбранный agent.ini не найден.'; exit; end;
    if not ForceDirectories(ConfigDir) or not FileCopy(ImportPage.Values[0], ConfigDir + '\agent.ini', True) then
      Result := 'Не удалось перенести настройки в AppData. Установка остановлена; исходный файл сохранён.';
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Command: String;
begin
#ifndef TestBuild
  if CurStep = ssPostInstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'TCLDisplayApp', Command) then
      RegWriteStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'TCLDisplayApp',
        '"' + ExpandConstant('{app}\TCL-Display.exe') + '" --tray');
#endif
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Command: String;
begin
#ifndef TestBuild
  if CurUninstallStep = usUninstall then
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'TCLDisplayApp', Command) then
      if CompareText(RunExecutable(Command), ExpandConstant('{app}\TCL-Display.exe')) = 0 then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'TCLDisplayApp');
#endif
  { Keep user settings on uninstall so a reinstall does not break HA pairing. }
end;
