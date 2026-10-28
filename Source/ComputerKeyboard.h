/*
  ==============================================================================

    ComputerKeyboard.h
    SmartKeys (MIWN) — El teclado de la computadora como teclado de notas
    (A W S E D F T G Y H U J K = una octava; Z / X = bajar / subir la octava).

    Sigue tocando acordes aunque se haga clic en la interfaz del instrumento
    cargado (vista SYNTH): esa interfaz es una vista nativa del plugin dentro de
    la ventana de SmartKeys y, al tocar una perilla o elegir un preset, se queda
    con el foco del teclado del sistema. Por eso las teclas no se leen de los
    eventos de JUCE (que dejan de llegar a SmartKeys) sino:

      - macOS: un monitor de eventos de la aplicación (NSEvent) ve cada tecla
        mientras el foco lo tiene SmartKeys o algo dentro de ella (la interfaz
        del plugin); y ve cada tecla que se suelta, aunque sea con otra ventana
        delante.
      - Windows: el estado físico de las teclas (GetAsyncKeyState) mientras la
        ventana de SmartKeys, o algo dentro de ella (el plugin), tiene el teclado.
      - Linux (app de prueba): el estado de las teclas del servidor X
        (XQueryKeymap) mientras el foco del teclado está dentro de la ventana.

    Una nota empieza sólo con una pulsación nueva de su tecla y termina al
    soltarla. Si lo que tiene el teclado es un campo de texto (el nombre de un
    preset, el buscador de sonidos del plugin), las teclas no tocan notas. Si la
    ventana pierde el teclado, o se elige otra KEY, lo que sonaba se suelta, y
    una tecla que siga pulsada no vuelve a sonar hasta pulsarla otra vez.

    Todo en el hilo de mensajes.

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>

namespace smartkeys
{

class ComputerKeyboard
{
public:
    static constexpr const char* kNoteKeys = "awsedftgyhujk";
    static constexpr int kNumNoteKeys = 13;

    // 'window': la ventana de SmartKeys (su editor). Las teclas cuentan mientras el foco del
    // teclado está en ella o dentro de ella (también en la interfaz del plugin cargado).
    explicit ComputerKeyboard (juce::Component& window);
    ~ComputerKeyboard();

    // Una tecla de nota (0..12) empieza o deja de sonar
    std::function<void (int keyIndex, bool isDown)> onNoteKey;
    // Z / X: bajar (-1) o subir (+1) la octava
    std::function<void (int delta)> onOctave;
    // ¿Pueden tocar ahora las teclas? (p. ej. no mientras se escribe el nombre de un preset)
    std::function<bool()> isAllowed;

    // 60 veces por segundo (y con cada evento de teclado de JUCE). Windows y Linux leen las
    // teclas; en todos, si el teclado ya no llega a la ventana, se suelta lo que sonaba.
    // Devuelve true si empezó o terminó alguna nota.
    bool poll();

    // Suelta lo que suena (las teclas que sigan pulsadas esperan a una pulsación nueva)
    void releaseAll();

    bool isSounding (int keyIndex) const noexcept   { return juce::isPositiveAndBelow (keyIndex, kNumNoteKeys) && sounding[(size_t) keyIndex]; }

    // Tecla de nota (0..12) para un carácter, o -1; Z = -1 octava, X = +1, si no 0
    static int noteKeyIndex (juce::juce_wchar c) noexcept;
    static int octaveDelta (juce::juce_wchar c) noexcept;

private:
    // Lo que ve cada sistema: una tecla se pulsa de nuevo o se suelta
    void press (int keyIndex);
    void release (int keyIndex);
    bool allowed() const;

    // Windows / Linux: una lectura de todas las teclas. Una nota empieza cuando su tecla pasa
    // de suelta a pulsada (y se puede tocar, sin Ctrl / Alt / Cmd); termina al soltarla.
    bool readKeys (bool canPlay, bool modifiersDown, const std::function<bool (juce::juce_wchar)>& isKeyDown);

    juce::Component& owner;
    std::array<bool, kNumNoteKeys> sounding {};   // notas que el editor está tocando
    std::array<bool, kNumNoteKeys + 2> held {};   // Windows / Linux: la última lectura (+ Z, X)

    struct Native;                                // la parte de cada sistema
    std::unique_ptr<Native> native;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComputerKeyboard)
};

} // namespace smartkeys
