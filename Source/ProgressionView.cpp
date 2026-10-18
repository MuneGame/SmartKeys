/*
  ==============================================================================

    ProgressionView.cpp
    SmartKeys — Paleta de acordes y carril de progresión (estilo Klimper).

  ==============================================================================
*/

#include "ProgressionView.h"

namespace smartkeys::ui
{

namespace
{
    juce::Colour qualityColour (int quality)
    {
        switch (quality)
        {
            case 0:  return Colours::chordMajor;
            case 1:  return Colours::chordMinor;
            case 2:  return Colours::chordDim;
            default: return Colours::chordOther;
        }
    }

    constexpr float kPaletteLabelWidth = 50.0f;
}

//==============================================================================
// PALETA DE ACORDES
//==============================================================================
ChordPaletteView::ChordPaletteView (SmartKeysAudioProcessor& p, ProgressionLane& l)
    : processor (p), lane (l)
{
    setRepaintsOnMouseActivity (false);
}

void ChordPaletteView::refresh (int rootKey, uint16_t pitchClassMask, bool romanNumerals, const ScaleChordEngine& namer)
{
    const bool flats = namer.prefersFlats();
    if (rootKey == shownRoot && pitchClassMask == shownMask && romanNumerals == shownRoman && flats == shownFlats)
        return;

    shownRoot = rootKey;
    shownMask = pitchClassMask;
    shownRoman = romanNumerals;
    shownFlats = flats;

    palette = buildChordPalette (rootKey, pitchClassMask);
    labels.clear();
    for (int row = 0; row < kNumPaletteRows; ++row)
    {
        for (int column = 0; column < palette.getNumColumns(); ++column)
        {
            const auto& cell = palette.at (row, column);
            if (! cell.valid)
            {
                labels.add ({});
                continue;
            }
            const std::vector<int> notes (cell.notes.begin(), cell.notes.begin() + cell.count);
            labels.add (romanNumerals ? namer.getRomanNumeral (notes) : namer.getShortChordName (notes));
        }
    }
    repaint();
}

juce::String ChordPaletteView::encodeChord (const PaletteCell& cell)
{
    juce::String s ("skchord:");
    for (int i = 0; i < cell.count; ++i)
        s << (i > 0 ? "," : "") << cell.notes[(size_t) i];
    return s;
}

juce::Rectangle<float> ChordPaletteView::getCellBounds (int row, int column) const
{
    const int columns = juce::jmax (1, palette.getNumColumns());
    auto area = getLocalBounds().toFloat().withTrimmedLeft (kPaletteLabelWidth);
    const float w = area.getWidth() / (float) columns;
    const float h = area.getHeight() / (float) kNumPaletteRows;
    return { area.getX() + w * (float) column, area.getY() + h * (float) row, w, h };
}

juce::Point<int> ChordPaletteView::cellAt (juce::Point<float> p) const
{
    for (int row = 0; row < kNumPaletteRows; ++row)
        for (int column = 0; column < palette.getNumColumns(); ++column)
            if (getCellBounds (row, column).contains (p) && palette.at (row, column).valid)
                return { row, column };
    return { -1, -1 };
}

void ChordPaletteView::paint (juce::Graphics& g)
{
    const auto rowNames = ChordPalette::getRowNames();

    for (int row = 0; row < kNumPaletteRows; ++row)
    {
        const auto rowBounds = getCellBounds (row, 0);
        g.setColour (Colours::label);
        g.setFont (makeFont (10.0f, true));
        g.drawText (rowNames[row], juce::Rectangle<float> (0.0f, rowBounds.getY(), kPaletteLabelWidth - 6.0f, rowBounds.getHeight()),
                    juce::Justification::centredRight, false);

        for (int column = 0; column < palette.getNumColumns(); ++column)
        {
            auto r = getCellBounds (row, column).reduced (2.5f, 2.0f);
            const auto& cell = palette.at (row, column);

            if (! cell.valid)
            {
                g.setColour (Colours::border.withAlpha (0.35f));
                g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
                continue;
            }

            const bool isHover = hovered == juce::Point<int> (row, column);
            const bool isPressed = pressed == juce::Point<int> (row, column);
            auto fill = qualityColour (cell.quality);
            if (isPressed)      fill = Colours::accent;
            else if (isHover)   fill = fill.brighter (0.25f);

            drawGlowBox (g, r, r.getHeight() * 0.5f, fill, isPressed);
            g.setColour (Colours::white);
            g.setFont (makeFont (juce::jlimit (9.5f, 12.5f, r.getHeight() * 0.55f), true));
            g.drawFittedText (labels[row * palette.getNumColumns() + column], r.toNearestInt().reduced (4, 0),
                              juce::Justification::centred, 1, 0.6f);
        }
    }
}

void ChordPaletteView::mouseMove (const juce::MouseEvent& e)
{
    const auto c = cellAt (e.position);
    if (c != hovered)
    {
        hovered = c;
        repaint();
    }
}

void ChordPaletteView::mouseExit (const juce::MouseEvent&)
{
    hovered = { -1, -1 };
    repaint();
}

void ChordPaletteView::stopAudition()
{
    if (auditioning)
    {
        processor.auditionChord (nullptr, 0, 0, false);
        auditioning = false;
    }
}

void ChordPaletteView::mouseDown (const juce::MouseEvent& e)
{
    pressed = cellAt (e.position);
    dragging = false;
    if (pressed.x >= 0)
    {
        const auto& cell = palette.at (pressed.x, pressed.y);
        processor.auditionChord (cell.notes.data(), cell.count, 100, true);
        auditioning = true;
    }
    repaint();
}

void ChordPaletteView::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed.x < 0 || dragging || e.getDistanceFromDragStart() < 6)
        return;

    stopAudition();
    dragging = true;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        const auto& cell = palette.at (pressed.x, pressed.y);
        auto bounds = getCellBounds (pressed.x, pressed.y).reduced (2.5f, 2.0f);
        juce::Image image (juce::Image::ARGB, juce::jmax (1, (int) bounds.getWidth()), juce::jmax (1, (int) bounds.getHeight()), true);
        {
            juce::Graphics ig (image);
            const auto r = image.getBounds().toFloat();
            ig.setColour (Colours::accent);
            ig.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            ig.setColour (Colours::white);
            ig.setFont (makeFont (12.0f, true));
            ig.drawFittedText (labels[pressed.x * palette.getNumColumns() + pressed.y], image.getBounds().reduced (4, 0),
                               juce::Justification::centred, 1, 0.6f);
        }
        container->startDragging (encodeChord (cell), this, juce::ScaledImage (image), false);
    }
    repaint();
}

void ChordPaletteView::mouseUp (const juce::MouseEvent& e)
{
    stopAudition();

    if (pressed.x >= 0 && ! dragging && cellAt (e.position) == pressed)
        lane.appendChord (palette.at (pressed.x, pressed.y));

    pressed = { -1, -1 };
    dragging = false;
    repaint();
}

juce::String ChordPaletteView::getTooltip()
{
    const auto c = cellAt (getMouseXYRelative().toFloat());
    if (c.x < 0)
        return u8 ("Acordes de la escala activa. Clic: escucha y añade al final de la progresión. "
                   "Arrástralo al carril para insertarlo donde quieras.");
    return labels[c.x * palette.getNumColumns() + c.y]
         + u8 ("  ·  clic: escuchar y añadir  ·  arrastrar: insertar en el carril");
}

//==============================================================================
// CARRIL DE PROGRESIÓN
//==============================================================================
ProgressionLane::ProgressionLane (SmartKeysAudioProcessor& p)
    : processor (p)
{
    setWantsKeyboardFocus (true);
    addChildComponent (scrollBar);
    scrollBar.setAutoHide (false);
    scrollBar.addListener (this);
}

ProgressionLane::~ProgressionLane()
{
    scrollBar.removeListener (this);
}

void ProgressionLane::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    viewStart = newRangeStart;
    repaint();
}

double ProgressionLane::getBeatsPerBar() const
{
    return juce::jmax (1.0, processor.getBeatsPerBar());
}

double ProgressionLane::getGrid() const
{
    const double g = QuantizeGrid::getGridPpq (processor.getQuantizeIndex());
    return g > 0.0 ? g : 0.25;   // sin cuantización: los bloques se mueven por semicorcheas
}

juce::Rectangle<float> ProgressionLane::getBlocksArea() const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f, 0.0f);
    r.removeFromTop (16.0f);   // regla de compases
    if (scrollBar.isVisible())
        r.removeFromBottom (8.0f);
    return r.withTrimmedBottom (4.0f);
}

double ProgressionLane::xToBeat (float x) const
{
    const auto area = getBlocksArea();
    return viewStart + (double) (x - area.getX()) / (double) juce::jmax (1.0f, area.getWidth()) * viewBeats;
}

float ProgressionLane::beatToX (double beat) const
{
    const auto area = getBlocksArea();
    return area.getX() + (float) ((beat - viewStart) / viewBeats) * area.getWidth();
}

juce::Rectangle<float> ProgressionLane::getBlockBounds (const BlockView& b) const
{
    const auto area = getBlocksArea();
    const float x1 = beatToX (b.start);
    const float x2 = beatToX (b.start + juce::jmax (1.0 / 32.0, b.length));
    return { x1, area.getY() + 2.0f, juce::jmax (3.0f, x2 - x1 - 2.0f), area.getHeight() - 4.0f };
}

int ProgressionLane::blockAt (juce::Point<float> p, bool& onRightEdge) const
{
    onRightEdge = false;
    for (int i = (int) views.size() - 1; i >= 0; --i)
    {
        const auto r = getBlockBounds (views[(size_t) i]);
        if (r.contains (p))
        {
            onRightEdge = p.x > r.getRight() - 7.0f && r.getWidth() > 14.0f;
            return i;
        }
    }
    return -1;
}

int ProgressionLane::selectedViewIndex() const noexcept
{
    for (int i = 0; i < (int) views.size(); ++i)
        if (views[(size_t) i].uid == selectedUid)
            return i;
    return -1;
}

int ProgressionLane::qualityOf (const std::vector<int>& notes)
{
    const auto id = ScaleChordEngine::identifyChord (notes);
    if (! id.valid)                                         return 4;
    if (id.thirdInterval == 4 && id.fifthInterval == 7)     return 0;
    if (id.thirdInterval == 3 && id.fifthInterval == 7)     return 1;
    if (id.thirdInterval == 3 && id.fifthInterval == 6)     return 2;
    return 4;
}

void ProgressionLane::rebuildViews (bool romanNumerals, const ScaleChordEngine& namer)
{
    views.clear();
    processor.withProgression ([this, romanNumerals, &namer] (ProgressionModel& m)
    {
        for (const auto& b : m.getBlocks())
        {
            BlockView v;
            v.start = b.start;
            v.length = b.length;
            v.count = b.count;
            v.notes = b.notes;
            v.offsets = b.offsets;
            v.recorded = b.recorded;
            v.open = b.open;
            v.uid = b.uid;
            const auto notes = b.getNotes();
            v.quality = qualityOf (notes);
            v.label = romanNumerals ? namer.getRomanNumeral (notes) : namer.getShortChordName (notes);
            views.push_back (v);
        }
        contentEnd = m.getContentEnd();
    });
}

void ProgressionLane::updateViewRange()
{
    const double bpb = getBeatsPerBar();
    double total = juce::jmax (4.0 * bpb, ProgressionModel::nextBarAfter (contentEnd, bpb) + bpb);
    if (capturing && recordHeadBeat >= 0.0)
        total = juce::jmax (total, ProgressionModel::nextBarAfter (recordHeadBeat, bpb) + bpb);

    const double maxView = 16.0 * bpb;
    if (total <= maxView + 1.0e-9)
    {
        viewBeats = total;
        viewStart = 0.0;
        scrollBar.setVisible (false);
        return;
    }

    viewBeats = maxView;

    // La grabación arrastra la vista para que el cabezal siempre se vea
    const double head = capturing ? recordHeadBeat : (previewing ? playheadBeat : -1.0);
    if (head >= 0.0 && (head > viewStart + viewBeats * 0.92 || head < viewStart))
        viewStart = juce::jmax (0.0, head - viewBeats * 0.25);

    viewStart = juce::jlimit (0.0, juce::jmax (0.0, total - viewBeats), viewStart);
    scrollBar.setRangeLimits (0.0, total, juce::dontSendNotification);
    scrollBar.setCurrentRange (viewStart, viewBeats, juce::dontSendNotification);
    scrollBar.setVisible (true);
}

void ProgressionLane::update (bool romanNumerals, const ScaleChordEngine& namer)
{
    bool needsRepaint = false;

    const auto version = processor.getProgressionVersion();
    const int root = namer.getRootKey();
    const uint16_t mask = namer.getCustomMask();
    if (version != shownVersion || romanNumerals != shownRoman || root != shownRoot || mask != shownMask)
    {
        shownVersion = version;
        shownRoman = romanNumerals;
        shownRoot = root;
        shownMask = mask;
        rebuildViews (romanNumerals, namer);
        needsRepaint = true;
    }

    // Los acordes de la paleta se dibujan con el STRUM actual de la perilla
    const double strumMs = processor.getStrumMs();
    const double bpm = processor.getHostBpm();
    const bool strumDescending = processor.isStrumDescending();
    if (std::abs (strumMs - shownStrumMs) > 1.0e-3 || std::abs (bpm - shownBpm) > 1.0e-3
        || strumDescending != shownStrumDescending)
    {
        shownStrumMs = strumMs;
        shownBpm = bpm;
        shownStrumDescending = strumDescending;
        needsRepaint = true;
    }

    const bool nowPreviewing = processor.isPreviewPlaying();
    const double newPlayhead = nowPreviewing ? processor.getPreviewPositionPpq() : -1.0;
    const bool nowCapturing = processor.isCapturing();
    const double newRecordHead = nowCapturing
        ? processor.withProgression ([] (ProgressionModel& m) { return m.getCaptureHead(); })
        : -1.0;

    if (nowPreviewing != previewing || nowCapturing != capturing
        || std::abs (newPlayhead - playheadBeat) > 1.0e-4 || std::abs (newRecordHead - recordHeadBeat) > 1.0e-4)
    {
        previewing = nowPreviewing;
        capturing = nowCapturing;
        playheadBeat = newPlayhead;
        recordHeadBeat = newRecordHead;
        needsRepaint = true;
    }

    if (needsRepaint)
    {
        updateViewRange();
        repaint();
    }
}

void ProgressionLane::resized()
{
    auto r = getLocalBounds().reduced (6, 0);
    scrollBar.setBounds (r.removeFromBottom (8).reduced (0, 1));
    updateViewRange();
}

void ProgressionLane::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Colours::laneBackground);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto area = getBlocksArea();
    const double bpb = getBeatsPerBar();

    // Regla de compases + rejilla
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (bounds.toNearestInt());

        const int firstBeat = (int) std::floor (viewStart);
        const int lastBeat = (int) std::ceil (viewStart + viewBeats);
        for (int beat = firstBeat; beat <= lastBeat; ++beat)
        {
            const float x = beatToX ((double) beat);
            if (x < area.getX() - 1.0f || x > area.getRight() + 1.0f)
                continue;
            const bool isBar = std::fmod ((double) beat, bpb) < 1.0e-6;
            g.setColour (juce::Colours::white.withAlpha (isBar ? 0.22f : 0.07f));
            g.fillRect (x, isBar ? 3.0f : area.getY(), 1.0f, area.getBottom() - (isBar ? 3.0f : area.getY()));
            if (isBar)
            {
                // Número de compás (sólo si cabe entero dentro del carril)
                const juce::String barNumber ((int) std::lround ((double) beat / bpb) + 1);
                if (x + 3.0f + 7.0f * (float) barNumber.length() <= area.getRight())
                {
                    g.setColour (juce::Colours::white.withAlpha (0.55f));
                    g.setFont (makeFont (10.0f, true));
                    g.drawText (barNumber, juce::Rectangle<float> (x + 3.0f, 1.0f, 30.0f, 14.0f),
                                juce::Justification::centredLeft, false);
                }
            }
        }
    }

    // Bloques de acordes
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.toNearestInt().expanded (0, 2));

        for (int i = 0; i < (int) views.size(); ++i)
        {
            const auto& v = views[(size_t) i];
            const auto r = getBlockBounds (v);
            if (r.getRight() < area.getX() || r.getX() > area.getRight())
                continue;

            const bool isSelected = v.uid == selectedUid;
            auto fill = qualityColour (v.quality);
            if (i == hovered && ! isSelected)
                fill = fill.brighter (0.2f);

            drawGlowBox (g, r, 5.0f, fill.withAlpha (0.92f), isSelected);
            if (isSelected)
            {
                g.setColour (Colours::accent);
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 2.0f);
            }
            if (v.open)
            {
                g.setColour (Colours::recording);
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 2.0f);
            }

            // Notas apiladas (mini piano roll del acorde, con el desfase del STRUM)
            auto rollArea = r.reduced (4.0f, 3.0f).withTrimmedTop (16.0f);
            if (v.count > 0 && rollArea.getHeight() > 4.0f)
            {
                int lo = v.notes[0], hi = v.notes[0];
                for (int n = 1; n < v.count; ++n)
                {
                    lo = juce::jmin (lo, v.notes[(size_t) n]);
                    hi = juce::jmax (hi, v.notes[(size_t) n]);
                }
                const float span = (float) juce::jmax (12, hi - lo + 1);
                const float rowH = juce::jlimit (1.5f, 4.0f, rollArea.getHeight() / span);
                const double strumPpqScale = processor.getHostBpm() / 60000.0;
                const auto end = v.start + juce::jmax (1.0 / 32.0, v.length);

                for (int n = 0; n < v.count; ++n)
                {
                    double offset = v.offsets[(size_t) n];
                    if (! v.recorded)
                    {
                        // Acordes de la paleta: el STRUM se aplica con la perilla actual
                        const int rank = processor.isStrumDescending() ? v.count - 1 - n : n;
                        offset = strumOffsetMs (rank, processor.getStrumMs()) * strumPpqScale;
                    }
                    const float x1 = juce::jmax (rollArea.getX(), beatToX (v.start + offset));
                    const float x2 = juce::jmin (rollArea.getRight(), beatToX (end) - 3.0f);
                    if (x2 <= x1)
                        continue;
                    const float y = rollArea.getBottom() - ((float) (v.notes[(size_t) n] - lo) + 1.0f) / span * rollArea.getHeight();
                    g.setColour (juce::Colours::white.withAlpha (0.78f));
                    g.fillRoundedRectangle (x1, y, x2 - x1, rowH, rowH * 0.4f);
                }
            }

            g.setColour (juce::Colours::white);
            g.setFont (makeFont (11.5f, true));
            g.drawFittedText (v.label, r.toNearestInt().reduced (5, 2).withHeight (14),
                              juce::Justification::centredLeft, 1, 0.55f);
        }
    }

    // Marcador de inserción (arrastrando desde la paleta)
    if (dropBeat >= 0.0)
    {
        const float x = beatToX (dropBeat);
        g.setColour (Colours::accent);
        g.fillRect (x - 1.0f, area.getY(), 3.0f, area.getHeight());
        juce::Path tri;
        tri.addTriangle (x - 6.0f, area.getY() - 6.0f, x + 6.0f, area.getY() - 6.0f, x, area.getY());
        g.fillPath (tri);
    }

    // Cabezales
    if (previewing && playheadBeat >= 0.0)
    {
        const float x = beatToX (playheadBeat);
        g.setColour (juce::Colour (0xFF7CF29C));
        g.fillRect (x, 2.0f, 2.0f, area.getBottom() - 2.0f);
    }
    if (capturing && recordHeadBeat >= 0.0)
    {
        const float x = beatToX (recordHeadBeat);
        g.setColour (Colours::recording);
        g.fillRect (x, 2.0f, 2.0f, area.getBottom() - 2.0f);
    }

    if (views.empty() && dropBeat < 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (makeFont (12.0f, true));
        g.drawFittedText (capturing ? u8 ("● Grabando: toca o canta, los acordes aparecerán aquí")
                                    : u8 ("Progresión vacía  ·  dale ▶ o ● a tu DAW (o pulsa ● REC) y toca, "
                                          "o haz clic / arrastra acordes de la paleta"),
                          area.toNearestInt().reduced (10, 0), juce::Justification::centred, 2, 0.7f);
    }
}

void ProgressionLane::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const int i = blockAt (e.position, edge);
    setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor
                         : (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
    if (i != hovered)
    {
        hovered = i;
        repaint();
    }
}

void ProgressionLane::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void ProgressionLane::auditionBlock (int viewIndex, bool on)
{
    if (on && juce::isPositiveAndBelow (viewIndex, (int) views.size()))
    {
        const auto& v = views[(size_t) viewIndex];
        processor.auditionChord (v.notes.data(), v.count, 100, true);
        auditioning = true;
    }
    else if (auditioning)
    {
        processor.auditionChord (nullptr, 0, 0, false);
        auditioning = false;
    }
}

void ProgressionLane::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    bool edge = false;
    const int i = blockAt (e.position, edge);

    if (e.mods.isPopupMenu())
    {
        if (i >= 0)
        {
            selectedUid = views[(size_t) i].uid;
            repaint();
        }
        showContextMenu (i);
        return;
    }

    dragMode = DragMode::None;
    dragUndoPushed = false;

    if (i >= 0)
    {
        const auto& v = views[(size_t) i];
        selectedUid = v.uid;
        dragMode = edge ? DragMode::Resize : DragMode::Move;
        dragOriginalStart = v.start;
        dragOriginalLength = v.length;
        if (dragMode == DragMode::Move)
            auditionBlock (i, true);
    }
    else
    {
        selectedUid = 0;
    }
    repaint();
}

void ProgressionLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::None || selectedUid == 0 || e.getDistanceFromDragStart() < 3)
        return;

    if (auditioning)
        auditionBlock (-1, false);

    const auto area = getBlocksArea();
    const double beatsPerPixel = viewBeats / (double) juce::jmax (1.0f, area.getWidth());
    const double delta = (double) (e.position.x - e.mouseDownPosition.x) * beatsPerPixel;
    const double grid = getGrid();
    const auto uid = selectedUid;

    processor.withProgression ([&] (ProgressionModel& m)
    {
        const int index = m.indexOfUid (uid);
        if (index < 0)
            return;

        if (! dragUndoPushed)
        {
            m.pushUndo();
            dragUndoPushed = true;
        }

        if (dragMode == DragMode::Move)
        {
            m.moveBlock (index, juce::jmax (0.0, std::round ((dragOriginalStart + delta) / grid) * grid));
        }
        else
        {
            const double end = std::round ((dragOriginalStart + dragOriginalLength + delta) / grid) * grid;
            m.resizeBlock (index, juce::jmax (grid, end - dragOriginalStart));
        }
    });
    processor.progressionEdited();
}

void ProgressionLane::mouseUp (const juce::MouseEvent&)
{
    auditionBlock (-1, false);
    dragMode = DragMode::None;
}

void ProgressionLane::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! scrollBar.isVisible())
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }
    const double amount = (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : -wheel.deltaY) * viewBeats * 0.5;
    scrollBar.setCurrentRangeStart (viewStart + amount, juce::sendNotificationSync);
}

bool ProgressionLane::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return selectedUid == 0;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        undo();
        return true;
    }
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))
    {
        duplicateSelected();
        return true;
    }
    return false;   // el resto (teclado de la PC como piano) lo maneja el editor
}

void ProgressionLane::deleteSelected()
{
    if (selectedUid == 0)
        return;
    const auto uid = selectedUid;
    processor.withProgression ([uid] (ProgressionModel& m) { m.removeBlock (m.indexOfUid (uid)); });
    selectedUid = 0;
    processor.progressionEdited();
}

void ProgressionLane::duplicateSelected()
{
    if (selectedUid == 0)
        return;
    const auto uid = selectedUid;
    uint32_t newUid = 0;
    processor.withProgression ([uid, &newUid] (ProgressionModel& m)
    {
        const int index = m.duplicateBlock (m.indexOfUid (uid));
        if (index >= 0)
            newUid = m.getBlocks()[(size_t) index].uid;
    });
    if (newUid != 0)
        selectedUid = newUid;
    processor.progressionEdited();
}

void ProgressionLane::undo()
{
    processor.withProgression ([] (ProgressionModel& m) { m.undo(); });
    processor.progressionEdited();
}

void ProgressionLane::appendChord (const PaletteCell& cell)
{
    if (! cell.valid)
        return;

    const double length = NewChordLength::getBeats (processor.getNewChordLengthIndex(), getBeatsPerBar());
    std::array<int, ChordBlock::kMaxNotes> vels {};
    vels.fill (100);
    const auto block = ProgressionModel::makeBlock (cell.notes.data(), vels.data(), cell.count, 0.0, length);

    uint32_t newUid = 0;
    processor.withProgression ([&block, &newUid] (ProgressionModel& m)
    {
        const int index = m.append (block);
        if (index >= 0)
            newUid = m.getBlocks()[(size_t) index].uid;
    });
    if (newUid != 0)
        selectedUid = newUid;
    processor.progressionEdited();
}

void ProgressionLane::showContextMenu (int viewIndex)
{
    juce::PopupMenu menu;
    const bool hasBlock = juce::isPositiveAndBelow (viewIndex, (int) views.size());
    const bool canUndo = processor.withProgression ([] (ProgressionModel& m) { return m.canUndo(); });

    menu.addItem (1, u8 ("Duplicar a continuación"), hasBlock);
    menu.addItem (2, u8 ("Borrar acorde"), hasBlock);
    menu.addSeparator();
    menu.addItem (3, u8 ("Deshacer"), canUndo);
    menu.addItem (4, u8 ("Borrar toda la progresión"), ! views.empty());

    juce::Component::SafePointer<ProgressionLane> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [safe] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            switch (result)
                            {
                                case 1: safe->duplicateSelected(); break;
                                case 2: safe->deleteSelected(); break;
                                case 3: safe->undo(); break;
                                case 4:
                                    safe->processor.withProgression ([] (ProgressionModel& m) { m.clear(); });
                                    safe->selectedUid = 0;
                                    safe->processor.progressionEdited();
                                    break;
                                default: break;
                            }
                        });
}

//==============================================================================
bool ProgressionLane::decodeChord (const juce::String& text, std::array<int, ChordBlock::kMaxNotes>& notes, int& count)
{
    count = 0;
    if (! text.startsWith ("skchord:"))
        return false;
    juce::StringArray tokens;
    tokens.addTokens (text.fromFirstOccurrenceOf ("skchord:", false, false), ",", "");
    for (const auto& t : tokens)
        if (t.isNotEmpty() && count < ChordBlock::kMaxNotes)
            notes[(size_t) count++] = juce::jlimit (0, 127, t.getIntValue());
    return count > 0;
}

bool ProgressionLane::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith ("skchord:");
}

void ProgressionLane::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void ProgressionLane::itemDragMove (const SourceDetails& details)
{
    const double grid = getGrid();
    double t = juce::jmax (0.0, std::round (xToBeat ((float) details.localPosition.x) / grid) * grid);
    for (const auto& v : views)
    {
        if (t > v.start + 1.0e-9 && t < v.start + v.length - 1.0e-9)
        {
            t = (xToBeat ((float) details.localPosition.x) - v.start) < v.length * 0.5 ? v.start : v.start + v.length;
            break;
        }
    }
    t = juce::jmin (t, contentEnd);   // nunca deja huecos al final de la progresión
    if (std::abs (t - dropBeat) > 1.0e-9)
    {
        dropBeat = t;
        repaint();
    }
}

void ProgressionLane::itemDragExit (const SourceDetails&)
{
    dropBeat = -1.0;
    repaint();
}

void ProgressionLane::itemDropped (const SourceDetails& details)
{
    std::array<int, ChordBlock::kMaxNotes> notes {};
    int count = 0;
    const double at = dropBeat >= 0.0 ? dropBeat : contentEnd;
    dropBeat = -1.0;

    if (! decodeChord (details.description.toString(), notes, count))
    {
        repaint();
        return;
    }

    const double length = NewChordLength::getBeats (processor.getNewChordLengthIndex(), getBeatsPerBar());
    std::array<int, ChordBlock::kMaxNotes> vels {};
    vels.fill (100);
    const auto block = ProgressionModel::makeBlock (notes.data(), vels.data(), count, at, length);

    uint32_t newUid = 0;
    processor.withProgression ([&block, at, &newUid] (ProgressionModel& m)
    {
        const int index = m.insertAt (block, at, 0.0);
        if (index >= 0)
            newUid = m.getBlocks()[(size_t) index].uid;
    });
    if (newUid != 0)
        selectedUid = newUid;
    processor.progressionEdited();
    grabKeyboardFocus();
}

} // namespace smartkeys::ui
