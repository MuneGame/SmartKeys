#!/usr/bin/env bash
# ==============================================================================
#  SmartKeys — desinstalación completa en macOS
#  Uso:  bash Installer/uninstall_mac.sh      (pide tu contraseña)
# ==============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

for s in scripts-vst3/preinstall scripts-au/preinstall scripts-app/preinstall; do
    sudo bash "$ROOT/Installer/macos/$s"
done
for id in com.jeanpaulaudio.smartkeys.vst3 com.jeanpaulaudio.smartkeys.au com.jeanpaulaudio.smartkeys.app; do
    sudo pkgutil --forget "$id" >/dev/null 2>&1 || true
done
echo "SmartKeys se desinstaló por completo (registro: /tmp/smartkeys-install.log)."
