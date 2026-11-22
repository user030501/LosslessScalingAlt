; Inno Setup script for LSAlt.
; Requires Inno Setup 6 (https://jrsoftware.org/isinfo.php) to compile.
;
; Build the Release binaries first, then produce the portable install
; layout that this script packages:
;
;   cmake --preset windows-x64
;   cmake --build --preset windows-x64-release
;   cmake --install build/windows-x64 --prefix dist/portable --config Release
;
; Then compile this script (from the repository root):
;
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" packaging\installer.iss
;
; The resulting installer is written to packaging\output\LSAlt-Setup.exe.

#define MyAppName "LSAlt"
#define MyAppVersion "0.5.0"
#define MyAppExeName "LSAlt.exe"
#define MyAppPublisher "LSAlt project"

[Setup]
AppId={{8F2C7F1E-3B9A-4C2E-9E7E-8B6A1A2E7A10}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=output
OutputBaseFilename=LSAlt-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Not affiliated with, derived from, or endorsed by any commercial product
; (see README.md / LICENSE) -- no third-party branding is used anywhere in
; this installer.

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
; There is no "preserve config" task because there's nothing to opt into:
; user config/profiles/logs live under %LOCALAPPDATA%\LSAlt, which this
; installer and uninstaller never touch, on every install/upgrade/uninstall.

[Files]
; Source is the portable install layout produced by `cmake --install`.
Source: "..\dist\portable\LSAlt.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\portable\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\portable\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\portable\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\portable\shaders\*"; DestDir: "{app}\shaders"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Only remove what we installed; user config/profiles/logs live under
; %LOCALAPPDATA%\LSAlt and are left alone unless the user opts out of
; "preserveconfig" above during an upgrade -- the uninstaller never
; touches %LOCALAPPDATA% regardless, by design (uninstalling should not
; silently delete the user's saved profiles and session history).
Type: filesandordirs; Name: "{app}\shaders"
