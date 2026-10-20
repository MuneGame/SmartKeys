/*
  ==============================================================================

    MacActivity.mm
    SmartKeys — pruebas: actividad de NSProcessInfo retenida correctamente
    (ver MacActivity.h).

  ==============================================================================
*/

#import <Foundation/Foundation.h>
#include "MacActivity.h"

void* smartkeysBeginLatencyCriticalActivity()
{
    id activity = [[NSProcessInfo processInfo] beginActivityWithOptions: NSActivityUserInitiatedAllowingIdleSystemSleep
                                                                         | NSActivityLatencyCritical
                                                                 reason: @"SmartKeysHostTests: audio y MIDI a tiempo real"];
    return (void*) CFBridgingRetain (activity);   // +1: vive hasta smartkeysEndActivity
}

void smartkeysEndActivity (void* token)
{
    if (token == nullptr)
        return;

    id activity = CFBridgingRelease (token);
    [[NSProcessInfo processInfo] endActivity: activity];
}
