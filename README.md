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
- **OUTPUT MODE**: el acorde suena en el **piano interno**, sale por **MIDI OUT** hacia otro plugin (en el canal MIDI 1–16 que elijas), o ambas cosas.
- **Voz → acordes en tiempo real**: lee el micrófono por la entrada del DAW **o directamente de tu interfaz de audio** (botón `DEVICE`). La voz cruda nunca sale por el plugin.
- **DRAG CHORDS TO PIANO ROLL**: graba todos los acordes que tocas o cantas y los arrastras como `.mid` al Piano Roll, con todas sus notas.
- **43 escalas**: básicas, 21 occidentales, 7 maqam árabes y 12 ragas indias (Hindustani y Carnatic).

---

## Novedades de la versión 1.1.0

| Problema en 1.0.0 | Solución en 1.1.0 |
|---|---|
| Al reinstalar, el DAW seguía cargando el plugin viejo | Los instaladores **borran antes** cualquier versión anterior (en `/Library` y en `~/Library` de cada usuario, o en `Common Files\VST3`), la caché de Audio Units de macOS y la ficha de SmartKeys en la base de datos de FL Studio |
| El lector de voz no recibía audio en FL Studio (los generadores del Channel Rack no reciben el audio del Mixer) | Botón **`DEVICE`**: SmartKeys lee el micrófono directamente de la interfaz. Además, **SmartKeys FX** se inserta en la pista del micrófono y recibe la voz como entrada principal |
| El Piano Roll dibujaba solo 1 nota | El DAW graba la tecla que tú tocas, no la salida del plugin. **`DRAG CHORDS TO PIANO ROLL`** exporta los acordes completos (5–7 notas incluidas) y se arrastran al Piano Roll |
| No se podía elegir a dónde van los acordes | **OUTPUT MODE** `PIANO · MIDI OUT · BOTH` y selector de canal **MIDI CH 1–16**, en tiempo real |

---

## Descargar los instaladores (sin compilar nada)

GitHub compila y prueba SmartKeys en cada cambio y publica los instaladores en la página **Releases** de este repositorio (**SmartKeys 1.1.0**):

- **Mac:** `SmartKeys-1.1.0-macOS.pkg`. Funciona en Mac con chip Apple e Intel. Instala SmartKeys (VST3, AU y app) y SmartKeys FX (VST3 y AU).
- **Windows:** `SmartKeys-1.1.0-Windows-Setup.exe`. Instala SmartKeys (VST3 y app) y SmartKeys FX (VST3).

**Los dos instaladores eliminan automáticamente la versión anterior** antes de copiar la nueva. No hace falta borrar nada a mano. El registro de lo que se borró en Mac queda en `/tmp/smartkeys-install.log`.

En Mac, el `.pkg` no está firmado con un certificado de Apple. La primera vez, macOS dirá que no puede verificar al desarrollador. Para abrirlo, ve a **Ajustes del Sistema → Privacidad y seguridad → "Abrir igualmente"** y vuelve a abrir el `.pkg`. Después, en FL Studio: **Options → Manage plugins → Find installed plugins**.

---

## Instalar desde el código con un solo comando

### macOS: borra la versión vieja, compila e instala

```bash
bash Installer/install_mac.sh
```

1. Comprueba las Command Line Tools de Apple y CMake (si falta CMake, lo instala solo para tu usuario).
2. Borra SmartKeys y SmartKeys FX (`.vst3`, `.component`, `.app`) de `/Library`, de `~/Library` de cada usuario y de `/Applications`. También borra la caché de Audio Units y la ficha de FL Studio.
3. Compila el binario Universal y genera `Installer/Output/SmartKeys-1.1.0-macOS.pkg`.
4. Instala el `.pkg` y reinicia `AudioComponentRegistrar` para que FL Studio y Ableton vean el binario nuevo.

Te pide la contraseña de administrador en los pasos 2 y 4. Para desinstalar todo: `bash Installer/uninstall_mac.sh`.

### Windows

```bat
Installer\build_windows.bat
```

Compila, genera `SmartKeys-1.1.0-Windows-Setup.exe` y lo ejecuta. El instalador borra antes la versión anterior. Requisitos: Visual Studio 2022 con "Desarrollo para escritorio con C++", CMake 3.22+, Git e [Inno Setup 6.3+](https://jrsoftware.org/isdl.php). Añade `noinstall` para solo generar el `.exe`.

### Para desarrolladores: copiar al compilar

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSMARTKEYS_COPY_AFTER_BUILD=ON
cmake --build build --config Release
```

Con `SMARTKEYS_COPY_AFTER_BUILD=ON`, cada compilación hace tres cosas:

- **Antes de copiar** (PRE_BUILD) borra el `.vst3` y el `.component` anteriores de las carpetas de plugins de tu usuario.
- Copia los nuevos (`COPY_PLUGIN_AFTER_BUILD`).
- En Mac, al terminar (POST_BUILD), reinicia `AudioComponentRegistrar`.

JUCE 8.0.15 se descarga automáticamente; con `-DJUCE_DIR=/ruta/a/JUCE` se usa una copia local.

---

## Uso en el DAW

### Tocar
Carga **SmartKeys** como instrumento: en FL Studio va en el Channel Rack y en Ableton en una pista MIDI. Elige **KEY** y **SCALE** y toca. Con un clic en las notas de la barra **ESCALA** puedes activarlas o desactivarlas. El teclado de abajo ilumina las notas que suenan y el display muestra el acorde (`Dm`, `Gm7`, `F Maj9`…).

### OUTPUT MODE y canal MIDI
| Botón | Piano interno | MIDI OUT |
|---|---|---|
| `PIANO` | suena | no sale nada |
| `MIDI OUT` (External Plugin) | en silencio | sale el acorde completo |
| `BOTH` | suena | sale el acorde completo |

`MIDI CH 1–16` elige el canal MIDI de salida. Si cambias el canal con una tecla pulsada, sus NoteOff salen por el canal original, así que no quedan notas colgadas. El **puerto** MIDI lo asigna el DAW:

- **FL Studio:** en SmartKeys, abre el engranaje ⚙ (*Fruity Wrapper*) → MIDI → **Output port** = 1. En el plugin destino, **Input port** = 1.
- **Ableton Live:** crea otra pista MIDI con *MIDI From* = `SmartKeys` y pon en ella el instrumento destino.

### Pasar los acordes al Piano Roll
1. Toca o canta tu progresión: el contador del botón **`DRAG CHORDS TO PIANO ROLL (n)`** sube con cada acorde.
2. **Arrastra el botón** al Piano Roll de FL Studio (o a una pista o clip de Ableton). Se sueltan todos los acordes con todas sus notas, al tempo del proyecto.
3. `QUANTIZE 1/16` ajusta los acordes a semicorcheas. `CLEAR` empieza una progresión nueva.
4. Un **clic** (sin arrastrar) guarda el `.mid` en `Música/SmartKeys/` y lo muestra en el Finder o el Explorador.

Esto funciona igual en `PIANO`, `MIDI OUT` y `BOTH`, y también con los acordes disparados por la voz.

### Controlador vocal: tres formas de meter la voz

Activa **`MIC VOICE: ON`** y ajusta **GATE** (por defecto −48 dB). El medidor se ilumina cuando cantas por encima del umbral, y la etiqueta muestra en vivo lo que detecta: `Mic: A3 -> Am`.

**A. `DEVICE`: el micrófono directo (recomendado; sirve en cualquier DAW)**
1. Pulsa `DEVICE` y elige tu interfaz o micrófono en la lista (por ejemplo `Volt 1`), y el canal: `CH 1`, `CH 2` o `CH 1+2`.
2. SmartKeys abre esa entrada **solo para leerla**: no toca la salida de audio del DAW, y FL Studio puede seguir usando la misma interfaz.
3. En Mac, el permiso del micrófono es de la aplicación anfitriona. Si la etiqueta dice *"Sin señal: permite el micrófono a tu DAW"*, activa FL Studio (o Ableton) en **Ajustes del Sistema → Privacidad y seguridad → Micrófono** y reinicia el DAW.
4. `RESCAN` vuelve a buscar dispositivos (por ejemplo, si conectas la interfaz con el DAW abierto).

**B. SmartKeys FX en la pista del micrófono (FL Studio)**
1. En el Mixer, selecciona la pista donde entra tu micrófono (*Input* = tu interfaz) y carga **SmartKeys FX** en un slot de efectos.
2. `DAW INPUT` + `MIC VOICE: ON`. Lo que cantas en esa pista se convierte en acordes.
   - Con `PIANO` o `BOTH`, la pista suena con el piano de SmartKeys en lugar de tu voz.
   - Con `MIDI OUT`, los acordes van a otro plugin mediante el *Output port* del wrapper.

**C. Sidechain (instrumento SmartKeys)**
- **Ableton Live 11/12:** en el dispositivo SmartKeys abre el panel **Sidechain**, actívalo y elige la pista del micrófono en *Audio From*.
- **Standalone:** usa la entrada elegida en *Options → Audio/MIDI Settings*, o `DEVICE`.

---

## Controles

| Control | Función |
|---|---|
| `SMART CHORDS / SINGLE NOTES / COMPLEX CHORDS` | Acorde diatónico (3–4 notas), nota individual cuantizada, o acorde a dos manos (5–7 notas) |
| `PIANO / MIDI OUT / BOTH` | OUTPUT MODE: a dónde va el acorde (ver tabla de arriba) |
| `MIDI CH 1…16` | Canal MIDI de salida |
| `A4` + `440 / 442` | Afinación del piano interno (400–480 Hz) |
| `MICROTUNING` | Afina el piano interno con los cents reales de la escala (maqam, justa, pitagórica, shrutis) |
| `ALL NOTES OFF` | Pánico: suelta todas las notas |
| `GAIN` | Volumen del piano (−60 a +6 dB) |
| `MIC VOICE` · `DAW INPUT / DEVICE` | Activa la voz y elige de dónde llega |
| Lista de dispositivos · `CH 1 / CH 2 / CH 1+2` · `RESCAN` | Micrófono directo (vía DEVICE) |
| Medidor · `GATE` · etiqueta `Mic:` | Nivel de entrada, umbral del gate y nota/acorde detectados en vivo |
| `KEY` / `SCALE` / barra `ESCALA` | Tónica, escala (`Más escalas`: Occidentales / Árabes / Indias) y notas activas una a una |
| `TRIAD / +8VA / 7TH` | Grados k, k+2, k+4 · + octava · + séptima (en SMART CHORDS) |
| `LUSH 7/9 / NEO-SOUL / CINEMATIC` | Variación de COMPLEX CHORDS (aparece en ese modo) |
| `DRAG CHORDS TO PIANO ROLL` · `QUANTIZE 1/16` · `CLEAR` | Grabador de acordes y exportación `.mid` |
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

**MIDI:** el buffer de salida se reemplaza (`swapWith`) por el acorde completo. `ActiveNoteRegistry` guarda las 7 notas para soltarlas todas con el NoteOff, aunque cambies de modo, de escala o de canal con la tecla pulsada.

---

## Decisiones técnicas

**Orden estricto del `processBlock`.**
1. Lee el micrófono (bus de entrada del DAW o FIFO del dispositivo) y ejecuta el detector de voz.
2. Publica el nivel y la nota detectada para el medidor y la etiqueta `Mic:`.
3. `buffer.clear()`: la voz cruda nunca sale.
4. Expande cada nota de entrada (MIDI, teclado en pantalla, voz) al acorde completo en `expandedChordMidi`, en orden de muestra.
5. Renderiza el piano interno (si OUTPUT ≠ MIDI OUT).
6. Copia las notas al grabador de acordes.
7. `midiMessages.swapWith (expandedChordMidi)`. En `PIANO`, `midiMessages.clear()`.

**Tiempo real.** `processBlock` no asigna memoria ni bloquea:
- Arrays fijos y `MidiBuffer` con capacidad reservada.
- FIFOs lock-free (`AbstractFifo`) para el teclado en pantalla, el micrófono directo y el grabador.
- Atómicos para la interfaz.

Las pruebas cuentan cada `new` durante miles de bloques: **0 asignaciones**.

**Micrófono directo (DEVICE).**
- `HardwareMicInput` abre el dispositivo **solo como entrada**. En Mac, CoreAudio permite que el DAW y SmartKeys lean la misma interfaz a la vez. En Windows depende del driver: si el DAW usa ASIO en modo exclusivo, la entrada puede no estar disponible, y entonces conviene usar SmartKeys FX.
- El callback del dispositivo mezcla a mono y escribe en un FIFO lock-free, que el hilo de audio vacía en cada bloque.
- Si el FIFO acumula más de ~60 ms o 2,5 bloques, se descarta lo más antiguo, así que la latencia nunca crece.
- El detector se prepara a la frecuencia del dispositivo, y los eventos de voz se reubican en el bloque del host.
- Solo se abre un dispositivo elegido por el usuario o guardado en el proyecto: los validadores automáticos nunca abren hardware.

**Dos variantes.** El documento pedía `VST3_CATEGORIES "Instrument" "Fx" "Generator"`. JUCE ordena la categoría `Fx` en primer lugar, así que el instrumento se anunciaría como `Fx|Instrument|Generator` y FL Studio lo listaría como efecto, sin poder cargarlo en el Channel Rack. Por eso hay dos plugins:

| Plugin | Categorías VST3 | Tipo AU | Entrada |
|---|---|---|---|
| `SmartKeys` | `Instrument\|Synth\|Generator` | `aumu` | sidechain |
| `SmartKeys FX` | `Fx\|Generator` | `aumf` | principal |

Las dos aceptan entrada mono o estéreo y tienen salida estéreo (`BusesProperties().withInput ("Mic Input", stereo, true).withOutput ("Output", stereo, true)`).

**Grabador de acordes.**
- El hilo de audio publica cada NoteOn y NoteOff de salida con su posición en negras, según el tempo del host.
- El hilo de mensajes empareja las notas y genera un MIDI de 960 PPQ con el tempo del proyecto y compás 4/4.
- Opcionalmente cuantiza a 1/16, y desplaza todo para que el primer acorde empiece en el tiempo 0.

**Limpieza en los instaladores.**
- **macOS:** cada componente del `.pkg` (VST3, AU, App) lleva un `preinstall` que borra solo su tipo de archivo, así ninguno puede borrar lo que otro acaba de instalar. AU también refresca la caché antes y después. Solo se tocan `SmartKeys.*` y `SmartKeys FX.*`; el resto de plugins no se toca.
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
- *Maqam*: el cuarto de tono se cuantiza a la nota natural y lleva −50 cents para `MICROTUNING`.
- *Raga Carnatic* usa la afinación de shrutis.

El MIDI es 12-TET: los cents solo afectan al piano interno.

---

## Estructura

```
SmartKeys/
├── CMakeLists.txt                 SmartKeys (instrumento) + SmartKeys FX (efecto), limpieza al copiar
├── Source/
│   ├── ScaleChordEngine.h         Escalas, cuantización, acordes Smart y Complex, cifrado
│   ├── VoicePitchTracker.h        Gate RMS + YIN + histéresis 35 cents + estabilidad 3 frames
│   ├── HardwareMicInput.h         Micrófono directo (vía DEVICE): dispositivo solo-entrada + FIFO lock-free
│   ├── ChordRecorder.h            Grabador de acordes y exportación .mid (DRAG CHORDS TO PIANO ROLL)
│   ├── FlKeysPianoSynth.h         Piano aditivo de 32 voces
│   ├── MidiOutputRouter.h         Expansión a acordes, canal de salida, ActiveNoteRegistry
│   ├── PluginProcessor.h/.cpp     Parámetros, buses, processBlock lock-free (orden estricto)
│   └── PluginEditor.h/.cpp        Interfaz 850×640 vectorial
├── Tests/
│   ├── EngineTests.cpp            523 comprobaciones del núcleo DSP
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
- **523 comprobaciones del núcleo:** escalas, acordes, voicings complejos, router MIDI, detector de voz, grabador de acordes, FIFO del micrófono y 0 asignaciones en el audio.
- **Prueba de integración** (`HostTests.cpp`, 26 comprobaciones): carga los VST3 compilados como un DAW y comprueba:
  - Que cada tecla sale como acorde completo por MIDI OUT.
  - El canal 5 y los NoteOff por el canal original.
  - Los tres OUTPUT MODE: silencio absoluto en MIDI OUT, sin MIDI en PIANO, ambos en BOTH.
  - COMPLEX CHORDS de 7 notas.
  - Que cantar La3 por la entrada dispara La menor y lo suelta al callar.
  - Que la voz cruda nunca sale.
  - Que SmartKeys FX convierte la voz de la pista en acordes.
- **Limpieza de versiones anteriores** simulada con instalaciones falsas: borra SmartKeys y SmartKeys FX de todas las rutas y deja intactos los demás plugins (Serum, Kontakt, `SmartKeysPro.vst3`…).
- **En GitHub Actions**, en cada compilación se instala una "versión anterior" falsa en un Mac y en un PC de pruebas, se ejecuta el instalador nuevo y se comprueba que la borró. Después se validan los plugins instalados con `auval` (Apple) y `pluginval` nivel 10.
