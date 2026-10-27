/*
  ==============================================================================

    ComputerKeyboard.cpp
    SmartKeys (MIWN) — El teclado de la computadora como teclado de notas.
    Lo común, Windows y Linux (macOS: ComputerKeyboard_mac.mm).

  ==============================================================================
*/

#include "ComputerKeyboard.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif JUCE_LINUX || JUCE_BSD
 #include <X11/Xlib.h>
 #include <X11/keysym.h>
#endif

namespace smartkeys
{

//==============================================================================
int ComputerKeyboard::noteKeyIndex (juce::juce_wchar c) noexcept
{
    c = juce::CharacterFunctions::toLowerCase (c);
    for (int i = 0; i < kNumNoteKeys; ++i)
        if ((juce::juce_wchar) kNoteKeys[i] == c)
            return i;
    return -1;
}

int ComputerKeyboard::octaveDelta (juce::juce_wchar c) noexcept
{
    c = juce::CharacterFunctions::toLowerCase (c);
    return c == 'z' ? -1 : (c == 'x' ? 1 : 0);
}

bool ComputerKeyboard::allowed() const
{
    if (isAllowed != nullptr && ! isAllowed())
        return false;

    // Un campo de texto de JUCE con el foco en la ventana de SmartKeys: se está escribiendo
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (auto* text = dynamic_cast<juce::TextInputTarget*> (focused))
            if (text->isTextInputActive())
                return false;

    return true;
}

void ComputerKeyboard::press (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumNoteKeys) || sounding[(size_t) i] || ! allowed())
        return;

    sounding[(size_t) i] = true;
    if (onNoteKey != nullptr)
        onNoteKey (i, true);
}

void ComputerKeyboard::release (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumNoteKeys) || ! sounding[(size_t) i])
        return;

    sounding[(size_t) i] = false;
    if (onNoteKey != nullptr)
        onNoteKey (i, false);
}

void ComputerKeyboard::releaseAll()
{
    for (int i = 0; i < kNumNoteKeys; ++i)
        release (i);
}

bool ComputerKeyboard::readKeys (bool canPlay, bool modifiersDown, const std::function<bool (juce::juce_wchar)>& isKeyDown)
{
    bool changed = false;

    for (int k = 0; k < kNumNoteKeys + 2; ++k)
    {
        const auto c = k < kNumNoteKeys ? (juce::juce_wchar) kNoteKeys[k] : (juce::juce_wchar) (k == kNumNoteKeys ? 'z' : 'x');
        const bool down = isKeyDown (c);
        const bool was = held[(size_t) k];
        held[(size_t) k] = down;
        if (down == was)
            continue;

        if (k < kNumNoteKeys)
        {
            if (! down && sounding[(size_t) k])
            {
                release (k);
                changed = true;
            }
            else if (down && canPlay && ! modifiersDown)
            {
                press (k);   // sólo una pulsación nueva (suelta -> pulsada) empieza una nota
                changed = changed || sounding[(size_t) k];
            }
        }
        else if (down && canPlay && ! modifiersDown && onOctave != nullptr)
        {
            onOctave (k == kNumNoteKeys ? -1 : 1);
        }
    }

    // El teclado ya no llega a la ventana (o no se puede tocar): lo que sonaba se suelta
    if (! canPlay)
    {
        for (int i = 0; i < kNumNoteKeys; ++i)
        {
            if (sounding[(size_t) i])
            {
                release (i);
                changed = true;
            }
        }
    }
    return changed;
}

//==============================================================================
#if JUCE_WINDOWS

// Windows: el estado físico de las teclas mientras el teclado está en la ventana de SmartKeys o
// dentro de ella (la ventana nativa del plugin cargado es hija suya)
struct ComputerKeyboard::Native
{
    explicit Native (juce::Component& c) : owner (c) {}

    // 0: el teclado está en otra parte; 1: en SmartKeys (o en el plugin); 2: ahí, pero en un campo de texto
    int keyboardState() const
    {
        auto* peer = owner.getPeer();
        const auto ours = peer != nullptr ? (HWND) peer->getNativeHandle() : nullptr;
        const auto foreground = GetForegroundWindow();
        if (ours == nullptr || foreground == nullptr)
            return 0;

        GUITHREADINFO info {};
        info.cbSize = sizeof (info);
        if (! GetGUIThreadInfo (GetWindowThreadProcessId (foreground, nullptr), &info))
            return 0;

        const auto focus = info.hwndFocus;
        if (focus == nullptr || ! (focus == ours || IsChild (ours, focus)))
            return 0;

        // Un cursor de texto parpadeando o un control de edición: se está escribiendo
        if (info.hwndCaret != nullptr)
            return 2;
        wchar_t className[64] = {};
        GetClassNameW (focus, className, 63);
        const juce::String name (className);
        if (name.equalsIgnoreCase ("Edit") || name.startsWithIgnoreCase ("RichEdit"))
            return 2;
        return 1;
    }

    static bool isDown (int virtualKey) noexcept   { return (GetAsyncKeyState (virtualKey) & 0x8000) != 0; }

    juce::Component& owner;
};

ComputerKeyboard::ComputerKeyboard (juce::Component& window)
    : owner (window), native (std::make_unique<Native> (window))
{
}

ComputerKeyboard::~ComputerKeyboard() = default;

bool ComputerKeyboard::poll()
{
    const bool canPlay = native->keyboardState() == 1 && allowed();
    const bool modifiers = Native::isDown (VK_CONTROL) || Native::isDown (VK_MENU)
                        || Native::isDown (VK_LWIN) || Native::isDown (VK_RWIN);
    return readKeys (canPlay, modifiers, [] (juce::juce_wchar c)
    {
        return Native::isDown ((int) juce::CharacterFunctions::toUpperCase (c));   // 'A'..'Z' = VK_A..VK_Z
    });
}

//==============================================================================
#elif JUCE_LINUX || JUCE_BSD

// Linux: el estado de las teclas del servidor X mientras el foco del teclado está en la ventana
// de SmartKeys o dentro de ella (la ventana X del plugin cargado está metida en ella). Con su
// propia conexión al servidor (sólo hilo de mensajes).
struct ComputerKeyboard::Native
{
    explicit Native (juce::Component& c) : owner (c), display (XOpenDisplay (nullptr)) {}

    ~Native()
    {
        if (display != nullptr)
            XCloseDisplay (display);
    }

    bool focusIsInside() const
    {
        auto* peer = owner.getPeer();
        if (peer == nullptr || display == nullptr)
            return false;

        const auto ours = (::Window) (juce::pointer_sized_uint) peer->getNativeHandle();
        ::Window focus = 0;
        int revert = 0;
        XGetInputFocus (display, &focus, &revert);

        // De la ventana con el foco hacia arriba, hasta la raíz
        for (int depth = 0; depth < 64 && focus != None && focus != PointerRoot; ++depth)
        {
            if (focus == ours)
                return true;

            ::Window root = 0, parent = 0;
            ::Window* children = nullptr;
            unsigned int numChildren = 0;
            if (XQueryTree (display, focus, &root, &parent, &children, &numChildren) == 0)
                return false;
            if (children != nullptr)
                XFree (children);
            if (parent == 0 || parent == root)
                return false;
            focus = parent;
        }
        return false;
    }

    void readState()
    {
        if (display != nullptr)
            XQueryKeymap (display, keys);
    }

    bool isDown (KeySym symbol) const
    {
        if (display == nullptr)
            return false;
        const auto code = XKeysymToKeycode (display, symbol);
        return code != 0 && (keys[code >> 3] & (1 << (code & 7))) != 0;
    }

    juce::Component& owner;
    Display* display = nullptr;
    char keys[32] {};
};

ComputerKeyboard::ComputerKeyboard (juce::Component& window)
    : owner (window), native (std::make_unique<Native> (window))
{
}

ComputerKeyboard::~ComputerKeyboard() = default;

bool ComputerKeyboard::poll()
{
    native->readState();
    const bool canPlay = native->focusIsInside() && allowed();
    const bool modifiers = native->isDown (XK_Control_L) || native->isDown (XK_Control_R)
                        || native->isDown (XK_Alt_L) || native->isDown (XK_Alt_R)
                        || native->isDown (XK_Super_L) || native->isDown (XK_Super_R);
    return readKeys (canPlay, modifiers, [this] (juce::juce_wchar c)
    {
        return native->isDown ((KeySym) c);   // 'a'..'z' = XK_a..XK_z
    });
}

//==============================================================================
#elif ! JUCE_MAC

// Otros sistemas: los eventos de teclado de JUCE (mientras la ventana de SmartKeys tiene el foco)
struct ComputerKeyboard::Native {};

ComputerKeyboard::ComputerKeyboard (juce::Component& window) : owner (window) {}
ComputerKeyboard::~ComputerKeyboard() = default;

bool ComputerKeyboard::poll()
{
    const bool canPlay = owner.hasKeyboardFocus (true) && allowed();
    const auto mods = juce::ModifierKeys::currentModifiers;
    return readKeys (canPlay, mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown(), [] (juce::juce_wchar c)
    {
        return juce::KeyPress::isKeyCurrentlyDown ((int) c);
    });
}

#endif

} // namespace smartkeys
