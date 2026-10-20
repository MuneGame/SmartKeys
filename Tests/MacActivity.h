/*
  ==============================================================================

    MacActivity.h
    SmartKeys — pruebas: como un DAW, el proceso no entra en App Nap mientras
    se prueba el audio y el MIDI a tiempo real (macOS agruparía sus
    temporizadores).

    No se usa juce::ScopedLowPowerModeDisabler: guarda la "activity" de
    NSProcessInfo sin retenerla y su destructor puede enviar endActivity: a un
    objeto ya liberado (fue el cierre de FL Studio de la 1.6.0 al quitar el
    plugin).

  ==============================================================================
*/

#pragma once

void* smartkeysBeginLatencyCriticalActivity();
void smartkeysEndActivity (void* token);
