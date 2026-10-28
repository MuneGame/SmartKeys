/*
  ==============================================================================

    PluginEditor.h
    SmartKeys (MIWN) — Interfaz gráfica vectorial (850 x 658 px, redimensionable).

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UiCommon.h"
#include "ComputerKeyboard.h"

namespace smartkeys::ui
{

//==============================================================================
// Módulo KEY: 12 botones independientes, 5 oscuros arriba (C#/Db … A#/Bb) y 7 claros
// abajo (C … B), separados por huecos: ningún botón comparte un solo píxel con otro.
// Sólo la tónica elegida se ilumina en magenta (#EC1368); los demás se quedan como
// están (no es un teclado: un clic elige la tónica y nunca toca notas).
class KeySelector : public juce::Component
{
public:
    explicit KeySelector (juce::RangedAudioParameter& rootParam);
    ~KeySelector() override;
    void paint (juce::Graphics&) override;   // el brillo de la tónica, detrás de su botón
    void resized() override;

    // Después de elegir una tónica (SmartKeys suelta lo que tocaban la pantalla y la PC)
    std::function<void (int pitchClass)> onKeyChosen;

    int getSelected() const noexcept                { return selected; }
    juce::Rectangle<int> getButtonBounds (int pitchClass) const;

private:
    class KeyButton;

    void choose (int pitchClass);
    void setSelected (int pitchClass);
    static juce::Rectangle<int> layoutKey (juce::Rectangle<int> area, int pitchClass);

    std::array<std::unique_ptr<KeyButton>, 12> buttons;
    int selected = 0;
    PolledParameterAttachment attachment;
};

//==============================================================================
// Menú "Más escalas" con submenús Occidentales / Árabes (maqam) / Indias (raga)
void showScaleMenu (juce::Component& target, juce::RangedAudioParameter& scaleParam, int currentScale);

// Módulo SCALE: desplegable + botones Major / Minor / Chromatic
class ScaleSelector : public juce::Component
{
public:
    explicit ScaleSelector (juce::RangedAudioParameter& scaleParam);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class MenuBox : public juce::Component
    {
    public:
        explicit MenuBox (ScaleSelector& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    private:
        ScaleSelector& owner;
    };

    void setSelected (int scaleIndex);

    juce::RangedAudioParameter& scaleParam;
    MenuBox menuBox { *this };
    juce::TextButton majorButton { "Major" }, minorButton { "Minor" }, chromaticButton { "Chromatic" };
    int selected = 0;
    PolledParameterAttachment attachment;
};

//==============================================================================
// Barra horizontal ESCALA: 12 bloques cromáticos activables con clic
class ScaleBar : public juce::Component
{
public:
    ScaleBar (SmartKeysAudioProcessor& p, juce::RangedAudioParameter& scaleParam);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setState (uint16_t mask, const juce::String& scaleTitle, bool customised, int scaleIndex);

private:
    juce::Rectangle<float> getTitleBounds() const;
    juce::Rectangle<float> getBlockBounds (int pitchClass) const;

    SmartKeysAudioProcessor& processor;
    juce::RangedAudioParameter& scaleParam;
    uint16_t mask = 0;
    juce::String title { "C Major" };
    bool isCustom = false;
    int currentScale = 0;
    int hovered = -1;
};

//==============================================================================
// Medidor LED de entrada de micrófono
class LevelMeter : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void setState (float levelDb, float gateDb, bool gateOpen, bool enabled);

private:
    float level = -100.0f, gate = -48.0f, displayLevel = -100.0f;
    bool open = false, active = false;
};

//==============================================================================
// Mini piano visualizador de 5 octavas (C2 – B6), interactivo
class PianoVisualizer : public juce::Component
{
public:
    static constexpr int kLowestNote = 36;   // C2
    static constexpr int kNumOctaves = 5;
    static constexpr int kHighestNote = kLowestNote + kNumOctaves * 12 - 1;   // B6

    explicit PianoVisualizer (SmartKeysAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void setActiveNotes (uint64_t lo, uint64_t hi);
    void setScaleMask (uint16_t mask)    { if (mask != scaleMask) { scaleMask = mask; repaint(); } }

    // Suelta la nota que el ratón mantenía pulsada (al cerrar la ventana, al elegir otra KEY)
    void releaseMouseNote();

    // Un temblor del ratón o del trackpad mientras se pulsa no cambia de tecla: hay que
    // moverse unos píxeles, y meterse en la tecla vecina (no quedarse en el borde)
    static constexpr float kDragSlopPx = 5.0f, kKeyEdgeMarginPx = 2.5f;

private:
    static bool isBlack (int note) noexcept;
    bool isNoteLit (int note) const noexcept;
    juce::Rectangle<float> getKeyRect (int note) const;
    int noteAt (juce::Point<float> p, int& velocity, float edgeMargin = 0.0f) const;

    SmartKeysAudioProcessor& processor;
    uint64_t activeLo = 0, activeHi = 0;
    uint16_t scaleMask = 0;
    int mouseNote = -1;
};

//==============================================================================
// Lectura de la voz en la tira MIC: "Mic: A3 -> Acorde: Am" (o un aviso breve
// si macOS no deja usar el micrófono)
class VoiceReadout : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    void paint (juce::Graphics&) override;

    void showIdle();                                                    // "Mic: --"
    void showVoice (const juce::String& note, const juce::String& kind, const juce::String& result);
    void showMessage (const juce::String& message, bool isWarning);
    void showNothing();                                                 // MIC VOICE apagado

private:
    enum class Kind { Nothing, Idle, Voice, Message };
    bool set (Kind, const juce::String& a, const juce::String& b, const juce::String& c, bool warning);

    Kind kind = Kind::Nothing;
    juce::String note, label, result;
    bool warning = false;
};

//==============================================================================
// Estado del teclado MIDI virtual "SmartKeys Chords" (esquina superior derecha)
class ChordPortStatus : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    enum class State { Hidden, Off, Waiting, Active, Recording, Unavailable, Looping,
                       PianoRoll, PianoRollRecording, PianoRollNotReturning, PianoRollFlKeyboard };

    ChordPortStatus();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;   // sólo el texto (está alineado a la derecha)
    void setState (State newState, const juce::String& deviceName);

    std::function<void()> onClick;   // menú: acordes al Piano Roll de SmartKeys / a otro instrumento / apagado

private:
    State state = State::Hidden;
    juce::String name;
    float contentLeft = 0.0f;
};

//==============================================================================
// 2.0: la ranura de instrumento ("+ LOAD PLUGIN" / "Loaded: Serum"). Clic: menú
// con los instrumentos instalados.
class InstrumentSlot : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    enum class Look { Empty, Busy, Loaded, Bypassed, Problem };

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void setState (Look look, const juce::String& prefix, const juce::String& title, const juce::String& subtitle);

    std::function<void()> onClick;

private:
    Look look = Look::Empty;
    juce::String prefix, title, subtitle;
};

// VIEW: [ SMARTKEYS | SYNTH ]. SMARTKEYS muestra las escalas, KEY y el LIVE KEYBOARD; SYNTH
// muestra, dentro de la misma ventana, la interfaz del instrumento cargado.
class ViewSwitch : public juce::Component
{
public:
    ViewSwitch();
    void paint (juce::Graphics&) override;   // el rótulo "VIEW"
    void resized() override;

    void setSynth (bool showSynth);                 // sin avisar
    void setSynthAvailable (bool available);        // SYNTH sólo con un instrumento cargado
    bool isSynth() const noexcept                   { return synthButton.getToggleState(); }

    std::function<void (bool showSynth)> onChange;  // clic del usuario

private:
    juce::TextButton smartKeysButton { "SMARTKEYS" }, synthButton { "SYNTH" };
};

// Preset de SmartKeys: "PRESET / Neo-Soul · D Dorian ▾". Clic: menú de presets.
class PresetBox : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void setName (const juce::String&);

    std::function<void()> onClick;

private:
    juce::String name;
};

// Ventana dentro de la interfaz para guardar o borrar un preset (no abre otra
// ventana del sistema: en un plugin podría quedar detrás del DAW)
class PresetDialog : public juce::Component
{
public:
    PresetDialog();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // fuera de la ventana: cancelar (nada pasa a la interfaz de detrás)
    bool keyPressed (const juce::KeyPress&) override;

    // Guardar: nombre propuesto y, si hay instrumento cargado, su nombre (para incluirlo)
    void showSave (const juce::String& suggestedName, const juce::String& instrumentName);
    void showDelete (const juce::String& presetName);
    void close();

    std::function<void (const juce::String& name, bool includeInstrument)> onSave;
    std::function<void()> onDelete;

private:
    void updateWarning();
    juce::Rectangle<int> getCard() const;

    bool deleting = false;
    juce::String title, message, warning;
    juce::TextEditor nameEditor;
    juce::ToggleButton includeInstrument;
    juce::TextButton okButton, cancelButton;
};

//==============================================================================
// Pantalla LED del acorde / nota
class ChordDisplay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void setText (const juce::String& main, const juce::String& detail, bool lit);

private:
    juce::String mainText { "--" }, detailText { "Toca una tecla o canta" };   // detalle: hasta 2 líneas ("\n")
    bool isLit = false;
};

} // namespace smartkeys::ui

//==============================================================================
class SmartKeysAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::ComponentListener,
                                      private juce::Timer
{
public:
    explicit SmartKeysAudioProcessorEditor (SmartKeysAudioProcessor&);
    ~SmartKeysAudioProcessorEditor() override;

    static constexpr int kWidth = 850;
    static constexpr int kHeight = 658;
    static constexpr int kHeaderHeight = 106;   // vista SYNTH: logo, modos, GAIN y la fila INSTRUMENT

    void paint (juce::Graphics&) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    // Teclado de la PC -> notas. ComputerKeyboard lee las teclas aunque el foco lo tenga la
    // interfaz del instrumento cargado (vista SYNTH); aquí cada tecla se convierte en su nota.
    void onComputerKey (int keyIndex, bool isDown);
    void releaseComputerKeys();

    // Al elegir otra KEY: se suelta lo que tocaban el ratón y el teclado de la PC
    void releaseUiNotes();

    // VISTAS: SMARTKEYS (escalas, KEY, LIVE KEYBOARD…) o SYNTH (la interfaz del instrumento
    // cargado, dentro de esta misma ventana, debajo de la fila INSTRUMENT). En SYNTH la ventana
    // toma el tamaño de la interfaz del plugin (y lo sigue si el plugin cambia su zoom).
    void setSynthView (bool show);
    void updateSynthView();                         // temporizador: instrumento cambiado / quitado
    void applyViewSize();
    float getSynthHeaderScale() const noexcept;     // la escala de SmartKeys (la cabecera en SYNTH)
    void componentMovedOrResized (juce::Component&, bool wasMoved, bool wasResized) override;

    // Donde se muestra la interfaz del plugin (fondo oscuro; un aviso mientras se carga)
    struct HostedView : public juce::Component
    {
        void paint (juce::Graphics&) override;
        void setMessage (const juce::String&);

    private:
        juce::String message;
    };

    // Todo el contenido vive en un componente de 850 x 658 que se escala al redimensionar
    struct Content : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
        void mouseDown (const juce::MouseEvent&) override
        {
            if (auto* parent = getParentComponent())
                parent->grabKeyboardFocus();   // el teclado de la PC vuelve a tocar notas
        }
    };

    void paintContent (juce::Graphics&);
    void layoutContent();
    void timerCallback() override;
    void updateDisplay();
    void updateVoiceReadout();
    void updateStrumLabel();

    // 2.0: ranura de instrumento y presets
    void updateInstrumentRow (bool force = false);
    void showInstrumentMenu();
    void showPresetMenu();

    //==========================================================================
    // ORDEN DE LOS MIEMBROS: C++ los destruye en orden inverso. Primero el
    // LookAndFeel y los controles; los enlaces con los parámetros van al FINAL
    // para destruirse ANTES que los controles que usan.
    SmartKeysAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& apvts;
    smartkeys::ui::SmartKeysLookAndFeel lookAndFeel;
    Content content;

    // Fila superior: modo y volumen (la salida MIDI va siempre por el canal 1)
    smartkeys::ui::ChoiceButtons modeSwitch;
    juce::Slider gainSlider;
    smartkeys::ui::ChordPortStatus portStatus;        // "MIDI ● SmartKeys Chords"

    // Fila INSTRUMENT: ranura, VIEW (SMARTKEYS | SYNTH), BYPASS, UNLOAD y presets
    smartkeys::ui::InstrumentSlot instrumentSlot;
    smartkeys::ui::ViewSwitch viewSwitch;
    juce::TextButton bypassButton { "BYPASS" }, unloadButton { "UNLOAD" };
    smartkeys::ui::PresetBox presetBox;

    // Tira MIC: controlador de voz, medidor, gate y "Mic: A3 -> Acorde: Am"
    juce::TextButton micButton;
    smartkeys::ui::LevelMeter micMeter;
    juce::Slider gateSlider;
    smartkeys::ui::VoiceReadout voiceReadout;

    // KEY · SCALE · ESCALA
    smartkeys::ui::KeySelector keySelector;
    smartkeys::ui::ScaleSelector scaleSelector;
    smartkeys::ui::ScaleBar scaleBar;

    // LIVE KEYBOARD + acorde + voicing + STRUM
    smartkeys::ui::PianoVisualizer piano;
    smartkeys::ui::ChordDisplay chordDisplay;
    smartkeys::ui::ChoiceButtons chordTypeSwitch;     // SMART CHORDS: TRIAD / +8VA / 7TH
    smartkeys::ui::ChoiceButtons variationSwitch;     // COMPLEX CHORDS: Lush / Neo-Soul / Cinematic
    juce::Slider strumKnob;
    juce::Label strumTitleLabel, strumValueLabel;
    smartkeys::ui::ChoiceButtons strumDirSwitch;      // ASC ▲ / DESC ▼

    // Motor local (hilo de mensajes) para nombres de escala y acordes
    smartkeys::ScaleChordEngine displayEngine;
    int shownRoot = -1, shownScale = -1, shownMode = -1, shownVariation = -1;
    uint16_t shownMask = 0;
    uint64_t shownEvent = ~(uint64_t) 0, shownLo = ~(uint64_t) 0, shownHi = ~(uint64_t) 0;
    int shownVoiceNote = -2;

    // Teclado de la computadora (A W S E D F T G Y H U J K, Z/X = octava): la nota que suena
    // por cada tecla (-1: ninguna) y la octava
    std::array<int, smartkeys::ComputerKeyboard::kNumNoteKeys> computerKeyNotes {};
    int computerOctave = 4;

    // ACORDES AL PIANO ROLL: aviso cuando se toca con el teclado de FL (que FL graba)
    uint32_t shownDawLiveNotes = 0;
    double dawKeyboardHintUntilMs = 0.0;
    void showChordPortMenu();

    // Lo que muestra la fila INSTRUMENT (se refresca cuando cambia)
    uint32_t shownHostedChange = ~0u, shownCatalogChange = ~0u;
    juce::String shownPresetName { "\x01" }, shownFailure;
    bool shownSynthView = false, shownInvalidAudio = false;

    // Encima de todo: guardar / borrar preset
    smartkeys::ui::PresetDialog presetDialog;

    // Vista SYNTH: la interfaz del plugin (su dueño es la ranura; aquí sólo se muestra)
    HostedView hostedView;
    juce::Component::SafePointer<juce::AudioProcessorEditor> shownEditor;
    bool synthView = false, synthLayoutPending = false, changingView = false;

    // El teclado de la computadora (se destruye antes que todo lo demás: ver el destructor)
    std::unique_ptr<smartkeys::ComputerKeyboard> computerKeyboard;

    // Enlaces por sondeo (el hilo de audio nunca avisa a la interfaz).
    // Declarados DESPUÉS de todos los controles: se destruyen antes que ellos.
    std::unique_ptr<smartkeys::ui::PolledButtonAttachment> micAttach;
    std::unique_ptr<smartkeys::ui::PolledSliderAttachment> gainAttach, gateAttach, strumAttach;

    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessorEditor)
};
