#!/usr/bin/env bash
# ==============================================================================
#  SmartKeys — compila (Universal: Apple Silicon + Intel) y genera el instalador
#  .pkg para macOS con pkgbuild + productbuild.
#
#  Instala automáticamente (borrando ANTES cualquier versión anterior en
#  /Library y en ~/Library de cada usuario, incluida la vieja "SmartKeys FX"
#  de la 1.1–1.4, con scripts preinstall):
#    VST3 -> /Library/Audio/Plug-Ins/VST3/SmartKeys.vst3
#    AU   -> /Library/Audio/Plug-Ins/Components/SmartKeys.component
#    App  -> /Applications/SmartKeys.app                        (opcional)
#  y refresca la caché de Audio Units (killall AudioComponentRegistrar).
#
#  Uso:
#    ./Installer/build_mac_installer.sh
#
#  Firma / notarización opcionales (recomendadas para distribuir):
#    export DEV_ID_APP="Developer ID Application: Tu Nombre (TEAMID)"
#    export DEV_ID_INSTALLER="Developer ID Installer: Tu Nombre (TEAMID)"
#    export NOTARY_PROFILE="perfil-notarytool"   # xcrun notarytool store-credentials
#  Sin estas variables los binarios se firman ad-hoc (uso personal).
# ==============================================================================
set -euo pipefail

VERSION="2.0.1"
ID_BASE="com.miwn.smartkeys"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
WORK="$ROOT/Installer/.pkgwork"
OUT="$ROOT/Installer/Output"
SCRIPTS_SRC="$ROOT/Installer/macos"

echo "==> [1/5] Compilando Release (arm64 + x86_64): SmartKeys (VST3 + AU + Standalone)"
# Ninja si está instalado; si no, Makefiles (sólo requiere las Command Line Tools)
GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"; fi
cmake -S "$ROOT" -B "$BUILD" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DSMARTKEYS_COPY_AFTER_BUILD=OFF
cmake --build "$BUILD" --config Release --parallel

ART="$BUILD/SmartKeys_artefacts/Release"
VST3="$ART/VST3/SmartKeys.vst3"
AU="$ART/AU/SmartKeys.component"
APP="$ART/Standalone/SmartKeys.app"
for b in "$VST3" "$AU" "$APP"; do
    [[ -d "$b" ]] || { echo "No se encontró $b"; exit 1; }
done

echo "==> [2/5] Firmando bundles"
SIGN_ID="${DEV_ID_APP:--}"
TIMESTAMP="--timestamp=none"
[[ "$SIGN_ID" != "-" ]] && TIMESTAMP="--timestamp"

# Permiso de micrófono (Hardened Runtime lo exige). 2.0: la app carga en su ranura
# plugins de otros fabricantes (firmados por ellos): sin estos permisos macOS no la dejaría
ENTITLEMENTS="$ROOT/Installer/.standalone.entitlements"
cat > "$ENTITLEMENTS" <<'EOS'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.device.audio-input</key>
    <true/>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
    <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
    <true/>
</dict>
</plist>
EOS

for b in "$VST3" "$AU"; do
    codesign --force --deep --options runtime "$TIMESTAMP" --sign "$SIGN_ID" "$b"
done
codesign --force --deep --options runtime "$TIMESTAMP" --entitlements "$ENTITLEMENTS" --sign "$SIGN_ID" "$APP"
rm -f "$ENTITLEMENTS"

for b in "$VST3" "$AU" "$APP"; do
    codesign --verify --deep --strict "$b"
done

echo "==> [3/5] Preparando payloads y scripts de limpieza"
rm -rf "$WORK" && mkdir -p "$WORK"/{vst3,au,app,pkgs} "$OUT"
mkdir -p "$WORK/vst3/Library/Audio/Plug-Ins/VST3" \
         "$WORK/au/Library/Audio/Plug-Ins/Components" \
         "$WORK/app/Applications"
ditto "$VST3" "$WORK/vst3/Library/Audio/Plug-Ins/VST3/SmartKeys.vst3"
ditto "$AU"   "$WORK/au/Library/Audio/Plug-Ins/Components/SmartKeys.component"
ditto "$APP"  "$WORK/app/Applications/SmartKeys.app"

# Cada componente lleva su preinstall (borra SOLO su tipo de archivo) + funciones comunes
for c in vst3 au app; do
    mkdir -p "$WORK/scripts-$c"
    cp "$SCRIPTS_SRC/cleanup_common.sh" "$WORK/scripts-$c/"
    cp "$SCRIPTS_SRC/scripts-$c/"* "$WORK/scripts-$c/"
    chmod +x "$WORK/scripts-$c/"*
done

echo "==> [4/5] pkgbuild (componentes no reubicables)"
build_component () {   # $1=nombre  $2=raíz  $3=identificador  $4=scripts
    local plist="$WORK/$1.plist"
    pkgbuild --analyze --root "$2" "$plist" >/dev/null
    # Evita que el instalador "reubique" un bundle a una copia antigua en otra carpeta
    # (pkgbuild --analyze no siempre incluye la clave: si no existe, se añade)
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null \
            || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"
        i=$((i + 1))
    done
    plutil -p "$plist"
    pkgbuild --root "$2" --component-plist "$plist" --identifier "$3" \
             --version "$VERSION" --install-location "/" --scripts "$4" "$WORK/pkgs/$1.pkg"
}
build_component vst3 "$WORK/vst3" "$ID_BASE.vst3" "$WORK/scripts-vst3"
build_component au   "$WORK/au"   "$ID_BASE.au"   "$WORK/scripts-au"
build_component app  "$WORK/app"  "$ID_BASE.app"  "$WORK/scripts-app"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>SmartKeys $VERSION (MIWN)</title>
    <options customize="allow" require-scripts="true" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>
    <choice id="vst3" title="VST3: SmartKeys" description="/Library/Audio/Plug-Ins/VST3 (borra antes la versión anterior y SmartKeys FX)">
        <pkg-ref id="$ID_BASE.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit: SmartKeys" description="/Library/Audio/Plug-Ins/Components (borra antes la versión anterior, SmartKeys FX y la caché AU)">
        <pkg-ref id="$ID_BASE.au"/>
    </choice>
    <choice id="app" title="App SmartKeys (Standalone)" description="/Applications">
        <pkg-ref id="$ID_BASE.app"/>
    </choice>
    <pkg-ref id="$ID_BASE.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$ID_BASE.au"   version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="$ID_BASE.app"  version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
EOF

echo "==> [5/5] productbuild"
PKG="$OUT/SmartKeys-$VERSION-macOS.pkg"
if [[ -n "${DEV_ID_INSTALLER:-}" ]]; then
    productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" \
                 --sign "$DEV_ID_INSTALLER" "$PKG"
else
    productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" "$PKG"
fi

if [[ -n "${NOTARY_PROFILE:-}" && -n "${DEV_ID_INSTALLER:-}" ]]; then
    echo "==> Notarizando"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

rm -rf "$WORK"
echo ""
echo "Listo: $PKG"
