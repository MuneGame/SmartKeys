; ==============================================================================
;  SmartKeys — Instalador para Windows (Inno Setup 6.3 o superior)
;
;  Compilar:
;    1) cmake -B build -A x64
;    2) cmake --build build --config Release
;    3) "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Installer\Installer_Windows.iss
;       (o simplemente ejecutar Installer\build_windows.bat)
;
;  Resultado: Installer\Output\SmartKeys-1.0.0-Windows-Setup.exe
;  - VST3       -> C:\Program Files\Common Files\VST3\SmartKeys.vst3   ({commoncf64}\VST3)
;  - Standalone -> C:\Program Files\SmartKeys\SmartKeys.exe            (opcional)
; ==============================================================================

#define AppName      "SmartKeys"
#define AppVersion   "1.0.0"
#define AppPublisher "JeanPaul Audio"
#define BuildDir     "..\build\SmartKeys_artefacts\Release"

[Setup]
AppId={{6B7E2C1D-4F3A-4E8B-9C2D-1A5F7E3B9D42}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputDir=Output
OutputBaseFilename={#AppName}-{#AppVersion}-Windows-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\{#AppName}.exe
CloseApplications=yes

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Instalación completa (VST3 + Standalone)"
Name: "custom"; Description: "Personalizada"; Flags: iscustom

[Components]
Name: "vst3";       Description: "Plugin VST3 (FL Studio, Ableton Live, Cubase, Reaper...)"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Aplicación Standalone (usar sin DAW)";                    Types: full

[Files]
; Bundle VST3 completo (carpeta SmartKeys.vst3 con Contents\x86_64-win\SmartKeys.vst3)
Source: "{#BuildDir}\VST3\SmartKeys.vst3\*"; DestDir: "{commoncf64}\VST3\SmartKeys.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

; Standalone
Source: "{#BuildDir}\Standalone\SmartKeys.exe"; DestDir: "{app}"; \
    Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Components: standalone
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppName}.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Crear acceso directo en el escritorio"; Components: standalone; Flags: unchecked

[InstallDelete]
; Elimina una versión anterior del bundle para evitar archivos huérfanos
Type: filesandordirs; Name: "{commoncf64}\VST3\SmartKeys.vst3"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SmartKeys.vst3"

[Run]
Filename: "{app}\{#AppName}.exe"; Description: "Abrir SmartKeys Standalone"; \
    Components: standalone; Flags: nowait postinstall skipifsilent unchecked

[Messages]
spanish.FinishedLabel=SmartKeys se instaló correctamente.%n%nEl VST3 está en C:\Program Files\Common Files\VST3. Vuelve a escanear plugins en tu DAW (FL Studio: Options > Manage plugins > Find plugins; Ableton: Preferences > Plug-Ins > Rescan).
