#!/usr/bin/env bash
# ==============================================================================
#  SmartKeys — compila (Universal: Apple Silicon + Intel) y genera el instalador
#  .pkg para macOS con pkgbuild + productbuild.
#
#  Instala automáticamente:
#    VST3       -> /Library/Audio/Plug-Ins/VST3/SmartKeys.vst3
#    AU         -> /Library/Audio/Plug-Ins/Components/SmartKeys.component
#    Standalone -> /Applications/SmartKeys.app            (opcional en el instalador)
#
#  Uso:
#    chmod +x Installer/build_mac_installer.sh
#    ./Installer/build_mac_installer.sh
#
#  Firma / notarización opcionales (recomendadas para distribuir):
#    export DEV_ID_APP="Developer ID Application: Tu Nombre (TEAMID)"
#    export DEV_ID_INSTALLER="Developer ID Installer: Tu Nombre (TEAMID)"
#    export NOTARY_PROFILE="perfil-notarytool"   # xcrun notarytool store-credentials
#  Sin estas variables los binarios se firman ad-hoc (uso personal).
# ==============================================================================
set -euo pipefail

VERSION="1.0.0"
NAME="SmartKeys"
ID_BASE="com.jeanpaulaudio.smartkeys"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
ART="$BUILD/${NAME}_artefacts/Release"
WORK="$ROOT/Installer/.pkgwork"
OUT="$ROOT/Installer/Output"

echo "==> [1/5] Compilando Release (arm64 + x86_64)"
# Ninja si está instalado; si no, Makefiles (sólo requiere las Command Line Tools)
GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"; fi
cmake -S "$ROOT" -B "$BUILD" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build "$BUILD" --config Release --parallel

VST3="$ART/VST3/$NAME.vst3"
AU="$ART/AU/$NAME.component"
APP="$ART/Standalone/$NAME.app"
for b in "$VST3" "$AU" "$APP"; do
    [[ -d "$b" ]] || { echo "No se encontró $b"; exit 1; }
done

echo "==> [2/5] Firmando bundles"
SIGN_ID="${DEV_ID_APP:--}"
TIMESTAMP="--timestamp=none"
[[ "$SIGN_ID" != "-" ]] && TIMESTAMP="--timestamp"

# Permiso de micrófono para la app Standalone (Hardened Runtime lo exige)
ENTITLEMENTS="$ROOT/Installer/.standalone.entitlements"
cat > "$ENTITLEMENTS" <<'EOS'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.device.audio-input</key>
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

echo "==> [3/5] Preparando payloads"
rm -rf "$WORK" && mkdir -p "$WORK"/{vst3,au,app,scripts,pkgs} "$OUT"
mkdir -p "$WORK/vst3/Library/Audio/Plug-Ins/VST3" \
         "$WORK/au/Library/Audio/Plug-Ins/Components" \
         "$WORK/app/Applications"
ditto "$VST3" "$WORK/vst3/Library/Audio/Plug-Ins/VST3/$NAME.vst3"
ditto "$AU"   "$WORK/au/Library/Audio/Plug-Ins/Components/$NAME.component"
ditto "$APP"  "$WORK/app/Applications/$NAME.app"

# postinstall del AU: refresca la caché de Audio Units para que Logic/Live lo vean ya
cat > "$WORK/scripts/postinstall" <<'EOS'
#!/bin/bash
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
EOS
chmod +x "$WORK/scripts/postinstall"

echo "==> [4/5] pkgbuild (componentes no reubicables)"
build_component () {   # $1=nombre  $2=raíz  $3=identificador  [$4=scripts]
    local plist="$WORK/$1.plist"
    pkgbuild --analyze --root "$2" "$plist" >/dev/null
    # Evita que el instalador "reubique" el bundle a una copia antigua en otra carpeta
    /usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$plist" 2>/dev/null || true
    local extra=()
    [[ -n "${4:-}" ]] && extra=(--scripts "$4")
    pkgbuild --root "$2" --component-plist "$plist" --identifier "$3" \
             --version "$VERSION" --install-location "/" ${extra[@]+"${extra[@]}"} "$WORK/pkgs/$1.pkg"
}
build_component vst3 "$WORK/vst3" "$ID_BASE.vst3"
build_component au   "$WORK/au"   "$ID_BASE.au" "$WORK/scripts"
build_component app  "$WORK/app"  "$ID_BASE.app"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>
    <choice id="vst3" title="VST3 Plug-in" description="/Library/Audio/Plug-Ins/VST3">
        <pkg-ref id="$ID_BASE.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit (AU)" description="/Library/Audio/Plug-Ins/Components">
        <pkg-ref id="$ID_BASE.au"/>
    </choice>
    <choice id="app" title="Standalone App" description="/Applications">
        <pkg-ref id="$ID_BASE.app"/>
    </choice>
    <pkg-ref id="$ID_BASE.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$ID_BASE.au"   version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="$ID_BASE.app"  version="$VERSION">app.pkg</pkg-ref>
</installer-gui-script>
EOF

echo "==> [5/5] productbuild"
PKG="$OUT/$NAME-$VERSION-macOS.pkg"
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
