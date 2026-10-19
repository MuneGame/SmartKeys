/*
  ==============================================================================

    MicPermission.h
    SmartKeys — permiso de micrófono de macOS (AVFoundation, MicPermission_mac.mm).

    Dentro de un DAW, macOS concede el micrófono a la APLICACIÓN anfitriona
    (p. ej. "FL Studio"), no al plugin: el aviso y la lista de Ajustes del
    Sistema > Privacidad y seguridad > Micrófono muestran el nombre del DAW.

  ==============================================================================
*/

#pragma once

namespace smartkeys::micpermission
{
    // 1 = permitido, 0 = aún no se ha preguntado, -1 = denegado o restringido
    int getStatus() noexcept;

    // Muestra el aviso de macOS (asíncrono; el resultado se lee con getStatus())
    void request() noexcept;
}
