/*
  ==============================================================================

    EditorTests_mac.mm
    SmartKeys 2.0.2 — la parte de macOS de la prueba de la ventana: clics y
    teclas de verdad (eventos de AppKit que pasan por la aplicación igual que
    los del usuario) y la prueba pieza por pieza del teclado de la computadora
    (ComputerKeyboard) con una ventana "de DAW" hecha a mano.

  ==============================================================================
*/

#include <juce_gui_basics/juce_gui_basics.h>
#include "ComputerKeyboard.h"

#import <AppKit/AppKit.h>

#include <functional>
#include <iostream>
#include <vector>

// Una vista que acepta el foco del teclado (la de un plugin, la de otra parte del DAW)
@interface SKTestFocusableView : NSView
@end

@implementation SKTestFocusableView
- (BOOL) acceptsFirstResponder   { return YES; }
@end

namespace edtest
{
// (sin "using namespace juce": en macOS, Point y Component también son tipos de Carbon)

static void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

static NSWindow* windowOf (juce::Component& c)
{
    if (auto* peer = c.getTopLevelComponent()->getPeer())
        return [(NSView*) peer->getNativeHandle() window];
    return nil;
}

void prepareApplication()
{
    [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps: YES];
}

bool click (juce::Component& inWindow, juce::Point<int> screenPosition)
{
    NSWindow* window = windowOf (inWindow);
    if (window == nil)
        return false;

    [window makeKeyAndOrderFront: nil];
    [NSApp activateIgnoringOtherApps: YES];

    // Pantalla de JUCE (origen arriba a la izquierda) -> ventana de AppKit (origen abajo a la izquierda)
    const CGFloat mainHeight = [[[NSScreen screens] objectAtIndex: 0] frame].size.height;
    const NSPoint inScreen = NSMakePoint ((CGFloat) screenPosition.x, mainHeight - (CGFloat) screenPosition.y);
    const NSPoint location = [window convertPointFromScreen: inScreen];

    for (NSEventType type : { NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp })
    {
        NSEvent* e = [NSEvent mouseEventWithType: type
                                        location: location
                                   modifierFlags: 0
                                       timestamp: [[NSProcessInfo processInfo] systemUptime]
                                    windowNumber: [window windowNumber]
                                         context: nil
                                     eventNumber: 0
                                      clickCount: 1
                                        pressure: type == NSEventTypeLeftMouseDown ? 1.0f : 0.0f];
        [NSApp postEvent: e atStart: NO];
        pump (80);
    }
    return true;
}

static NSEvent* keyEvent (NSWindow* window, NSEventType type, NSString* characters, NSEventModifierFlags flags, BOOL repeat)
{
    return [NSEvent keyEventWithType: type
                            location: NSZeroPoint
                       modifierFlags: flags
                           timestamp: [[NSProcessInfo processInfo] systemUptime]
                        windowNumber: [window windowNumber]
                             context: nil
                          characters: characters
         charactersIgnoringModifiers: characters
                           isARepeat: repeat
                             keyCode: 0];
}

bool key (juce::Component& inWindow, char c, bool isDown)
{
    NSWindow* window = windowOf (inWindow);
    if (window == nil)
        return false;

    NSString* characters = [NSString stringWithFormat: @"%c", c];
    [NSApp postEvent: keyEvent (window, isDown ? NSEventTypeKeyDown : NSEventTypeKeyUp, characters, 0, NO) atStart: NO];
    pump (40);
    return true;
}

int visibleWindows()
{
    int n = 0;
    for (NSWindow* w in [NSApp windows])
        if ([w isVisible] && [w frame].size.width > 100 && [w frame].size.height > 100)
            ++n;
    return n;
}

//==============================================================================
// El teclado de la computadora pieza por pieza
void runKeyboardUnitTests (const std::function<void (bool, const char*)>& check)
{
    // Ventana "del DAW": una vista del propio DAW (su Piano Roll) y, al lado, la de SmartKeys
    NSWindow* window = [[NSWindow alloc] initWithContentRect: NSMakeRect (200, 200, 900, 500)
                                                   styleMask: NSWindowStyleMaskTitled
                                                     backing: NSBackingStoreBuffered
                                                       defer: NO];
    [window setReleasedWhenClosed: NO];
    NSView* content = [window contentView];
    SKTestFocusableView* dawView = [[SKTestFocusableView alloc] initWithFrame: NSMakeRect (0, 0, 200, 500)];
    NSView* holder = [[NSView alloc] initWithFrame: NSMakeRect (200, 0, 700, 500)];
    [content addSubview: dawView];
    [content addSubview: holder];
    [window makeKeyAndOrderFront: nil];

    NSWindow* other = [[NSWindow alloc] initWithContentRect: NSMakeRect (300, 300, 300, 200)
                                                  styleMask: NSWindowStyleMaskTitled
                                                    backing: NSBackingStoreBuffered
                                                      defer: NO];
    [other setReleasedWhenClosed: NO];

    // La interfaz de SmartKeys (un componente de JUCE dentro de la vista del DAW)
    juce::Component editor;
    editor.setSize (700, 500);
    editor.setWantsKeyboardFocus (true);
    juce::TextEditor presetName;   // un campo de texto de SmartKeys
    presetName.setBounds (400, 10, 200, 24);
    editor.addAndMakeVisible (presetName);
    editor.addToDesktop (0, (void*) holder);
    editor.setVisible (true);
    pump (100);

    NSView* juceView = (NSView*) editor.getPeer()->getNativeHandle();

    // La interfaz del plugin cargado: su vista nativa DENTRO de la de SmartKeys, con un campo de texto
    SKTestFocusableView* pluginView = [[SKTestFocusableView alloc] initWithFrame: NSMakeRect (10, 10, 300, 200)];
    [juceView addSubview: pluginView];
    NSTextField* pluginSearch = [[NSTextField alloc] initWithFrame: NSMakeRect (10, 10, 150, 24)];
    [pluginView addSubview: pluginSearch];

    struct Event { int key; bool down; };
    std::vector<Event> notes;
    std::vector<int> octaves;
    bool allowed = true;

    auto keyboard = std::make_unique<smartkeys::ComputerKeyboard> (editor);
    keyboard->onNoteKey = [&] (int k, bool d) { notes.push_back ({ k, d }); };
    keyboard->onOctave = [&] (int delta) { octaves.push_back (delta); };
    keyboard->isAllowed = [&] { return allowed; };

    auto post = [&] (NSWindow* w, NSEventType type, NSString* chars, NSEventModifierFlags flags = 0, BOOL repeat = NO)
    {
        [NSApp postEvent: keyEvent (w, type, chars, flags, repeat) atStart: NO];
        pump (60);
    };
    auto tap = [&] (NSWindow* w, NSString* chars, NSEventModifierFlags flags = 0)
    {
        post (w, NSEventTypeKeyDown, chars, flags);
        post (w, NSEventTypeKeyUp, chars, flags);
    };
    auto is = [&] (std::initializer_list<Event> expected)
    {
        if (notes.size() != expected.size())
            return false;
        size_t i = 0;
        for (auto& e : expected)
        {
            if (notes[i].key != e.key || notes[i].down != e.down)
                return false;
            ++i;
        }
        return true;
    };

    // 1. Foco en SmartKeys
    [window makeFirstResponder: juceView];
    notes.clear();
    post (window, NSEventTypeKeyDown, @"a");
    post (window, NSEventTypeKeyDown, @"a", 0, YES);   // repetición de la tecla mantenida
    post (window, NSEventTypeKeyUp, @"a");
    check (is ({ { 0, true }, { 0, false } }), "foco en SmartKeys: A toca y suelta (la repetición no dispara otra)");

    // 2. Foco en la interfaz del plugin (después de tocar una perilla o elegir un preset)
    [window makeFirstResponder: pluginView];
    notes.clear();
    tap (window, @"s");
    check (is ({ { 1, true }, { 1, false } }), "foco en la interfaz del plugin (dentro de SmartKeys): S toca igual");

    notes.clear();
    tap (window, @"A");   // con Mayúsculas / Bloq Mayús
    check (is ({ { 0, true }, { 0, false } }), "en mayúscula también");

    // 3. Escribiendo en un campo de texto del plugin (su buscador de sonidos)
    [window makeFirstResponder: pluginSearch];
    notes.clear();
    tap (window, @"d");
    check (notes.empty(), "escribiendo en un campo de texto del plugin: no toca notas");

    // 4. Escribiendo el nombre de un preset de SmartKeys (TextEditor de JUCE)
    [window makeFirstResponder: juceView];
    presetName.grabKeyboardFocus();
    pump (60);
    notes.clear();
    tap (window, @"f");
    check (notes.empty(), "escribiendo en un campo de texto de SmartKeys: no toca notas");
    editor.grabKeyboardFocus();
    pump (60);

    // 5. Atajos (Cmd / Ctrl / Opción) y SmartKeys sin permitirlo
    [window makeFirstResponder: juceView];
    notes.clear();
    tap (window, @"g", NSEventModifierFlagCommand);
    tap (window, @"g", NSEventModifierFlagControl);
    tap (window, @"g", NSEventModifierFlagOption);
    check (notes.empty(), "Cmd / Ctrl / Opción + tecla: atajos, no notas");
    allowed = false;
    tap (window, @"g");
    allowed = true;
    check (notes.empty(), "con la ventana de guardar preset abierta: no toca notas");

    // 6. Foco en otra parte del DAW de la misma ventana (su Piano Roll): no son de SmartKeys
    [window makeFirstResponder: dawView];
    notes.clear();
    tap (window, @"h");
    check (notes.empty(), "foco en otra vista del DAW de la misma ventana: no toca notas");

    // 7. Otra ventana delante: no toca, pero lo que sonaba se suelta al soltar la tecla allí
    [window makeFirstResponder: juceView];
    notes.clear();
    tap (other, @"j");
    check (notes.empty(), "teclas en otra ventana: no tocan notas");
    post (window, NSEventTypeKeyDown, @"j");
    post (other, NSEventTypeKeyUp, @"j");
    check (is ({ { 11, true }, { 11, false } }), "una nota se suelta aunque la tecla se suelte con otra ventana delante");

    // 8. Octava
    octaves.clear();
    tap (window, @"z");
    tap (window, @"x");
    tap (window, @"x");
    check (octaves == std::vector<int> ({ -1, 1, 1 }), "Z / X cambian la octava (también con el foco en el plugin)");

    // 9. releaseAll: lo que suena se suelta; la tecla mantenida no vuelve a sonar sola
    notes.clear();
    post (window, NSEventTypeKeyDown, @"k");
    keyboard->releaseAll();
    post (window, NSEventTypeKeyDown, @"k", 0, YES);
    post (window, NSEventTypeKeyUp, @"k");
    check (is ({ { 12, true }, { 12, false } }), "soltar todo (otra KEY): se suelta y la tecla mantenida no vuelve a sonar sola");

    // 10. Al destruirlo, el monitor de teclas se quita (ninguna tecla llega después)
    keyboard = nullptr;
    notes.clear();
    tap (window, @"a");
    check (notes.empty(), "destruido: ya no llega ninguna tecla");

    editor.removeFromDesktop();
    [window orderOut: nil];
    [other orderOut: nil];
}
} // namespace edtest
