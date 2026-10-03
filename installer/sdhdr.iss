; SD-HDR installer. Build first:
;   cmake --install build --config Release --prefix dist
; Then compile this script with Inno Setup 6.

#define AppName "SD-HDR"
#define AppVersion "1.0.0"
#define AppExe "SDHDR.exe"

[Setup]
AppId={{8F3A1C2E-6D58-4F0A-9B31-7C4E2A91B6D0}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=SD-HDR
DefaultDirName={autopf}\SD-HDR
DefaultGroupName=SD-HDR
OutputDir=..\dist
OutputBaseFilename=SD-HDR-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExe}

[Files]
Source: "..\dist\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\shaders\*"; DestDir: "{app}\shaders"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\SD-HDR"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\SD-HDR"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch SD-HDR"; Flags: nowait postinstall skipifsilent
