; Build with tools/package.py. The payload must contain no portable.ini or user data.
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif

#if FileExists(PayloadDir + "\\portable.ini")
  #error Standard-mode payload must not contain portable.ini
#endif

[Setup]
AppId={{989C75F7-5337-4A9F-B50D-5F901FF1A43B}
AppName=Darkeye
AppVersion={#AppVersion}
AppPublisher=Darkeye
DefaultDirName={localappdata}\Programs\Darkeye
DefaultGroupName=Darkeye
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Darkeye-{#AppVersion}-windows-x64-setup
UninstallDisplayIcon={app}\Darkeye.exe
SetupIconFile=..\resources\logo.ico
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "portable.ini,data\*"

[Icons]
Name: "{group}\Darkeye"; Filename: "{app}\Darkeye.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Darkeye"; Filename: "{app}\Darkeye.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Darkeye.exe"; Description: "Launch Darkeye"; Flags: nowait postinstall skipifsilent

[Code]
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if FileExists(ExpandConstant('{app}\portable.ini')) or
     DirExists(ExpandConstant('{app}\data')) then
    Result := 'This directory contains a portable or legacy Darkeye installation. Choose a different directory to preserve its data.';
end;

// No UninstallDelete rule: user databases, images and settings are retained.
