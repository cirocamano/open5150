# Contribuir a OPEN5150

Las correcciones, mejoras de audio y contribuciones a la interfaz son bienvenidas.

## Preparar el entorno

Necesitas macOS, las herramientas de desarrollo de Apple y CMake 3.24 o posterior. Consulta el [README](README.md) para compilar y utilizar la aplicación y los plugins.

Desde la raíz del proyecto:

```sh
make
make test
cmake -S . -B build/plugins -DCMAKE_BUILD_TYPE=Release
cmake --build build/plugins --parallel 4
ctest --test-dir build/plugins --output-on-failure
```

## Proponer un cambio

1. Abre una rama para tu cambio.
2. Mantén el estilo del código y la compatibilidad con C++17.
3. Añade o ajusta una prueba si cambias el comportamiento del procesamiento, la carga de IR o el guardado de estado.
4. Ejecuta las pruebas y actualiza la documentación si cambia el uso del programa.
5. Abre una pull request explicando el problema, la solución y cómo la comprobaste.

El motor compartido está en `dsp/`, la integración AU/VST3 en `plugin/` y los recursos de la aplicación en `ui/`. Conserva las operaciones de disco y las asignaciones de memoria fuera del callback de audio.

## Informar de un problema

Incluye la versión de macOS, la arquitectura del equipo, el formato utilizado (app, AU o VST3), el DAW si corresponde y los pasos para reproducirlo. Para problemas de audio, indica también la frecuencia de muestreo y el tamaño de buffer.

## Licencia

Las contribuciones a OPEN5150 se realizan bajo la [licencia MIT](LICENSE). Las dependencias conservan sus propias licencias.
