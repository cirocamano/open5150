# OPEN5150

Simulador de amplificador de guitarra inspirado en el 5150, con sonidos limpios, crunch y alta ganancia. Disponible para macOS como aplicación independiente y como plugin **AU** y **VST3**.

## Características

- Tres modos: **Red Lead**, **Green Crunch** y **Green Clean**.
- Controles de Pre Gain, Post Gain, Bass, Mid, Treble, Presence, Resonance y Bright.
- Simulación de previo y etapa de potencia.
- Noise gate con umbral ajustable y limitador de salida.
- Cargador de respuestas a impulsos de cabina en formato WAV, mono y estéreo.
- Controles de EQ de micrófono, posición, distancia, ángulo y filtros Low Cut / High Cut.
- Aplicación con interfaz gráfica, selección de dispositivos y grabación WAV.
- Plugins con parámetros automatizables y guardado de ajustes en la sesión del DAW.
- Interfaz de consola con controles de teclado.

## Formatos disponibles

| Formato | Uso |
| --- | --- |
| Aplicación `.app` | Tocar y grabar sin abrir un DAW |
| Audio Unit (AU) | Procesar guitarra en un host compatible con AU |
| VST3 | Procesar guitarra en un host compatible con VST3 |
| Consola | Usar el amplificador desde la terminal |

Los plugins admiten entrada y salida mono, estéreo y entrada mono con salida estéreo. El DAW gestiona los dispositivos de audio, el buffer y la grabación.

## Requisitos

- macOS.
- Herramientas de desarrollo de Apple, con compilador C++17 y `make`.
- CMake **3.24 o posterior** para compilar los plugins.
- Una interfaz de audio con entrada de instrumento para conectar una guitarra.

Para instalar las herramientas de desarrollo de Apple:

```sh
xcode-select --install
```

Los comandos siguientes se ejecutan desde la raíz del proyecto. La compilación utiliza la arquitectura del equipo que la ejecuta.

## Compilar la aplicación

```sh
make
open build/Open5150.app
```

La aplicación incluye todos los recursos de su interfaz y puede copiarse a **Aplicaciones**. Al abrirla, selecciona los dispositivos de entrada y salida, ajusta el buffer y carga una IR de cabina. macOS puede solicitar permiso para acceder a la entrada de audio.

Se admiten buffers de **32, 64, 128 y 256 muestras**. Un buffer menor reduce la latencia de procesamiento y requiere más capacidad de CPU. La latencia total también depende de la interfaz de audio y sus controladores.

Las grabaciones se guardan en `~/Music/OPEN5150/Recordings` como WAV estéreo de 32 bits flotantes.

## Compilar los plugins

```sh
cmake -S . -B build/plugins -DCMAKE_BUILD_TYPE=Release
cmake --build build/plugins --parallel 4
```

CMake descarga automáticamente [JUCE 8.0.12](https://github.com/juce-framework/JUCE/tree/8.0.12) durante la primera configuración.

| Formato | Archivo generado |
| --- | --- |
| AU | `build/plugins/Open5150_artefacts/Release/AU/OPEN5150.component` |
| VST3 | `build/plugins/Open5150_artefacts/Release/VST3/OPEN5150.vst3` |

### Instalación

Para instalar los plugins para tu usuario:

```sh
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components" "$HOME/Library/Audio/Plug-Ins/VST3"
cp -R build/plugins/Open5150_artefacts/Release/AU/OPEN5150.component "$HOME/Library/Audio/Plug-Ins/Components/"
cp -R build/plugins/Open5150_artefacts/Release/VST3/OPEN5150.vst3 "$HOME/Library/Audio/Plug-Ins/VST3/"
```

Reabre el DAW o actualiza su lista de plugins y busca **OPEN5150**. La interfaz del plugin ofrece controles de parámetros y un botón para cargar la IR WAV.

## Cabinas e IR

Usa **Cargar IR WAV…** para seleccionar una respuesta a impulsos de cabina. La cabina permanece en bypass hasta cargar una IR válida; en el plugin, el control **Cabina IR** permite activar o desactivar su procesamiento.

- WAV mono o estéreo, PCM de 16/24 bits o float de 32 bits.
- Frecuencias de muestreo entre **22,05 y 192 kHz**.
- Duración máxima de **dos segundos**.
- Remuestreo automático a la frecuencia del motor, conservando el nivel y la cola de la respuesta.
- Conservación de la IR anterior si un archivo no puede cargarse.

Las IR mono se aplican a ambos canales; las estéreo conservan sus respuestas izquierda y derecha. Los controles de micrófono y posición modifican el sonido mediante EQ. Para tocar con buffers pequeños, las IR cortas reducen el consumo de CPU.

Los plugins guardan la **ruta del WAV** junto con los parámetros de la sesión. Conserva la IR en una ubicación estable e inclúyela al trasladar un proyecto a otro equipo. El archivo WAV no se incrusta en la sesión.

## Uso desde la consola

```sh
# Elegir dispositivos y buffer de forma interactiva
./build/open5150_live

# Cargar una IR de cabina
./build/open5150_live --ir "cabina.wav"

# Consultar los dispositivos disponibles
./build/open5150_live --list-devices

# Mostrar las opciones
./build/open5150_live --help
```

También puedes indicar los IDs de entrada, salida y tamaño de buffer:

```sh
./build/open5150_live <entrada> <salida> <buffer> --ir "cabina.wav"
```

### Controles de teclado

| Teclas | Control |
| --- | --- |
| `c` / `C` | Cambiar Red / Green |
| `p` / `P` | Bajar / subir Pre Gain |
| `v` / `V` | Bajar / subir Post Gain |
| `b` / `B`, `m` / `M`, `t` / `T` | Bass, Mid, Treble |
| `r` / `R`, `e` / `E` | Resonance, Presence |
| `g` / `G` | Umbral del noise gate |
| `i` / `I` | EQ de micrófono |
| `x` / `X` | Posición de micrófono |
| `o` / `O` | Activar / desactivar cabina |
| Espacio | Iniciar / detener grabación |
| `q` / `Q` | Salir |

## Desarrollo

El motor está escrito en C++17 y utiliza los frameworks de audio de macOS. La aplicación y los plugins comparten el procesamiento del amplificador, el gate, la cabina y el limitador.

Para ejecutar las pruebas:

```sh
make test
ctest --test-dir build/plugins --output-on-failure
```

Las pruebas cubren el procesamiento DSP, la carga y el remuestreo de IR, los parámetros, la grabadora, los formatos de canal y el guardado de estado. También comprueban la carga de los bundles AU y VST3.

Para eliminar los archivos de compilación:

```sh
make clean
```

## Contribuir

Consulta [CONTRIBUTING.md](CONTRIBUTING.md) para preparar el entorno, ejecutar las pruebas y proponer cambios.

## Licencia

El código de OPEN5150 se distribuye bajo la [licencia MIT](LICENSE).

Los plugins utilizan JUCE, que se distribuye por separado bajo sus propias [condiciones de licencia](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md).
