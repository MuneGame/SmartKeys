/*
  ==============================================================================

    ProgressionView.h
    SmartKeys — Constructor de progresiones estilo Klimper:

      - ChordPaletteView: acordes diatónicos de la escala activa (una columna
        por grado y una fila por tipo: tríada, 7ª, 6ª, add9, sus2, sus4) con
        cifrado (C, Dm, Em...) o grados romanos (I, ii, iii...). Clic: escucha
        y lo añade al final; arrastrar: lo inserta donde lo sueltes.
      - ProgressionLane: carril por compases con los bloques de acordes
        (construidos a mano o grabados con el DAW). Cada bloque muestra sus
        notas apiladas (y el desfase del STRUM). Se mueven, se alargan, se
        duplican y se borran; cabezal de la vista previa y de la grabación.

  ==============================================================================
*/

#pragma once

#include "UiCommon.h"
#include "PluginProcessor.h"

namespace smartkeys::ui
{

//==============================================================================
class ProgressionLane;

class ChordPaletteView : public juce::Component,
                         public juce::TooltipClient
{
public:
    ChordPaletteView (SmartKeysAudioProcessor& p, ProgressionLane& lane);

    // Reconstruye la paleta si cambió la escala, la tónica, las notas o el modo de nombres
    void refresh (int rootKey, uint16_t pitchClassMask, bool romanNumerals, const ScaleChordEngine& namer);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    static juce::String encodeChord (const PaletteCell& cell);

private:
    juce::Rectangle<float> getCellBounds (int row, int column) const;
    juce::Point<int> cellAt (juce::Point<float>) const;   // (fila, columna) o (-1, -1)
    void stopAudition();

    SmartKeysAudioProcessor& processor;
    ProgressionLane& lane;
    ChordPalette palette;
    juce::StringArray labels;   // fila * columnas + columna
    juce::StringArray columnHeaders;

    int shownRoot = -1;
    uint16_t shownMask = 0;
    bool shownRoman = false;
    bool shownFlats = false;

    juce::Point<int> hovered { -1, -1 }, pressed { -1, -1 };
    bool auditioning = false, dragging = false;
};

//==============================================================================
class ProgressionLane : public juce::Component,
                        public juce::DragAndDropTarget,
                        private juce::ScrollBar::Listener
{
public:
    explicit ProgressionLane (SmartKeysAudioProcessor& p);
    ~ProgressionLane() override;

    // Llamado desde el temporizador del editor (60 Hz)
    void update (bool romanNumerals, const ScaleChordEngine& namer);

    // Añade un acorde de la paleta al final de la progresión
    void appendChord (const PaletteCell& cell);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    void deleteSelected();
    void duplicateSelected();
    void undo();

private:
    struct BlockView
    {
        double start = 0.0, length = 0.0;
        int count = 0;
        std::array<int, ChordBlock::kMaxNotes> notes {};
        std::array<double, ChordBlock::kMaxNotes> offsets {};
        bool recorded = false, open = false;
        int quality = 4;
        uint32_t uid = 0;
        juce::String label;
    };

    enum class DragMode { None, Move, Resize };

    double getBeatsPerBar() const;
    double getGrid() const;
    double xToBeat (float x) const;
    float beatToX (double beat) const;
    juce::Rectangle<float> getBlocksArea() const;
    juce::Rectangle<float> getBlockBounds (const BlockView& b) const;
    int blockAt (juce::Point<float>, bool& onRightEdge) const;
    void rebuildViews (bool romanNumerals, const ScaleChordEngine& namer);
    void updateViewRange();
    void showContextMenu (int index);
    static bool decodeChord (const juce::String& text, std::array<int, ChordBlock::kMaxNotes>& notes, int& count);
    static int qualityOf (const std::vector<int>& notes);
    void auditionBlock (int viewIndex, bool on);
    int selectedViewIndex() const noexcept;

    SmartKeysAudioProcessor& processor;
    std::vector<BlockView> views;
    juce::ScrollBar scrollBar { false };

    uint32_t shownVersion = 0;
    bool shownRoman = false;
    int shownRoot = -1;
    uint16_t shownMask = 0;
    double shownStrumMs = -1.0, shownBpm = -1.0;   // desfase del STRUM dibujado en los acordes de la paleta
    bool shownStrumDescending = false;
    double contentEnd = 0.0;
    double viewStart = 0.0, viewBeats = 16.0;

    uint32_t selectedUid = 0;
    int hovered = -1;
    DragMode dragMode = DragMode::None;
    double dragOriginalStart = 0.0, dragOriginalLength = 0.0;
    bool dragUndoPushed = false, auditioning = false;

    double dropBeat = -1.0;          // marcador de inserción al arrastrar desde la paleta
    double playheadBeat = -1.0;      // vista previa
    double recordHeadBeat = -1.0;    // grabación
    bool previewing = false, capturing = false;
};

} // namespace smartkeys::ui
