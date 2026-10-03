; Pixelview Desktop installer (Inno Setup 6). Built by cmake/windows/pixelview-release.py:
;   iscc /DAppVersion=0.1.0 /DBuildNumber=11 /DSourceDir=<rundir\Release> /DStageDir=<licenses>
;        /DOutputDir=<dist> /DOutputName=<name> [/DSign /Spixelview=<signtool command>] pixelview-installer.iss
; Per-user install (no administrator prompt), so WinSparkle can apply updates
; silently: the appcast passes /SILENT /SP- /NOCANCEL /NORESTART.

#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef BuildNumber
  #error BuildNumber is required
#endif
#ifndef SourceDir
  #error SourceDir is required
#endif
#ifndef StageDir
  #error StageDir is required
#endif
#ifndef IconFile
  #error IconFile is required
#endif

#define AppName "Pixelview Desktop"
#define AppExe "bin\64bit\Pixelview.exe"

[Setup]
; Never change AppId: it ties updates and uninstall to earlier installs.
AppId={{6F0B6C2E-8D4A-4E59-9B1F-6A1D2C7E9B30}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
VersionInfoVersion={#AppVersion}.{#BuildNumber}
VersionInfoProductVersion={#AppVersion}
AppPublisher=Pixelview
AppPublisherURL=https://pixelview.io
AppSupportURL=https://pixelview.io
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19045
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=force
RestartApplications=no
LicenseFile={#StageDir}\COPYING
#ifdef Sign
SignTool=pixelview
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Replace the program tree on update so removed plugins never linger.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\obs-plugins"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb"
Source: "{#StageDir}\*"; DestDir: "{app}\Licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin\64bit"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Tasks: desktopicon

[Registry]
; pixelview:// player links for this user. The app only claims the scheme
; itself when no usable registration exists (development builds).
Root: HKCU; Subkey: "Software\Classes\pixelview"; ValueType: string; ValueName: ""; ValueData: "URL:Pixelview Desktop"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\pixelview"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\pixelview\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"",0"
Root: HKCU; Subkey: "Software\Classes\pixelview\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

[Run]
; Also runs after a silent WinSparkle update, so the app comes back.
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall
