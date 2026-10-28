#!/usr/bin/env bash
# ==============================================================================
#  SmartKeys — borra la versión anterior, compila la nueva e instálala (macOS)
#
#  Uso (desde la carpeta del proyecto):
#      bash Installer/install_mac.sh
#
#  Pasos:
#    1. Comprueba las herramientas (Command Line Tools de Apple y CMake).
#    2. Borra SmartKeys y la vieja SmartKeys FX (.vst3, .component, .app) de /Library,
#       ~/Library de cada usuario y /Applications, la caché de Audio Units y la
#       ficha de SmartKeys en la base de datos de FL Studio.
#    3. Compila (Universal) y genera el .pkg.
#    4. Instala el .pkg y refresca la caché de Audio Units.
#  Pide tu contraseña de administrador para los pasos 2 y 4.
# ==============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="2.0.3"
PKG="$ROOT/Installer/Output/SmartKeys-$VERSION-macOS.pkg"

echo "==> [1/4] Comprobando herramientas"
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Faltan las Command Line Tools de Apple. Se abrirá el instalador de Apple."
    xcode-select --install || true
    echo "Cuando termine esa instalación, vuelve a ejecutar:  bash Installer/install_mac.sh"
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    if command -v brew >/dev/null 2>&1; then
        echo "Instalando CMake con Homebrew..."
        brew install cmake
    else
        echo "Instalando CMake sólo para tu usuario (pip)..."
        python3 -m pip install --user --upgrade cmake \
            || python3 -m pip install --user --upgrade --break-system-packages cmake
        export PATH="$(python3 -m site --user-base)/bin:$PATH"
    fi
fi
cmake --version | head -1

echo "==> [2/4] Borrando cualquier versión anterior de SmartKeys (pide tu contraseña)"
for s in scripts-vst3/preinstall scripts-au/preinstall scripts-app/preinstall; do
    sudo bash "$ROOT/Installer/macos/$s"
done
echo "    (registro: /tmp/smartkeys-install.log)"

echo "==> [3/4] Compilando y generando el instalador"
bash "$ROOT/Installer/build_mac_installer.sh"

echo "==> [4/4] Instalando"
sudo installer -pkg "$PKG" -target /
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

echo ""
echo "SmartKeys $VERSION instalado."
echo "En FL Studio: Options > Manage plugins > Find installed plugins."
