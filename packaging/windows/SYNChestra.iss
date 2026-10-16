; Inno Setup script for the Windows installer. CI runs:
;   iscc /DAppVersion=1.0.0 /DAppExe=<path to SYNChestra.exe> /O<output folder> packaging\windows\SYNChestra.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppExe
  #define AppExe "..\..\build\SYNChestra_artefacts\Release\SYNChestra.exe"
#endif

[Setup]
; Keep this AppId: Windows uses it to upgrade an installed copy instead of adding a second one.
AppId={{0A5859E9-1943-4AE4-A18F-2C48E13B0F9E}
AppName=SYNChestra
AppVersion={#AppVersion}
AppVerName=SYNChestra {#AppVersion}
AppPublisher=SYNChestra
AppPublisherURL=https://github.com/Sharkuel/SYNChestra
AppSupportURL=https://github.com/Sharkuel/SYNChestra/issues
DefaultDirName={autopf}\SYNChestra
DefaultGroupName=SYNChestra
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputBaseFilename=SYNChestra-Windows-x64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Installs for everyone by default; the first page offers "only for me" without admin rights.
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\SYNChestra.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\SYNChestra"; Filename: "{app}\SYNChestra.exe"
Name: "{autodesktop}\SYNChestra"; Filename: "{app}\SYNChestra.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\SYNChestra.exe"; Description: "{cm:LaunchProgram,SYNChestra}"; Flags: nowait postinstall skipifsilent
