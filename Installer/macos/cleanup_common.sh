#!/bin/bash
# ==============================================================================
#  SmartKeys — funciones de limpieza compartidas por los scripts del .pkg
#  (se copian dentro de cada componente al generar el instalador)
# ==============================================================================
SMARTKEYS_LOG="/tmp/smartkeys-install.log"
SMARTKEYS_NAMES=("SmartKeys" "SmartKeys FX")

sk_log()    { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" >> "$SMARTKEYS_LOG"; }
sk_remove() { if [ -e "$1" ] || [ -L "$1" ]; then rm -rf "$1" && sk_log "borrado: $1"; fi; }

# Carpetas personales de todos los usuarios de la Mac (incluye ~/Library de cada uno)
sk_user_homes()
{
    for home in /Users/*; do
        [ -d "$home/Library" ] && echo "$home"
    done
}

# Borra <nombre><extensión> en la carpeta global y en la de cada usuario
sk_remove_bundles()   # $1 = subcarpeta de Plug-Ins (VST3 | Components)  $2 = extensión
{
    local sub="$1" ext="$2" name home
    for name in "${SMARTKEYS_NAMES[@]}"; do
        sk_remove "/Library/Audio/Plug-Ins/$sub/$name$ext"
        while IFS= read -r home; do
            sk_remove "$home/Library/Audio/Plug-Ins/$sub/$name$ext"
        done < <(sk_user_homes)
    done
}

sk_flush_audio_unit_cache()
{
    local home
    while IFS= read -r home; do
        sk_remove "$home/Library/Caches/AudioUnitCache/com.apple.audiounits.cache"
    done < <(sk_user_homes)
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
    sk_log "caché de Audio Units refrescada"
}

# Fuerza a FL Studio a volver a escanear SmartKeys (borra sólo sus fichas .nfo)
sk_forget_in_fl_studio()
{
    local home db
    while IFS= read -r home; do
        db="$home/Documents/Image-Line/FL Studio/Presets/Plugin database/Installed"
        [ -d "$db" ] || continue
        find "$db" -type f \( -name "SmartKeys.nfo" -o -name "SmartKeys FX.nfo" \) -print -delete >> "$SMARTKEYS_LOG" 2>&1 || true
    done < <(sk_user_homes)
}
