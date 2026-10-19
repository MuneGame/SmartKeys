/*
  ==============================================================================

    PluginEditor.h
    SmartKeys (MIWN) — Interfaz gráfica vectorial (850 x 606 px, redimensionable).

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UiCommon.h"

namespace smartkeys::ui
{

//==============================================================================
// Módulo KEY: 5 bloques oscuros (#) y 7 blancos, la tónica activa en magenta
class KeySelector : public juce::Component
{
public:
    explicit KeySelector (juce::RangedAudioParameter& rootParam);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> getKeyBounds (int pitchClass) const;
    int hitTestKey (juce::Point<float>) const;

    int selected = 0;
    int hovered = -1;
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

    // Suelta la nota que el ratón mantenía pulsada (al cerrar la ventana)
    void releaseMouseNote();

private:
    static bool isBlack (int note) noexcept;
    bool isNoteLit (int note) const noexcept;
    juce::Rectangle<float> getKeyRect (int note) const;
    int noteAt (juce::Point<float> p, int& velocity) const;

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
    enum class State { Hidden, Off, Waiting, Active, Unavailable, Looping };

    ChordPortStatus();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void setState (State newState, const juce::String& deviceName);

    std::function<void()> onClick;   // encender / apagar el teclado

private:
    State state = State::Hidden;
    juce::String name;
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
                                      private juce::Timer
{
public:
    explicit SmartKeysAudioProcessorEditor (SmartKeysAudioProcessor&);
    ~SmartKeysAudioProcessorEditor() override;

    static constexpr int kWidth = 850;
    static constexpr int kHeight = 606;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    // Todo el contenido vive en un componente de 850 x 606 que se escala al redimensionar
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

    // Teclado de la computadora (A W S E D F T G Y H U J K, Z/X = octava)
    static constexpr const char* kKeyMap = "awsedftgyhujk";
    std::array<bool, 13> computerKeysDown {};
    std::array<int, 13> computerKeyNotes {};
    int computerOctave = 4;

    // Enlaces por sondeo (el hilo de audio nunca avisa a la interfaz).
    // Declarados DESPUÉS de todos los controles: se destruyen antes que ellos.
    std::unique_ptr<smartkeys::ui::PolledButtonAttachment> micAttach;
    std::unique_ptr<smartkeys::ui::PolledSliderAttachment> gainAttach, gateAttach, strumAttach;

    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartKeysAudioProcessorEditor)
};
