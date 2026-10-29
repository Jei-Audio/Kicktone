; Kicktone (formerly Tailtone) Windows installer (Inno Setup 6.3 or later).
; Built by Build-Installer-Windows.cmd, which passes AppVersion, Vst3Dir,
; StandaloneExe and PackageDir. Do not build this file by hand.
#ifndef AppVersion
  #error Run Build-Installer-Windows.cmd (AppVersion is not defined)
#endif

[Setup]
; PERMANENT (v0.5.24 onward): one AppId for every version, like the plugin's
; permanent identity, so a new installer replaces the installed plugin (also an
; install made while it was called Tailtone). Never change it.
AppId={{BF2873E4-6204-4723-B626-A45B2DF4731F}
AppName=Kicktone
AppVersion={#AppVersion}
AppVerName=Kicktone {#AppVersion}
AppPublisher=Jei Audio
AppContact=Jeiaudio.project@gmail.com
AppCopyright=Jei Audio (Jeipster), GNU GPL v3
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Kicktone {#AppVersion} installer
DefaultDirName={autopf}\Jei Audio\Kicktone
; v0.5.29: an upgrade from Tailtone must not keep the old "Tailtone" folder.
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile={#PackageDir}\Source-code\LICENSE.txt
OutputBaseFilename=Kicktone-{#AppVersion}-Windows-Setup
SetupIconFile={#Vst3Dir}\Plugin.ico
UninstallDisplayName=Kicktone
UninstallDisplayIcon={commoncf64}\VST3\Kicktone.vst3\Plugin.ico
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The VST3 folder (Common Files) needs administrator rights.
PrivilegesRequired=admin
MinVersion=10.0
WizardStyle=modern
; Offers to close a DAW that has the old plugin loaded.
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Types]
Name: "full"; Description: "VST3 plugin and standalone app"
Name: "vst3"; Description: "VST3 plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Kicktone VST3 plugin (Common Files\VST3)"; Types: full vst3 custom
Name: "standalone"; Description: "Kicktone standalone app"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone app"; Components: standalone; Flags: unchecked

[InstallDelete]
; The new version replaces the whole bundle, including one copied there by
; BUILD-WINDOWS.cmd, so no file of an older build is left behind.
Type: filesandordirs; Name: "{commoncf64}\VST3\Kicktone.vst3"; Components: vst3
; v0.5.29: the plugin was renamed from Tailtone. The old bundle has the same
; plugin ID, so remove it (the DAW would list the plugin twice), together with
; the old standalone folder and shortcuts.
Type: filesandordirs; Name: "{commoncf64}\VST3\Tailtone.vst3"; Components: vst3
Type: filesandordirs; Name: "{autopf}\Jei Audio\Tailtone"
Type: files; Name: "{autoprograms}\Tailtone.lnk"
Type: files; Name: "{autodesktop}\Tailtone.lnk"

[Files]
Source: "{#Vst3Dir}\*"; DestDir: "{commoncf64}\VST3\Kicktone.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StandaloneExe}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#PackageDir}\Installers\windows\Kicktone-ReadMe.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "{#PackageDir}\Source-code\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\Source-code\THIRD-PARTY.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Kicktone"; Filename: "{app}\Kicktone.exe"; Components: standalone
Name: "{autodesktop}\Kicktone"; Filename: "{app}\Kicktone.exe"; Tasks: desktopicon

[UninstallDelete]
Type: dirifempty; Name: "{app}"
Type: dirifempty; Name: "{autopf}\Jei Audio"
