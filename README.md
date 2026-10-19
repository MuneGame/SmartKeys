# SmartKeys — Piano inteligente, generador de acordes y controlador vocal (VST3 / AU / Standalone)

![SmartKeys](docs/screenshot.png)

SmartKeys (JUCE 8, C++17) se instala en dos versiones que salen del mismo código:

| Plugin | Tipo | Dónde se carga | Para qué |
|---|---|---|---|
| **SmartKeys** | Instrumento (VST3 · AU · app Standalone) | Channel Rack de FL Studio, pista MIDI de Ableton | Tocar acordes con el teclado, el Piano Roll o la voz |
| **SmartKeys FX** | Efecto (VST3 · AU) | La pista del **micrófono** en el Mixer de FL Studio (o una pista de audio) | Convertir la voz de esa pista en acordes sin ningún ruteo |

Las dos hacen lo mismo:

- **Cuantizan** cualquier nota (teclado MIDI, teclado de la PC, Piano Roll o tu voz) a la escala elegida: es imposible tocar una nota fuera de escala.
- **SMART CHORDS**: cada tecla dispara el acorde diatónico completo de ese grado (tríada, tríada + octava o cuatriada con 7ª). En Fa Mayor: Do → C Maj, Re → Dm, Mi → E dim.
- **COMPLEX CHORDS**: cada tecla dispara un acorde de 5 a 7 notas a dos manos (R&B / Neo-Soul), con tres variaciones y velocidades balanceadas por voz.
- **Piano + MIDI siempre a la vez**: el piano interno suena (su volumen se ajusta con `GAIN`) y cada tecla sale además como el **acorde completo** por la salida MIDI (canal 1–16) hacia cualquier otro plugin.
- **STRUM SPEED**: las notas del acorde entran escalonadas, de grave a aguda (`ASC ▲`) o al revés (`DESC ▼`), con una perilla de 0 a 200 ms.
- **Voz → acordes en tiempo real**: SmartKeys FX en la pista del micrófono (o la entrada sidechain del instrumento) convierte tu voz en acordes. La voz cruda nunca sale por el plugin.
- **43 escalas**: básicas, 21 occidentales, 7 maqam árabes y 12 ragas indias (Hindustani y Carnatic).

---

## Novedades de la versión 1.4.0

**Se corrigió el cierre de FL Studio.** FL se cerraba al **cerrar la ventana de SmartKeys** (`EAccessViolation` en `~SmartKeysAudioProcessorEditor` → `Slider::removeListener`). La perilla STRUM SPEED se destruía antes que su enlace con el parámetro, y el enlace la tocaba ya destruida. Escribir en memoria liberada así es lo que en la 1.2 dañaba el montón de FL (`nanov2_guard_corruption_detected`). El fallo existía desde la 1.2, cuando llegó el STRUM. Más detalles en [Por qué se cerraba FL Studio](#por-qué-se-cerraba-fl-studio-12--13).

**Versión más ligera y simple.** Se quitaron siete cosas, de la interfaz y del motor:

| Quitado | Ahora |
|---|---|
| `OUTPUT MODE` (`PIANO + MIDI` / `MIDI OUT`) | Siempre suena el piano y siempre sale el MIDI. Para bajar el piano, `GAIN` |
| `A4` · `440` · `442` | Afinación fija en A4 = 440 Hz |
| `MICROTUNING` | El piano suena en 12-TET, como el MIDI |
| `ALL NOTES OFF` | Sigue funcionando el *All Notes Off* (CC 120/123) que manda el DAW al detenerse |
| `MIDI PORT` (puerto virtual *Chord Controller*) | Para tocar otro plugin, la salida MIDI del wrapper (*Output port*) |
| Pestaña `PROGRESIÓN`, captura y paleta | Sólo la vista `KEY · SCALE`, sin pestañas |
| `DRAG CHORDS TO PIANO ROLL` y los `.mid` | El STRUM ocupa ese espacio |

Los proyectos guardados con versiones anteriores se abren sin problema: lo que ya no existe se descarta al cargarlos.

La 1.3.0 quitó el micrófono `DEVICE`, dejó el `processBlock` sin asignaciones de memoria y trajo el STRUM de 0 a 200 ms. La 1.2.0, la salida MIDI siempre activa y la ventana redimensionable. La 1.1.0, la limpieza automática de versiones anteriores al instalar, SmartKeys FX y el selector de canal MIDI.

---

## Descargar los instaladores (sin compilar nada)

GitHub compila y prueba SmartKeys en cada cambio y publica los instaladores en la página **Releases** de este repositorio (**SmartKeys 1.4.0**):

- **Mac:** `SmartKeys-1.4.0-macOS.pkg`. Funciona en Mac con chip Apple e Intel. Instala SmartKeys (VST3, AU y app) y SmartKeys FX (VST3 y AU).
- **Windows:** `SmartKeys-1.4.0-Windows-Setup.exe`. Instala SmartKeys (VST3 y app) y SmartKeys FX (VST3).

**Los dos instaladores eliminan automáticamente la versión anterior** antes de copiar la nueva. No hace falta borrar nada a mano. El registro de lo que se borró en Mac queda en `/tmp/smartkeys-install.log`.

En Mac, el `.pkg` no está firmado con un certificado de Apple. La primera vez, macOS dirá que no puede verificar al desarrollador. Para abrirlo, ve a **Ajustes del Sistema → Privacidad y seguridad → "Abrir igualmente"** y vuelve a abrir el `.pkg`. Después, en FL Studio: **Options → Manage plugins → Find installed plugins**.

---

## Instalar desde el código con un solo comando

### macOS: borra la versión vieja, compila e instala

```bash
bash Installer/install_mac.sh
```

1. Comprueba las Command Line Tools de Apple y CMake (si falta CMake, lo instala con Homebrew o solo para tu usuario).
2. Borra SmartKeys y SmartKeys FX (`.vst3`, `.component`, `.app`) de `/Library`, de `~/Library` de cada usuario y de `/Applications`. También borra la caché de Audio Units y la ficha de FL Studio.
3. Compila en `Release` el binario Universal y genera `Installer/Output/SmartKeys-1.4.0-macOS.pkg`.
4. Instala el `.pkg` y reinicia `AudioComponentRegistrar` para que FL Studio y Ableton vean el binario nuevo.

Te pide la contraseña de administrador en los pasos 2 y 4. Para desinstalar todo: `bash Installer/uninstall_mac.sh`.

### Windows

```bat
Installer\build_windows.bat
```

Compila, genera `SmartKeys-1.4.0-Windows-Setup.exe` y lo ejecuta. El instalador borra antes la versión anterior. Requisitos: Visual Studio 2022 con "Desarrollo para escritorio con C++", CMake 3.22+, Git e [Inno Setup 6.3+](https://jrsoftware.org/isdl.php). Añade `noinstall` para solo generar el `.exe`.

### Para desarrolladores: copiar al compilar

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSMARTKEYS_COPY_AFTER_BUILD=ON
cmake --build build --config Release
```

Con `SMARTKEYS_COPY_AFTER_BUILD=ON`, cada compilación hace tres cosas:

- **Antes de copiar** (PRE_BUILD) borra el `.vst3` y el `.component` anteriores de las carpetas de plugins de tu usuario.
- Copia los nuevos (`COPY_PLUGIN_AFTER_BUILD`).
- En Mac, al terminar (POST_BUILD), reinicia `AudioComponentRegistrar`.

Está desactivado por defecto para no dejar una segunda copia en `~/Library` junto a la que instala el `.pkg` en `/Library` (dos copias son justo lo que hace que el DAW cargue una versión vieja). JUCE 8.0.15 se descarga automáticamente; con `-DJUCE_DIR=/ruta/a/JUCE` se usa una copia local.

---

## Uso en el DAW

### Tocar
Carga **SmartKeys** como instrumento: en FL Studio va en el Channel Rack y en Ableton en una pista MIDI. Elige **KEY** y **SCALE** y toca. Con un clic en las notas de la barra **ESCALA** puedes activarlas o desactivarlas. El teclado de abajo ilumina las notas que suenan y el display muestra el acorde (`Dm`, `Gm7`, `F Maj9`…).

### Piano interno + salida MIDI
SmartKeys **siempre** hace las dos cosas a la vez:

- Suena su piano interno. `GAIN` ajusta su volumen (−60 a +6 dB).
- Cada tecla sale por la salida MIDI del plugin como el **acorde completo** (3, 4 o 5–7 notas), con su STRUM, para tocar cualquier otro plugin.

`MIDI CH 1–16` elige el canal MIDI de salida. Si cambias el canal con una tecla pulsada, sus NoteOff salen por el canal original, así que no quedan notas colgadas. El **puerto** MIDI lo asigna el DAW:

- **FL Studio:** en SmartKeys, abre el engranaje ⚙ (*Fruity Wrapper*) → MIDI → **Output port** = 1. En el plugin destino (Serum, Kontakt…), **Input port** = 1. Si sólo quieres oír el otro plugin, baja el `GAIN` de SmartKeys al mínimo.
- **Ableton Live:** crea otra pista MIDI con *MIDI From* = `SmartKeys` y pon en ella el instrumento destino.

### STRUM SPEED (entrada progresiva de las notas)
La perilla **STRUM SPEED** (debajo del display) va de `OFF` a 200 ms:

- En `OFF` todas las notas del acorde entran a la vez.
- Al subirla, la nota *i* entra con un retardo `i · base · (1 + 0.15·i)`. Cada nota tarda un poco más que la anterior, como un pianista que desgrana el acorde.
- `ASC ▲` entra de la más grave a la más aguda (F4 → A4 → C5); `DESC ▼`, al revés (C5 → A4 → F4).
- Si sueltas la tecla antes de que entren todas, las que faltaban se cancelan y las que sonaban se apagan juntas: nunca quedan notas colgadas.
- Funciona en el piano interno, en la salida MIDI y con la voz, y el `LIVE KEYBOARD` enciende las teclas una a una. Doble clic en la perilla: `OFF`.

### Controlador vocal: la voz entra por el plugin

Activa **`MIC VOICE: ON`** y ajusta **GATE** (por defecto −48 dB). El medidor se ilumina cuando cantas por encima del umbral, y la etiqueta muestra en vivo lo que detecta: `Mic: A3 -> Am`.

SmartKeys escucha la voz **sólo por su propio bus de entrada de audio**: nunca abre dispositivos de audio por su cuenta dentro del DAW.

**A. SmartKeys FX en la pista del micrófono (FL Studio, recomendado)**
1. En el Mixer, selecciona la pista donde entra tu micrófono (*Input* = tu interfaz) y carga **SmartKeys FX** en un slot de efectos.
2. Activa `MIC VOICE: ON`. Lo que cantas en esa pista se convierte en acordes: la pista suena con el piano de SmartKeys en lugar de tu voz, y los acordes salen por su salida MIDI.
3. En Mac, el permiso del micrófono es de FL Studio: **Ajustes del Sistema → Privacidad y seguridad → Micrófono**.

**B. Sidechain (instrumento SmartKeys)**
- **Ableton Live 11/12:** en el dispositivo SmartKeys abre el panel **Sidechain**, actívalo y elige la pista del micrófono en *Audio From*.
- **Standalone:** usa la entrada elegida en *Options → Audio/MIDI Settings*.

---

## Controles

| Control | Función |
|---|---|
| `SMART CHORDS / SINGLE NOTES / COMPLEX CHORDS` | Acorde diatónico (3–4 notas), nota individual cuantizada, o acorde a dos manos (5–7 notas) |
| `MIDI CH 1…16` | Canal MIDI de salida de los acordes completos |
| `GAIN` | Volumen del piano interno (−60 a +6 dB); la salida MIDI no cambia |
| `MIC VOICE` | Activa la voz (llega por la entrada del plugin: SmartKeys FX o sidechain) |
| Medidor · `GATE` · etiqueta `Mic:` | Nivel de entrada, umbral del gate y nota/acorde detectados en vivo |
| `KEY` · `SCALE` · barra `ESCALA` | Tónica, escala (`Más escalas`: Occidentales / Árabes / Indias) y notas activas una a una |
| `TRIAD / +8VA / 7TH` | Grados k, k+2, k+4 · + octava · + séptima (en SMART CHORDS) |
| `LUSH 7/9 / NEO-SOUL / CINEMATIC` | Variación de COMPLEX CHORDS (aparece en ese modo) |
| `STRUM SPEED` · `ASC ▲ / DESC ▼` | Entrada progresiva de las notas del acorde (0–200 ms) y su dirección |
| Teclado PC | `A W S E D F T G Y H U J K` = una octava, `Z / X` = bajar/subir octava |

---

## Modo COMPLEX CHORDS

![COMPLEX CHORDS — Dm11 Neo-Soul](docs/complex_neosoul.png)

La tecla (MIDI, teclado de la PC, Piano Roll o voz) se cuantiza a la escala (`d0`, índice `k`) y genera:

| Voz | 7th & 9th Lush | 9th & 11th Neo-Soul | Cinematic Spread Add9 | Velocidad |
|---|---|---|---|---|
| Mano izquierda | `d0 − 12`, `(k+4) − 12` | `d0 − 12`, `(k+4) − 12` | `d0 − 12`, `(k+4) − 12` | 95 % |
| Mano derecha | `k`, `k+2`, `k+6`, `k+8` | `k`, `k+2`, `k+6`, `k+8` | `k`, `k+8` (add9), `k+9` (10ª) | 80 % |
| Voz superior | `k+11` (5ª arriba) | `k+10` (11ª) | `k+11` (12ª), `k+14` (raíz +2 octavas) | 90 % (la más aguda) |

Progresión de **Fa Mayor** (I–vii, tecla en la 4ª octava, 7 notas por acorde):

| Variación | I | ii | iii | IV | V | vi | vii |
|---|---|---|---|---|---|---|---|
| Lush | F Maj9 | Gm9 | Am7(11) | Bb Maj9 | C9 | Dm9 | Em7b5 |
| Neo-Soul | F Maj13 | Gm11 | Am7(11) | Bb Maj9#11 | C13 | Dm11 | Em7b5(11) |
| Cinematic | Fadd9 | Gm(add9) | Am(add11) | Bbadd9 | Cadd9 | Dm(add9) | E dim |

**Regla anti-choques (avoid notes).** Si una extensión cae a 1 semitono (2ª menor) o a 13 semitonos (9ª menor) de otra voz, se sustituye por el siguiente candidato **diatónico**:
- 11ª natural sobre 3ª mayor → 13ª. Por eso sale F Maj13 y no "F Maj11".
- b9 → 11ª u octava.
- En Cinematic sobre acordes menores, la 9ª se sube una octava.

Las notas siempre pertenecen a la escala activa (verificado en 198 144 voicings).

**Velocity voicing:**
- Mano izquierda: 95 %.
- Voces centrales: 80 %.
- Voz superior: 90 %.

**Polifonía:** 32 voces, que dan para 4 acordes de 7 notas sostenidos.

**MIDI:** el buffer de salida se reemplaza (`swapWith`) por el acorde completo. `ActiveNoteRegistry` guarda las notas de cada tecla para soltarlas todas con el NoteOff, aunque cambies de modo, de escala o de canal con la tecla pulsada.

---

## Decisiones técnicas

### Por qué se cerraba FL Studio (1.2 – 1.3)

C++ destruye los miembros de una clase en el orden inverso al que se declaran. En el editor de la 1.2 y la 1.3, el enlace de la perilla STRUM SPEED con su parámetro estaba declarado **antes** que la perilla. Al cerrar la ventana:

1. Se destruía primero la perilla (`juce::Slider`).
2. Después se destruía el enlace, que llamaba a `strumKnob.removeListener (this)` sobre la perilla ya destruida.

Ese acceso a memoria liberada pasaba cada vez que se cerraba la ventana de SmartKeys. En FL Studio para Mac a veces escribía sobre memoria ya reutilizada. Eso dañaba el montón, y macOS lo detectaba más tarde en otra parte: el cierre `nanov2_guard_corruption_detected` de la 1.2. Otras veces fallaba en el acto: el `EAccessViolation` de la 1.3, en `~SmartKeysAudioProcessorEditor → Slider::removeListener`. Las direcciones del informe de FL se tradujeron con el binario 1.3.0 exacto (mismo UUID) y llevan a esas funciones.

**Cómo se encontró y se comprobó.** `pluginval` abre y cierra el editor muchas veces. Ejecutado sobre el VST3 compilado con AddressSanitizer, falla con la 1.3 en la primera prueba de editor, con la misma pila. pluginval sin sanitizer no lo veía, porque leer memoria liberada no siempre falla.

**Corrección (tres capas):**
- En el editor, los enlaces se declaran **después** de todos los controles, así que se destruyen antes que ellos.
- El destructor del editor para el temporizador y destruye los enlaces explícitamente, antes que nada más.
- Cada enlace guarda su control con un `Component::SafePointer`. Si el control ya no existe, el enlace no lo toca.

Además, `setSize()` es la última línea del constructor del editor, y el destructor suelta cualquier nota que el ratón o el teclado de la PC dejaran pulsada.

### Orden estricto del `processBlock`
1. Si el host pide un bloque de 0 muestras (FL Studio lo hace para entregar parámetros), sale sin tocar nada: no existe ninguna posición válida para un evento MIDI.
2. Lee el micrófono (sólo el bus de entrada del plugin) y ejecuta el detector de voz.
3. Publica el nivel y la nota detectada para el medidor y la etiqueta `Mic:`.
4. `buffer.clear()`: la voz cruda nunca sale.
5. Expande cada nota de entrada (MIDI, Piano Roll, teclado en pantalla y voz) al acorde completo, en orden de muestra. El MIDI se lee como bytes crudos: nunca se construye un `juce::MidiMessage`, que con un SysEx pediría memoria.
6. STRUM: reparte las notas de cada acorde en el tiempo con una cola por muestra. Cada evento queda en `0 ≤ posición < numSamples`.
7. Renderiza el piano interno y aplica `GAIN`.
8. `midiMessages.swapWith (...)`: la salida MIDI son siempre los acordes completos, con su STRUM.

El plugin no lee el transporte del DAW (`getPlayHead()`): ya no lo necesita.

### Tiempo real
`processBlock` no asigna memoria, no bloquea y no llama al hilo de mensajes:
- Arrays de tamaño fijo: las notas de cada tecla (`ChordSlot`), la cola del STRUM y el FIFO lock-free (`AbstractFifo`) del teclado en pantalla. Ningún `std::vector` en el camino de audio.
- `MidiBuffer` con capacidad reservada.
- Atómicos para la interfaz: el acorde, las notas encendidas, el nivel del micrófono. La ventana los lee con un temporizador; nada en el audio toca componentes ni el `MessageManager`.
- Los controles leen los parámetros por sondeo (30 veces por segundo) en lugar de escucharlos. Con los `ParameterAttachment` de JUCE, la automatización del host avisaba a la interfaz desde el hilo de audio.

Las pruebas lo comprueban de dos formas:
- Cuentan cada `new` durante miles de bloques de cada pieza (detector, router, STRUM, motor y sintetizador con robo de voces).
- Reemplazan `malloc` en un host de prueba y llaman al `processBlock` del VST3 compilado 3000 veces: bloques de 32 a 512 muestras, notas, STRUM, voz, *All Notes Off* y cambios de modo y escala.

En ambos casos el resultado es **0 asignaciones**.

**Dos variantes.** El documento pedía `VST3_CATEGORIES "Instrument" "Fx" "Generator"`. JUCE ordena la categoría `Fx` en primer lugar, así que el instrumento se anunciaría como `Fx|Instrument|Generator` y FL Studio lo listaría como efecto, sin poder cargarlo en el Channel Rack. Por eso hay dos plugins:

| Plugin | Categorías VST3 | Tipo AU | Entrada |
|---|---|---|---|
| `SmartKeys` | `Instrument\|Synth\|Generator` | `aumu` | sidechain |
| `SmartKeys FX` | `Fx\|Generator` | `aumf` | principal |

**Limpieza en los instaladores.**
- **macOS:** cada componente del `.pkg` (VST3, AU, App) lleva un `preinstall` que borra solo su tipo de archivo, así ninguno puede borrar lo que otro acaba de instalar. AU también refresca la caché antes y después (`killall -9 AudioComponentRegistrar`). Solo se tocan `SmartKeys.*` y `SmartKeys FX.*`; el resto de plugins no se toca.
- **Windows:** `[InstallDelete]` hace lo mismo.

**Detector de voz.**
- Señal decimada a ~24 kHz, YIN sobre 1024 muestras con salto de ~5 ms.
- Histéresis de 35 cents y 3 frames de estabilidad.
- Precisión < 2,1 cents de 65 Hz a 1046 Hz.
- Latencia de disparo: ~30–45 ms.

**Interpretación de escalas ambiguas.**
- *Blues* = blues mayor.
- *Minor Pentatonic + Blues* = pentatónica menor + b5.
- *Melodic Minor (Asc+Dec)* = unión de ambas formas.
- *Bebop* = bebop dominante.
- *Maqam*: el cuarto de tono se cuantiza a la nota natural.

El piano y el MIDI suenan en 12-TET con A4 = 440 Hz.

---

## Estructura

```
SmartKeys/
├── CMakeLists.txt                 SmartKeys (instrumento) + SmartKeys FX (efecto), limpieza al copiar
├── Source/
│   ├── ScaleChordEngine.h         Escalas, cuantización, acordes Smart y Complex, cifrado
│   ├── VoicePitchTracker.h        Gate RMS + YIN + histéresis 35 cents + estabilidad 3 frames
│   ├── StrumScheduler.h           STRUM: cola de NoteOn por muestra, ASC/DESC, cancelación al soltar
│   ├── FlKeysPianoSynth.h         Piano aditivo de 32 voces (robo de voces sin memoria dinámica)
│   ├── MidiOutputRouter.h         Expansión a acordes y ActiveNoteRegistry con ranuras de tamaño fijo
│   ├── PluginProcessor.h/.cpp     Parámetros, buses, processBlock lock-free
│   ├── PluginEditor.h/.cpp        Interfaz 850×606 vectorial y redimensionable
│   └── UiCommon.h                 Colores, tipografía, LookAndFeel y enlaces por sondeo
├── Tests/
│   ├── EngineTests.cpp            Comprobaciones del núcleo DSP
│   └── HostTests.cpp              Carga los VST3 compilados como un DAW y comprueba su salida
└── Installer/
    ├── install_mac.sh             Borra la versión vieja + compila + instala (macOS)
    ├── uninstall_mac.sh           Desinstalación completa (macOS)
    ├── build_mac_installer.sh     Compila Universal + pkgbuild/productbuild → .pkg
    ├── macos/                     Scripts preinstall/postinstall de limpieza del .pkg
    ├── Installer_Windows.iss      Inno Setup → .exe ([InstallDelete] de la versión anterior)
    └── build_windows.bat          Compila + genera el .exe + instala
```

---

## Verificación realizada

- **pluginval nivel 10** (el máximo) sobre los dos VST3, con pruebas de editor: **SUCCESS**. Incluye el validador oficial de Steinberg, estado, automatización, seguridad de hilos, fuzzing y buses.
- **pluginval nivel 10 sobre el VST3 de SmartKeys compilado con AddressSanitizer + UBSan.** Es la prueba que reproducía el cierre de FL Studio: abre y cierra el editor una y otra vez. Con la 1.3 fallaba en la primera prueba de editor; con la 1.4 da **SUCCESS** y 0 errores de memoria. SmartKeys FX usa el mismo editor.
- **517 comprobaciones del núcleo:**
  - Escalas, acordes, voicings complejos y router MIDI.
  - Detector de voz.
  - STRUM exacto a la muestra (ASC/DESC, NoteOff anticipado, *All Notes Off*).
  - Robo de voces del piano sin memoria dinámica, y 0 asignaciones en todo el camino de audio.
- **Prueba de integración** (`HostTests.cpp`, 39 comprobaciones). Carga los VST3 compilados como un DAW y comprueba:
  - Que cada tecla sale como acorde completo por MIDI, en el canal elegido, mientras suena el piano interno.
  - Que `GAIN` baja el piano sin tocar el MIDI.
  - Que ya no existen `OUTPUT MODE`, `A4` ni `MICROTUNING`, y que un proyecto de la 1.3 se abre sin restos de ellos ni de la progresión o el puerto.
  - El STRUM a 100 ms: +5520 y +12480 muestras a 48 kHz, en ASC y en DESC.
  - La voz → acordes y SmartKeys FX.
  - **4000 bloques de 0 a 512 muestras** (como los manda FL Studio): ningún evento MIDI fuera de su bloque y nada de MIDI en los bloques de 0 muestras.
  - **0 asignaciones** en 3000 llamadas al `processBlock` real, medidas reemplazando `malloc`.
  - **Concurrencia:** un hilo de audio llama sin parar al `processBlock` mientras el hilo de mensajes guarda y carga el estado y cambia parámetros, como hace FL Studio.
- **AddressSanitizer + UBSan** sobre la app y el VST3 del instrumento:
  - 1460 acciones al azar en la interfaz durante 5 minutos, con el audio corriendo: modos, escalas, notas de la barra ESCALA, teclado en pantalla y de la PC, perilla STRUM, ASC/DESC, canal MIDI, GAIN, GATE, MIC VOICE y redimensionar.
  - La prueba de integración del instrumento, con 20 s de concurrencia.
  - Resultado: **0 errores de memoria**.
- **Limpieza de versiones anteriores** simulada con instalaciones falsas: borra SmartKeys y SmartKeys FX de todas las rutas y deja intactos los demás plugins.
- **En GitHub Actions**, en cada compilación se instala una "versión anterior" falsa en un Mac y en un PC de pruebas, se ejecuta el instalador nuevo y se comprueba que la borró. Después se validan los plugins instalados con `auval` (Apple) y `pluginval` nivel 10.
