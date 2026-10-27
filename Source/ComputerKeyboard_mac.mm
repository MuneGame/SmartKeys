/*
  ==============================================================================

    ComputerKeyboard_mac.mm
    SmartKeys (MIWN) — El teclado de la computadora en macOS.

    Un monitor local de eventos (NSEvent) ve cada tecla que la aplicación (FL
    Studio, la app Standalone…) entrega, ANTES de que llegue a la vista que
    tiene el foco. Así una tecla pulsada con el foco en SmartKeys toca su
    acorde aunque ese foco lo tenga la interfaz del plugin cargado (que está
    DENTRO de la vista de SmartKeys: después de tocar una perilla o elegir un
    preset), y una tecla soltada con otra ventana delante se suelta igual.
    Sólo cuenta si lo que tiene el foco es SmartKeys o algo dentro de ella: si
    el DAW muestra el plugin dentro de su propia ventana, lo que se escriba en
    el resto de esa ventana (el Piano Roll, la lista de reproducción…) no toca
    acordes. El monitor sólo mira: nunca se queda con la tecla (la vista que
    tiene el foco la recibe como siempre).

  ==============================================================================
*/

#include "ComputerKeyboard.h"

#import <AppKit/AppKit.h>

namespace smartkeys
{

namespace
{
    // ¿Lo que tiene el foco es un campo de texto en el que se está escribiendo?
    bool isTypingText (NSResponder* responder)
    {
        if (responder == nil)
            return false;

        // Campos de texto de Cocoa (el editor de campo de un NSTextField es un NSText; VSTGUI y
        // muchos plugins usan un NSTextField para escribir)
        if ([responder isKindOfClass: [NSText class]] || [responder isKindOfClass: [NSTextField class]])
            return true;

        // Vistas de JUCE (SmartKeys o un plugin hecho con JUCE): sólo con un TextEditor con el
        // foco tienen una selección de texto
        if ([NSStringFromClass ([responder class]) hasPrefix: @"JUCEView"]
             && [responder respondsToSelector: @selector (selectedRange)])
        {
            const NSRange range = [(id<NSTextInputClient>) responder selectedRange];
            return range.location != NSNotFound;
        }

        return false;
    }
}

//==============================================================================
struct ComputerKeyboard::Native
{
    explicit Native (ComputerKeyboard& k) : keyboard (k)
    {
        // El bloque usa 'this' sin retenerlo: el monitor se quita en el destructor, antes de que
        // 'this' deje de existir (todo en el hilo principal)
        auto* self = this;
        id handler = [NSEvent addLocalMonitorForEventsMatchingMask: (NSEventMaskKeyDown | NSEventMaskKeyUp)
                                                           handler: ^NSEvent* (NSEvent* event)
                                                           {
                                                               self->handle (event);
                                                               return event;   // la tecla sigue su camino
                                                           }];
       #if __has_feature(objc_arc)
        monitor = handler;
       #else
        monitor = [handler retain];
       #endif
    }

    ~Native()
    {
        if (monitor != nil)
        {
            [NSEvent removeMonitor: monitor];
           #if ! __has_feature(objc_arc)
            [monitor release];
           #endif
            monitor = nil;
        }
    }

    // La vista de SmartKeys (la de su editor; la interfaz del plugin cargado está dentro de ella)
    NSView* ownerView() const
    {
        if (auto* peer = keyboard.owner.getPeer())
            return (NSView*) peer->getNativeHandle();
        return nil;
    }

    // Lo que tiene el foco de 'window' es SmartKeys o algo dentro de ella (la interfaz del plugin)
    static bool focusIsInside (NSWindow* window, NSView* view)
    {
        NSResponder* responder = [window firstResponder];
        return responder != nil
            && [responder isKindOfClass: [NSView class]]
            && [(NSView*) responder isDescendantOf: view];
    }

    void handle (NSEvent* event)
    {
        NSString* characters = [event charactersIgnoringModifiers];
        if (characters == nil || [characters length] == 0)
            return;

        const auto c = (juce::juce_wchar) [characters characterAtIndex: 0];
        const int key = ComputerKeyboard::noteKeyIndex (c);
        const int octave = ComputerKeyboard::octaveDelta (c);
        if (key < 0 && octave == 0)
            return;

        if ([event type] == NSEventTypeKeyUp)
        {
            if (key >= 0)
                keyboard.release (key);   // se suelta aunque sea con otra ventana delante
            return;
        }

        // Una pulsación nueva (no la repetición de una tecla mantenida), con el foco en SmartKeys
        // (o en el plugin, dentro de ella), sin Cmd / Ctrl / Opción (atajos) y sin estar
        // escribiendo en un campo de texto
        if ([event isARepeat])
            return;

        auto* view = ownerView();
        auto* window = view != nil ? [view window] : nil;
        if (window == nil || [event window] != window || ! focusIsInside (window, view))
            return;

        const auto flags = [event modifierFlags];
        if ((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption)) != 0)
            return;

        if (isTypingText ([window firstResponder]))
            return;

        if (key >= 0)
            keyboard.press (key);
        else if (keyboard.allowed() && keyboard.onOctave != nullptr)
            keyboard.onOctave (octave);
    }

    ComputerKeyboard& keyboard;
    id monitor = nil;
};

//==============================================================================
ComputerKeyboard::ComputerKeyboard (juce::Component& window)
    : owner (window)
{
    native = std::make_unique<Native> (*this);
}

ComputerKeyboard::~ComputerKeyboard()
{
    native.reset();   // primero el monitor: después ya no llega ninguna tecla
}

bool ComputerKeyboard::poll()
{
    // La aplicación pasó a segundo plano (los KeyUp ya no llegarán) o no se puede tocar ahora
    if ([NSApp isActive] && allowed())
        return false;

    bool any = false;
    for (auto s : sounding)
        any = any || s;
    releaseAll();
    return any;
}

} // namespace smartkeys
