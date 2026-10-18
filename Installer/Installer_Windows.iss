; ==============================================================================
;  SmartKeys — Instalador para Windows (Inno Setup 6.3 o superior)
;
;  Compilar:
;    1) cmake -B build -A x64
;    2) cmake --build build --config Release
;    3) "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Installer\Installer_Windows.iss
;       (o simplemente ejecutar Installer\build_windows.bat)
;
;  Resultado: Installer\Output\SmartKeys-1.2.0-Windows-Setup.exe
;  - VST3       -> C:\Program Files\Common Files\VST3\SmartKeys.vst3         ({commoncf64}\VST3)
;                  C:\Program Files\Common Files\VST3\SmartKeys FX.vst3      (efecto para la pista del micrófono)
;  - Standalone -> C:\Program Files\SmartKeys\SmartKeys.exe                  (opcional)
;
;  Antes de copiar nada, [InstallDelete] borra cualquier versión anterior de
;  SmartKeys (carpeta global y carpeta VST3 del usuario) y su ficha en la base
;  de datos de plugins de FL Studio, para que el DAW cargue siempre el binario nuevo.
; ==============================================================================

#define AppName      "SmartKeys"
#define AppVersion   "1.2.0"
#define AppPublisher "JeanPaul Audio"
#define BuildDir     "..\build\SmartKeys_artefacts\Release"
#define BuildDirFx   "..\build\SmartKeysFX_artefacts\Release"
#define FlDatabase   "{userdocs}\Image-Line\FL Studio\Presets\Plugin database\Installed"

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
Name: "vst3";       Description: "Plugins VST3: SmartKeys (instrumento) + SmartKeys FX (efecto para el micrófono)"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Aplicación Standalone (usar sin DAW)";                                               Types: full

[InstallDelete]
; 1) Versiones anteriores en la carpeta VST3 global
Type: filesandordirs; Name: "{commoncf64}\VST3\SmartKeys.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SmartKeys FX.vst3"
; 2) Copias de desarrollo en la carpeta VST3 del usuario (las que deja cmake -DSMARTKEYS_COPY_AFTER_BUILD=ON)
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\SmartKeys.vst3"
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\SmartKeys FX.vst3"
; 3) Standalone anterior
Type: files; Name: "{app}\{#AppName}.exe"
; 4) Fichas de SmartKeys en la base de datos de FL Studio -> FL vuelve a escanear el plugin nuevo
Type: files; Name: "{#FlDatabase}\Generators\VST3\SmartKeys.nfo"
Type: files; Name: "{#FlDatabase}\Generators\VST3\SmartKeys FX.nfo"
Type: files; Name: "{#FlDatabase}\Effects\VST3\SmartKeys.nfo"
Type: files; Name: "{#FlDatabase}\Effects\VST3\SmartKeys FX.nfo"

[Files]
; Bundles VST3 completos (carpeta .vst3 con Contents\x86_64-win\*.vst3)
Source: "{#BuildDir}\VST3\SmartKeys.vst3\*"; DestDir: "{commoncf64}\VST3\SmartKeys.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDirFx}\VST3\SmartKeys FX.vst3\*"; DestDir: "{commoncf64}\VST3\SmartKeys FX.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

; Standalone
Source: "{#BuildDir}\Standalone\SmartKeys.exe"; DestDir: "{app}"; \
    Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppName}.exe"; Components: standalone
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppName}.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Crear acceso directo en el escritorio"; Components: standalone; Flags: unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SmartKeys.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SmartKeys FX.vst3"

[Run]
Filename: "{app}\{#AppName}.exe"; Description: "Abrir SmartKeys Standalone"; \
    Components: standalone; Flags: nowait postinstall skipifsilent unchecked

[Messages]
spanish.FinishedLabel=SmartKeys {#AppVersion} se instaló correctamente (la versión anterior se borró antes de instalar).%n%nLos VST3 están en C:\Program Files\Common Files\VST3. Vuelve a escanear plugins en tu DAW (FL Studio: Options > Manage plugins > Find installed plugins; Ableton: Preferences > Plug-Ins > Rescan).
