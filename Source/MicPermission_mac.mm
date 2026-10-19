/*
  ==============================================================================

    MicPermission_mac.mm
    SmartKeys — permiso de micrófono de macOS con la API oficial (AVFoundation).

    Sólo se llama desde el hilo de mensajes y DESPUÉS de comprobar que la
    aplicación anfitriona declara NSMicrophoneUsageDescription (si no lo
    declara, macOS cerraría el programa al pedir el permiso).

  ==============================================================================
*/

#import <AVFoundation/AVFoundation.h>
#include "MicPermission.h"

namespace smartkeys::micpermission
{
    int getStatus() noexcept
    {
        switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
        {
            case AVAuthorizationStatusAuthorized:    return 1;
            case AVAuthorizationStatusNotDetermined: return 0;
            case AVAuthorizationStatusDenied:
            case AVAuthorizationStatusRestricted:    return -1;
        }

        return -1;
    }

    void request() noexcept
    {
        // El resultado no se usa aquí: SystemMicInput vuelve a leer getStatus()
        // en cada vuelta de su temporizador (no queda ningún puntero colgando).
        [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                                 completionHandler: ^(BOOL) {}];
    }
}
