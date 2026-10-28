/*
  ==============================================================================

    EditorTests.cpp
    SmartKeys 2.0.2 — prueba de la ventana de SmartKeys como en un DAW: el
    VST3 de SmartKeys con su interfaz abierta (en una ventana de este
    "DAW") y el instrumento de prueba (Tests/TestSynth.cpp) en su ranura. Se
    hace clic y se pulsan teclas de verdad (macOS: eventos de AppKit; Windows:
    mensajes del ratón y SendInput; Linux: xdotool bajo Xvfb).

      1. VIEW: SYNTH (guardada con el proyecto): la ventana de SmartKeys se abre
         con la interfaz del instrumento DENTRO (850 x 106+320) y ninguna otra.
      2. Un clic en el ZOOM del instrumento cambia su tamaño y la ventana de
         SmartKeys lo sigue (1000 x 106+640) y vuelve.
      3. Con el foco en la interfaz del instrumento (tras el clic), el teclado
         de la computadora toca acordes en él.
      4. VIEW SMARTKEYS / SYNTH (clics): el tamaño cambia al instante y la
         interfaz del instrumento no se vuelve a crear.
      5. Al cerrar la ventana de SmartKeys se cierra la interfaz del
         instrumento, que sigue sonando; al abrirla otra vez, vuelve SYNTH.
      6. (macOS, --au) Lo mismo con el instrumento de prueba como Audio Unit.
      7. (macOS) El teclado de la computadora pieza por pieza: foco en
         SmartKeys, en el plugin, en un campo de texto, en otra vista del DAW
         de la misma ventana, en otra ventana; Cmd / Ctrl; repeticiones.
      8. Doble clic en una perilla de SmartKeys (STRUM, GAIN, GATE): vuelve a
         su valor por defecto y el DAW recibe un solo gesto.

    Uso: SmartKeysEditorTests <SmartKeys.vst3> <SmartKeysTestSynth.vst3> [--au]

  ==============================================================================
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "UiCommon.h"   // smartkeys::ui::PolledSliderAttachment (las perillas de SmartKeys)
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_LINUX || JUCE_BSD
// (sin <X11/Xlib.h>: su "Time" choca con juce::Time)
extern "C"
{
    struct _XDisplay;
    typedef int (*SkXErrorHandler) (struct _XDisplay*, void*);
    SkXErrorHandler XSetErrorHandler (SkXErrorHandler);
}
#endif

using namespace juce;

//==============================================================================
// Lo que hace cada sistema (macOS: EditorTests_mac.mm)
namespace edtest
{
    void prepareApplication();
    bool click (Component& inWindow, Point<int> screenPosition);
    bool key (Component& inWindow, char c, bool isDown);   // false: esta máquina no deja simular teclas
    int visibleWindows();                                   // ventanas grandes visibles de este proceso (-1: no se sabe)
   #if JUCE_MAC
    void runKeyboardUnitTests (const std::function<void (bool, const char*)>& check);
   #endif
}

namespace
{
int numChecks = 0, numFailures = 0;

void check (bool ok, const char* what)
{
    ++numChecks;
    if (! ok) ++numFailures;
    std::cout << (ok ? "  ok     " : "  FALLO  ") << what << std::endl;
}

#if JUCE_LINUX || JUCE_BSD
// Linux: aquí conviven tres copias de JUCE (este "DAW", SmartKeys y el instrumento), y cada una
// cambia el manejador de errores de X11 del proceso (al cerrarse, deja el de Xlib, que termina el
// programa con cualquier error). Como hace un DAW, los errores de X11 sólo se apuntan.
int lenientXErrorHandler (struct _XDisplay*, void*)
{
    return 0;
}
#endif

void pump (int ms)
{
   #if JUCE_LINUX || JUCE_BSD
    XSetErrorHandler (lenientXErrorHandler);
   #endif
    MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

// Lo que el "DAW" hace por su cuenta (abrir y cerrar la ventana de SmartKeys) pasa, como en un
// DAW de verdad, dentro de una vuelta de su bucle de eventos: en macOS, con su "autorelease
// pool" (lo que AppKit deja pendiente se suelta al terminar, no al cerrar el programa)
template <typename Fn>
void dawAction (Fn&& fn)
{
    JUCE_AUTORELEASEPOOL
    {
        fn();
    }
}

File gSynthLog;

StringArray synthLines()
{
    StringArray lines;
    gSynthLog.readLines (lines);
    lines.removeEmptyStrings();
    return lines;
}

int countLines (const String& text)
{
    int n = 0;
    for (auto& l : synthLines())
        n += l.contains (text) ? 1 : 0;
    return n;
}

// Notas que empezaron (NoteOn) desde la línea 'from'
Array<int> noteOnsSince (int from)
{
    Array<int> notes;
    const auto lines = synthLines();
    for (int i = jmax (0, from); i < lines.size(); ++i)
    {
        StringArray t;
        t.addTokens (lines[i], " ", {});
        if (t.size() >= 6 && t[1] == "EV")
        {
            const int status = t[3].getIntValue(), d1 = t[4].getIntValue(), d2 = t[5].getIntValue();
            if ((status & 0xF0) == 0x90 && d2 > 0)
                notes.addIfNotAlreadyThere (d1);
        }
    }
    notes.sort();
    return notes;
}

//==============================================================================
// El "DAW": SmartKeys (VST3) procesando audio en su propio hilo, como en FL Studio
class Daw
{
public:
    ~Daw()   { stop(); }

    bool load (AudioPluginFormatManager& formats, const String& path)
    {
        OwnedArray<PluginDescription> types;
        for (auto* f : formats.getFormats())
            if (f->getName() == "VST3")
                f->findAllTypesForFile (types, path);
        if (types.isEmpty())
            return false;

        String error;
        plugin = formats.createPluginInstance (*types[0], 48000.0, 512, error);
        if (plugin == nullptr)
        {
            std::cout << "No se pudo cargar SmartKeys: " << error << std::endl;
            return false;
        }
        plugin->enableAllBuses();
        plugin->prepareToPlay (48000.0, 512);
        running = true;
        audio = std::thread ([this] { audioLoop(); });
        return true;
    }

    void stop()
    {
        running = false;
        if (audio.joinable())
            audio.join();
        if (plugin != nullptr)
            plugin->releaseResources();
    }

    AudioPluginInstance& instance()   { return *plugin; }

    // Una nota que llega del DAW (el Piano Roll), en el próximo bloque
    void sendNote (int note, bool on)
    {
        const SpinLock::ScopedLockType sl (pendingLock);
        pending.addEvent (on ? MidiMessage::noteOn (1, note, (uint8) 100) : MidiMessage::noteOff (1, note), 0);
    }

    // Estado de SmartKeys (el host VST3 lo envuelve en <VST3PluginState><IComponent>base64</...>)
    std::unique_ptr<XmlElement> getStateXml()
    {
        MemoryBlock state;
        plugin->getStateInformation (state);
        auto xml = AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        if (xml != nullptr && xml->hasTagName ("VST3PluginState"))
            if (auto* component = xml->getChildByName ("IComponent"))
            {
                MemoryBlock data;
                if (data.fromBase64Encoding (component->getAllSubText().trim()))
                    return AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize());
            }
        return nullptr;
    }

    void setStateXml (const XmlElement& inner)
    {
        MemoryBlock state;
        plugin->getStateInformation (state);
        auto wrapper = AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        if (wrapper == nullptr || ! wrapper->hasTagName ("VST3PluginState"))
            return;
        if (auto* component = wrapper->getChildByName ("IComponent"))
        {
            MemoryBlock innerData;
            AudioProcessor::copyXmlToBinary (inner, innerData);
            component->deleteAllTextElements();
            component->addTextElement (innerData.toBase64Encoding());
            MemoryBlock out;
            AudioProcessor::copyXmlToBinary (*wrapper, out);
            plugin->setStateInformation (out.getData(), (int) out.getSize());
        }
    }

    // ¿Está cargado en la ranura este instrumento (en este formato)?
    bool isLoadedInSlot (const PluginDescription& d)
    {
        if (auto xml = getStateXml())
            if (auto* h = xml->getChildByName ("HostedPlugin"))
                if (h->getStringAttribute ("status") == "loaded")
                    if (auto* p = h->getChildByName ("PLUGIN"))
                        return p->getStringAttribute ("format") == d.pluginFormatName
                            && p->getStringAttribute ("name") == d.name;
        return false;
    }

private:
    void audioLoop()
    {
        AudioBuffer<float> buffer (jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), 512);
        MidiBuffer midi;
        midi.ensureSize (8192);
        auto next = Time::getMillisecondCounterHiRes();
        while (running)
        {
            buffer.clear();
            midi.clear();
            {
                const SpinLock::ScopedLockType sl (pendingLock);
                midi.swapWith (pending);
            }
            plugin->processBlock (buffer, midi);

            next += 512.0 * 1000.0 / 48000.0;
            const auto wait = next - Time::getMillisecondCounterHiRes();
            if (wait > 0)
                Thread::sleep ((int) wait);
            else
                next = Time::getMillisecondCounterHiRes();
        }
    }

    std::unique_ptr<AudioPluginInstance> plugin;
    std::thread audio;
    std::atomic<bool> running { false };
    SpinLock pendingLock;
    MidiBuffer pending;
};

//==============================================================================
// La ventana del "DAW" con la interfaz de SmartKeys dentro (se ajusta a su tamaño)
class PluginWindow : public DocumentWindow
{
public:
    explicit PluginWindow (AudioProcessorEditor* e)
        : DocumentWindow ("SmartKeys (prueba)", Colours::darkgrey, 0), editor (e)
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (editor.get(), true);
        setTopLeftPosition (60, 80);
        setVisible (true);
        toFront (true);
    }

    ~PluginWindow() override
    {
        clearContentComponent();
        editor = nullptr;   // la interfaz de SmartKeys se destruye antes que la ventana
    }

    AudioProcessorEditor& getEditor()   { return *editor; }

private:
    std::unique_ptr<AudioProcessorEditor> editor;
};

juce::Rectangle<int> editorScreenBounds (PluginWindow& w)
{
    return w.getEditor().getScreenBounds();
}

bool waitForEditorSize (PluginWindow& w, int width, int height, int timeoutMs = 4000)
{
    const auto end = Time::getMillisecondCounterHiRes() + timeoutMs;
    while (Time::getMillisecondCounterHiRes() < end)
    {
        if (w.getEditor().getWidth() == width && w.getEditor().getHeight() == height)
            return true;
        pump (30);
    }
    std::cout << "     (tamaño de la ventana de SmartKeys: " << w.getEditor().getWidth() << " x "
              << w.getEditor().getHeight() << ", se esperaba " << width << " x " << height << ")" << std::endl;
    return false;
}

bool waitForLines (const String& text, int count, int timeoutMs = 5000)
{
    const auto end = Time::getMillisecondCounterHiRes() + timeoutMs;
    while (Time::getMillisecondCounterHiRes() < end)
    {
        if (countLines (text) >= count)
            return true;
        pump (30);
    }
    return countLines (text) >= count;
}

// Un clic dentro de la ventana de SmartKeys, en coordenadas de su interfaz
void clickEditor (PluginWindow& w, int x, int y)
{
    const auto origin = editorScreenBounds (w).getTopLeft();
    edtest::click (w, origin + Point<int> (x, y));
    pump (350);
}

// La cabecera de SmartKeys (850 de ancho) va centrada cuando la ventana es más ancha
void clickHeader (PluginWindow& w, int x, int y)
{
    clickEditor (w, x + jmax (0, roundToInt ((w.getEditor().getWidth() - 850) * 0.5)), y);
}

//==============================================================================
// 8. Doble clic en una perilla: juce::Slider avisa del doble clic con un gesto DENTRO del arrastre
//    que ya empezó el segundo clic (mouseDown -> mouseDoubleClick -> mouseUp). El enlace de
//    SmartKeys manda al DAW un solo gesto.
struct KnobProcessor final : public AudioProcessor
{
    KnobProcessor()
    {
        addParameter (strum = new AudioParameterFloat (ParameterID { "strum", 1 }, "Strum", 0.0f, 200.0f, 0.0f));
    }

    using AudioProcessor::processBlock;

    const String getName() const override                         { return "Knob"; }
    void prepareToPlay (double, int) override                      {}
    void releaseResources() override                               {}
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override  {}
    double getTailLengthSeconds() const override                   { return 0.0; }
    bool acceptsMidi() const override                              { return false; }
    bool producesMidi() const override                             { return false; }
    AudioProcessorEditor* createEditor() override                  { return nullptr; }
    bool hasEditor() const override                                { return false; }
    int getNumPrograms() override                                  { return 1; }
    int getCurrentProgram() override                               { return 0; }
    void setCurrentProgram (int) override                          {}
    const String getProgramName (int) override                     { return {}; }
    void changeProgramName (int, const String&) override           {}
    void getStateInformation (MemoryBlock&) override               {}
    void setStateInformation (const void*, int) override           {}

    AudioParameterFloat* strum = nullptr;
};

struct GestureCounter final : public AudioProcessorParameter::Listener
{
    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int, bool starting) override
    {
        ++(starting ? begins : ends);
        depth += starting ? 1 : -1;
        maxDepth = jmax (maxDepth, depth);
    }

    int begins = 0, ends = 0, depth = 0, maxDepth = 0;
};

void testKnobDoubleClick()
{
    std::cout << "\n-- Doble clic en una perilla de SmartKeys (STRUM, GAIN, GATE)" << std::endl;

    KnobProcessor processor;
    GestureCounter gestures;
    processor.strum->addListener (&gestures);

    Slider knob (Slider::RotaryHorizontalVerticalDrag, Slider::NoTextBox);
    auto attachment = std::make_unique<smartkeys::ui::PolledSliderAttachment> (*processor.strum, knob);

    {
        Slider::ScopedDragNotification drag (knob);   // un arrastre normal
        knob.setValue (150.0, sendNotificationSync);
    }
    check (gestures.begins == 1 && gestures.ends == 1 && approximatelyEqual (processor.strum->get(), 150.0f),
           "arrastrar la perilla: un gesto para el DAW y el valor nuevo (150 ms)");

    {
        auto secondClick = std::make_unique<Slider::ScopedDragNotification> (knob);   // mouseDown del segundo clic
        {
            Slider::ScopedDragNotification doubleClick (knob);                      // mouseDoubleClick
            knob.setValue (knob.getDoubleClickReturnValue(), sendNotificationSync);
        }
        secondClick = nullptr;                                                       // mouseUp
    }
    check (approximatelyEqual (processor.strum->get(), 0.0f), "doble clic: la perilla vuelve a su valor por defecto (0 ms)");
    check (gestures.begins == 2 && gestures.ends == 2 && gestures.maxDepth == 1,
           "y el DAW recibe un solo gesto (ninguno dentro de otro)");

    auto unfinished = std::make_unique<Slider::ScopedDragNotification> (knob);
    knob.setValue (80.0, sendNotificationSync);
    attachment = nullptr;   // la ventana se cierra a mitad del arrastre
    check (gestures.begins == 3 && gestures.ends == 3, "cerrar la ventana a mitad de un arrastre: el gesto termina igual");
    unfinished = nullptr;
    check (gestures.ends == 3 && gestures.depth == 0, "y el final del arrastre no manda otro");

    processor.strum->removeListener (&gestures);
}

constexpr int kHeader = 106;

//==============================================================================
// Una pasada completa con un instrumento (VST3 o Audio Unit) en la ranura
void testWithInstrument (Daw& daw, const PluginDescription& synth, const String& label)
{
    std::cout << "\n-- " << label << std::endl;

    // Como al abrir un proyecto guardado en la vista SYNTH: el instrumento en la ranura
    if (auto xml = daw.getStateXml())
    {
        xml->setAttribute ("view", "synth");
        xml->deleteAllChildElementsWithTagName ("HostedPlugin");
        auto* h = xml->createNewChildElement ("HostedPlugin");
        h->setAttribute ("bypass", 0);
        h->addChildElement (synth.createXml().release());
        daw.setStateXml (*xml);
    }
    bool loaded = false;
    for (int i = 0; i < 200 && ! loaded; ++i)
    {
        pump (50);
        loaded = daw.isLoadedInSlot (synth);
    }
    check (loaded, "el instrumento de prueba se carga en la ranura");
    if (! loaded)
        return;

    const int opened = countLines ("EDITOR OPEN"), closed = countLines ("EDITOR CLOSED");

    // 1. La ventana se abre en la vista SYNTH con la interfaz del instrumento dentro
    std::unique_ptr<PluginWindow> window;
    dawAction ([&] { window = std::make_unique<PluginWindow> (daw.instance().createEditorAndMakeActive()); });
    check (waitForEditorSize (*window, 850, kHeader + 320), "VIEW: SYNTH: la ventana de SmartKeys toma el tamaño del instrumento (850 x 106+320)");
    check (waitForLines ("EDITOR OPEN", opened + 1) && countLines ("EDITOR OPEN") == opened + 1,
           "la interfaz del instrumento se crea una sola vez");
    const int windows = edtest::visibleWindows();
    if (windows >= 0)
        check (windows == 1, "ninguna ventana aparte para el instrumento (una sola ventana: la de SmartKeys)");
    pump (500);

    // 2. ZOOM del instrumento: la ventana de SmartKeys lo sigue
    const int zoomed = countLines ("EDITOR SIZE 1000x640");
    clickEditor (*window, (850 - 520) / 2 + 80, kHeader + 136);
    check (waitForLines ("EDITOR SIZE 1000x640", zoomed + 1) && waitForEditorSize (*window, 1000, kHeader + 640),
           "un clic en el ZOOM del instrumento: la ventana de SmartKeys crece con él (1000 x 106+640)");

    // 3. Teclado con el foco en la interfaz del instrumento (después del clic)
    {
        const int from = synthLines().size();
        const bool pressed = edtest::key (*window, 'd', true);
        pump (250);
        edtest::key (*window, 'd', false);
        pump (300);
        if (pressed)
        {
            const auto notes = noteOnsSince (from);
            std::cout << "     D con el foco en el instrumento:";
            for (auto n : notes) std::cout << " " << n;
            std::cout << std::endl;
            check (notes == Array<int> { 64, 67, 71 },
                   "con el foco en la interfaz del instrumento, el teclado de la computadora toca acordes (E4 -> E G B)");
        }
        else
        {
            std::cout << "     (esta máquina no deja simular teclas de verdad: se omite)" << std::endl;
        }
    }

    clickEditor (*window, 80, kHeader + 136);   // ZOOM otra vez (el instrumento ocupa todo el ancho)
    check (waitForEditorSize (*window, 850, kHeader + 320), "y vuelve a encoger con él");

    // 4. SMARTKEYS / SYNTH
    clickHeader (*window, 387, 78);
    check (waitForEditorSize (*window, 850, 658), "VIEW: SMARTKEYS: 850 x 658 al instante");
    check (countLines ("EDITOR CLOSED") == closed, "la interfaz del instrumento no se destruye (queda oculta)");
    clickHeader (*window, 462, 78);
    check (waitForEditorSize (*window, 850, kHeader + 320) && countLines ("EDITOR OPEN") == opened + 1,
           "VIEW: SYNTH otra vez: la misma interfaz, sin crearla de nuevo");

    // 5. Cerrar la ventana de SmartKeys: el instrumento sigue sonando
    dawAction ([&] { window = nullptr; });
    pump (300);
    check (countLines ("EDITOR CLOSED") == closed + 1, "al cerrar SmartKeys se cierra la interfaz del instrumento");
    {
        const int from = synthLines().size();
        daw.sendNote (60, true);
        pump (300);
        daw.sendNote (60, false);
        pump (300);
        check (noteOnsSince (from) == Array<int> { 60, 64, 67 }, "y el instrumento sigue tocando los acordes (C4 -> C E G)");
    }

    dawAction ([&] { window = std::make_unique<PluginWindow> (daw.instance().createEditorAndMakeActive()); });
    check (waitForEditorSize (*window, 850, kHeader + 320) && waitForLines ("EDITOR OPEN", opened + 2),
           "al abrir SmartKeys otra vez vuelve a la vista SYNTH");
    dawAction ([&] { window = nullptr; });
    pump (300);
}
} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;

    if (argc < 3)
    {
        std::cout << "Uso: SmartKeysEditorTests <SmartKeys.vst3> <SmartKeysTestSynth.vst3> [--au]" << std::endl;
        return 2;
    }
    const auto cwd = File::getCurrentWorkingDirectory();
    const auto smartKeysPath = cwd.getChildFile (String::fromUTF8 (argv[1])).getFullPathName();
    const auto synthPath = cwd.getChildFile (String::fromUTF8 (argv[2])).getFullPathName();
    const bool withAU = argc > 3 && String (argv[3]) == "--au";

    const auto workDir = File::createTempFile ("smartkeys-editor");
    workDir.createDirectory();
    gSynthLog = workDir.getChildFile ("testsynth.log");
   #if JUCE_WINDOWS
    _putenv_s ("SMARTKEYS_TESTSYNTH_LOG", gSynthLog.getFullPathName().toRawUTF8());
    _putenv_s ("SMARTKEYS_USER_DATA", workDir.getChildFile ("userdata").getFullPathName().toRawUTF8());
   #else
    setenv ("SMARTKEYS_TESTSYNTH_LOG", gSynthLog.getFullPathName().toRawUTF8(), 1);
    setenv ("SMARTKEYS_USER_DATA", workDir.getChildFile ("userdata").getFullPathName().toRawUTF8(), 1);
   #endif

    edtest::prepareApplication();

    testKnobDoubleClick();

    AudioPluginFormatManager formats;
    addDefaultFormatsToManager (formats);

    {
        Daw daw;
        check (daw.load (formats, smartKeysPath), "SmartKeys (VST3) se carga en el \"DAW\"");
        if (numFailures == 0)
        {
            OwnedArray<PluginDescription> types;
            for (auto* f : formats.getFormats())
                if (f->getName() == "VST3")
                    f->findAllTypesForFile (types, synthPath);
            check (! types.isEmpty(), "el instrumento de prueba (VST3) se puede describir");
            if (! types.isEmpty())
                testWithInstrument (daw, *types[0], "Instrumento VST3 dentro de SmartKeys");

           #if JUCE_MAC
            if (withAU)
            {
                PluginDescription au;
                au.name = au.descriptiveName = "SmartKeysTestSynth";
                au.manufacturerName = "MIWN Test";
                au.pluginFormatName = "AudioUnit";
                au.category = "Synth";
                au.isInstrument = true;
                au.fileOrIdentifier = "AudioUnit:Synths/aumu,Tsyn,Miwt";
                au.numOutputChannels = 2;
                testWithInstrument (daw, au, "Instrumento Audio Unit dentro de SmartKeys");
            }
           #else
            ignoreUnused (withAU);
           #endif
        }
        daw.stop();
    }

   #if JUCE_MAC
    std::cout << "\n-- El teclado de la computadora, pieza por pieza (macOS)" << std::endl;
    edtest::runKeyboardUnitTests (check);
   #endif

    workDir.deleteRecursively();
    std::cout << "\n" << numChecks << " comprobaciones, " << numFailures << " fallos" << std::endl;
    return numFailures == 0 ? 0 : 1;
}

//==============================================================================
// Windows y Linux (macOS: EditorTests_mac.mm)
#if ! JUCE_MAC
namespace edtest
{
void prepareApplication() {}

 #if JUCE_WINDOWS
static BOOL CALLBACK countWindow (HWND h, LPARAM data)
{
    DWORD pid = 0;
    GetWindowThreadProcessId (h, &pid);
    RECT r {};
    if (pid == GetCurrentProcessId() && IsWindowVisible (h) && GetWindowRect (h, &r)
          && r.right - r.left > 100 && r.bottom - r.top > 100)
        ++*reinterpret_cast<int*> (data);
    return TRUE;
}

int visibleWindows()
{
    int n = 0;
    EnumWindows (countWindow, (LPARAM) &n);
    return n;
}
 #else
int visibleWindows()   { return -1; }   // Linux: lo comprueba la prueba de la app (keys202)
 #endif

 #if JUCE_WINDOWS
static Point<int> toPhysical (Point<int> p)
{
    return Desktop::getInstance().getDisplays().logicalToPhysical (p.toFloat()).roundToInt();
}

bool click (Component&, Point<int> screenPosition)
{
    // El cursor de verdad también (JUCE comprueba con él que el ratón sigue sobre el botón)
    const auto p = toPhysical (screenPosition);
    SetCursorPos (p.x, p.y);
    MessageManager::getInstance()->runDispatchLoopUntil (40);
    POINT pt { p.x, p.y };
    HWND target = WindowFromPoint (pt);
    if (target == nullptr)
        return false;
    ScreenToClient (target, &pt);
    const auto lParam = MAKELPARAM (pt.x, pt.y);
    SendMessage (target, WM_MOUSEMOVE, 0, lParam);
    SendMessage (target, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
    MessageManager::getInstance()->runDispatchLoopUntil (60);
    SendMessage (target, WM_LBUTTONUP, 0, lParam);
    return true;
}

bool key (Component& inWindow, char c, bool isDown)
{
    // Teclas de verdad (SendInput): sólo si esta máquina tiene escritorio y deja la ventana delante
    auto* peer = inWindow.getTopLevelComponent()->getPeer();
    const auto ours = peer != nullptr ? (HWND) peer->getNativeHandle() : nullptr;
    if (ours == nullptr)
        return false;
    if (isDown && GetForegroundWindow() != ours)
        SetForegroundWindow (ours);
    if (GetForegroundWindow() != ours)
        return false;

    INPUT input {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = (WORD) CharacterFunctions::toUpperCase ((juce_wchar) c);
    input.ki.dwFlags = isDown ? 0 : KEYEVENTF_KEYUP;
    if (SendInput (1, &input, sizeof (input)) != 1)
        return false;
    MessageManager::getInstance()->runDispatchLoopUntil (30);
    return ! isDown || (GetAsyncKeyState (input.ki.wVk) & 0x8000) != 0;
}
 #else
// Linux (bajo Xvfb): xdotool
bool click (Component&, Point<int> screenPosition)
{
    const auto p = Desktop::getInstance().getDisplays().logicalToPhysical (screenPosition.toFloat()).roundToInt();
    const auto cmd = "xdotool mousemove " + String (p.x) + " " + String (p.y) + " click 1";
    return std::system (cmd.toRawUTF8()) == 0;
}

bool key (Component&, char c, bool isDown)
{
    const auto cmd = String ("xdotool ") + (isDown ? "keydown " : "keyup ") + String::charToString ((juce_wchar) c);
    return std::system (cmd.toRawUTF8()) == 0;
}
 #endif
} // namespace edtest
#endif
