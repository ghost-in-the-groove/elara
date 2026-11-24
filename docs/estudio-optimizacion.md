# Estudio de optimización de Elara: rapidez, concurrencia y memoria

Fecha: 2026-11-24. Rama `main` (4c32790) con los cambios locales sin confirmar.

## 1. Conclusión

Los algoritmos de los caminos calientes ya están bien trabajados y medidos
(`docs/performance.md`). Lo que queda por ganar no está en afinar bucles, sino en
**dónde y cuándo se ejecuta el trabajo**:

1. **El hilo de la UI hace trabajo pesado en cada edición.** Cada tecla copia la
   canción entera y la vuelve a secuenciar de forma síncrona, y además recarga el
   reproductor. Con la canción de referencia son unos 39 ms de bloqueo por
   pulsación, antes de empezar el layout. Guardar también es síncrono.
2. **No hay pool de hilos ni cancelación.** `core::forEachIndex` crea y destruye
   `std::thread` en cada llamada (13 puntos de llamada en `src`), anidado dentro
   del pool global de Qt que también pinta las páginas. Un layout o un pintado
   obsoleto se ejecuta entero y solo se descarta su resultado.
3. **Todo se recalcula entero.** Cada edición vuelve a secuenciar toda la canción
   y a maquetar todas las páginas; no hay caché por compás ni noción de «qué ha
   cambiado» en los comandos.
4. **Hay copias y picos de memoria evitables**: texturas de página completas que
   crecen con zoom² × dpr², buffers duplicados al parsear XML/JSON, la unión final
   del exportador MusicXML y varios `struct` con campos de 64 bits para valores
   de 8-16 bits.
5. **El binario «optimizado» es `RelWithDebInfo` sin LTO.** No existe un preset
   `Release` real.

En canciones pequeñas nada de esto se nota: el impacto «alto» vale para ficheros
grandes (decenas de pistas, cientos de compases).

## 2. Método y límites

- Estudio **estático, de solo lectura**: no se ha compilado ni ejecutado ningún
  benchmark. Las cifras en ms proceden de `docs/performance.md` (canción de
  referencia: 20 pistas × 500 compases, 160k notas) o son estimaciones derivadas
  de ellas; ninguna es una medida nueva.
- Ocho subsistemas analizados por separado (app-documento, pintado/QML, notación,
  core, audio, io-xml, io-gp, io-midi-nativo). Cada hallazgo lo revisó después un
  segundo revisor con el encargo de refutarlo leyendo el código.
- Resultado: **70 hallazgos**: 30 confirmados, 40 parciales
  (el código es como se describe, pero el revisor rebajó el impacto o matizó la
  propuesta) y 0 refutados.
- La configuración de compilación (apartado 3.3) la revisé yo directamente y no
  pasó por el segundo revisor.
- Antes de aplicar cualquier cambio: medir antes y después con
  `python scripts/elara.py bench`, como exige `docs/performance.md`.

## 3. Propuestas prioritarias

### 3.1 Sacar el trabajo pesado del hilo de la UI (impacto alto)

| Qué | Dónde | Coste hoy (canción grande) | Propuesta |
|---|---|---|---|
| Copia de la canción + secuenciado por edición | `src/app/src/score_document.cpp:877-880` (`edited()`) | 10,7 ms + 28 ms por tecla | Una única tarea de worker que construya `Sequence` y layout juntos, como ya hace `openAndLayOut`; `apply()` publica ambos |
| Recarga del reproductor por edición | `src/app/src/audio_controller.cpp:87` (`Player::load`) | Reparte 320k eventos y crea un `Synth` por pista, aunque no suene nada | Cargar de forma perezosa al pulsar play, o en el mismo worker |
| Guardar `.elara` | `src/app/src/score_document.cpp:843` (`save()`) | ~70 ms (JSON + deflate) más el disco | Serializar en worker sobre el snapshot `shared_ptr<const Song>` |
| Copia al abrir | `src/app/src/score_document.cpp:502` | 10,7 ms una vez | Mover la canción al `Document` en vez de copiarla |

Ojo al mover el secuenciado: `AudioController::songEdited` decide entre
«actualizar mezcla» y «recargar» comparando la secuencia en el instante de
`songChanged`. Si la secuencia se publica más tarde hace falta una señal propia
(`sequenceChanged`), y la comparación `*sequence != *sequence_` debe ir también
al worker.

La solución de fondo es que el modelo ofrezca **instantáneas baratas**
(compases compartidos por `shared_ptr`, copy-on-write) y que cada comando
declare **qué ha cambiado** (`src/core/include/elara/core/document.hpp:47`). Con
eso desaparece la copia de 10,7 ms y se abre la puerta al recálculo incremental.

### 3.2 Concurrencia: pool compartido, coalescencia y cancelación (impacto medio-alto)

- **Pool de hilos persistente** en `src/core/include/elara/core/parallel.hpp:41-56`.
  Hoy cada `forEachIndex` crea hasta `hardware_concurrency()` hilos y el hilo
  llamante solo espera. Un secuenciado encadena varias tandas (notas, eventos y
  cada ronda de fusión) y un layout otras dos. Propuesta: pool único, el llamante
  participa en el trabajo, y un umbral de paralelismo por tamaño (hoy
  `kParallelMeasures = 48` lanza hilos para trabajos de microsegundos).
- **Un solo layout en vuelo, «último gana»** en `ScoreDocument::relayout()`
  (`src/app/src/score_document.cpp:466-485`). Hoy N ediciones rápidas lanzan N
  layouts completos, cada uno con su copia de la canción. `addTrack` lanza dos
  seguidos y tira el primero.
- **Cancelación cooperativa**: pasar un `std::stop_token` a
  `notation::layoutPages` (`page_layout.hpp:30`) y al pintado de páginas
  (`src/app/src/score_page.cpp:119-133`), comprobado entre sistemas y páginas.
- **Separar pools**: el layout ya paraleliza por dentro; lanzarlo desde el pool
  global de Qt, que también pinta una tarea por página, sobre-suscribe los núcleos.
- **Robustez**: si falla la creación de un hilo, el destructor del vector de
  `std::thread` llama a `std::terminate` (`parallel.hpp:41`); usar `std::jthread`.

El hilo de audio está bien resuelto: intercambio lock-free de la canción, niveles
atómicos, sin asignaciones ni bloqueos en `render()`. No hay que tocarlo.

### 3.3 Compilación (impacto medio, esfuerzo bajo)

- `CMakePresets.json` solo define `dev` (Debug) y `release` (**RelWithDebInfo**).
  No hay `build/release/CMakeCache.txt` en esta máquina para comprobar los flags efectivos; el valor por defecto de CMake para MSVC en esa configuración es `/Zi /O2 /Ob1 /DNDEBUG`, con inlining limitado (`/Ob1`).
- No hay `CMAKE_INTERPROCEDURAL_OPTIMIZATION` ni flags de arquitectura en ningún
  `CMakeLists.txt`, y los cinco módulos son bibliotecas **estáticas**: las
  llamadas entre módulos (`core` ↔ `notation` ↔ `audio`) no se pueden incrustar.

Propuesta: añadir un preset `dist` con `CMAKE_BUILD_TYPE=Release`, LTO activado
y, en MSVC, `/Ob2 /Gy /Gw` con `/OPT:REF,ICF` en el enlazado. Valorar `/arch:AVX2`
solo para `elara_audio` (bucles por muestra del sintetizador) y PGO con los
benchmarks como carga de entrenamiento. No usar `/fp:fast`: el sintetizador y los
snapshots dependen de resultados deterministas. Los benchmarks de
`docs/performance.md` deberían repetirse con ese preset.

### 3.4 Memoria (impacto medio)

| Qué | Dónde | Propuesta |
|---|---|---|
| Textura de página completa, crece con zoom² × dpr²; `cacheBuffer` mantiene páginas fuera de pantalla | `src/app/src/score_page.cpp:23-24`, `ScoreView.qml:279` | Pintar por teselas o limitar la resolución a la zona visible; liberar texturas fuera de pantalla |
| Textura con canal alfa siendo opaca | `src/app/src/score_painter.cpp:292` | `Format_RGB32` y textura opaca: ahorra mezcla a página completa |
| `load_buffer` copia el texto entero antes de parsear | `src/io/src/musicxml.cpp:1994`, `src/io/src/guitar_pro.cpp:1355` | `load_buffer_inplace` sobre el buffer propio |
| Un nodo PCDATA por hoja de texto en el DOM del GPIF | `src/io/src/guitar_pro.cpp:1354` | `parse_embed_pcdata` |
| La unión del exportador MusicXML copia el texto y mantiene dos copias | `src/io/src/musicxml_export.cpp:1730-1732` | Escribir cada parte en su tramo del buffer final o volcar por partes |
| DOM yyjson mutable completo + copia del texto al guardar | `src/io/src/native_format.cpp:104-108` | Emitir JSON directamente a un buffer, pista a pista |
| El secuenciador funde las listas por pista y el reproductor las vuelve a partir | `src/audio/src/sequence.cpp:696-711`, `src/audio/src/playback.hpp:35-50` | Conservar las listas por pista y dárselas al reproductor |
| Fusión por pares que reasigna y reinicializa en cada ronda | `src/core/src/performance.cpp:142` | Dos buffers alternos reservados una vez |
| `SoundingNote` 80 B, `PlayedNote` 64 B, `NoteEvent` 32 B | `core/performance.hpp:18`, `audio/sequence.hpp:31-66` | Campos de 8-16 bits: 48, 32 y 24 B |
| Historial de undo acotado por pasos (1000), no por memoria | `src/core/include/elara/core/document.hpp:42` | Añadir un tope por bytes retenidos |
| `Bravura.otf` cargada dos veces | `src/app/src/main.cpp:203`, `score_painter.cpp:54-57` | Compartir un único `QByteArray` |

### 3.5 Recalcular solo lo que cambia (impacto medio-alto, esfuerzo alto)

- **Layout incremental** (`src/notation/src/page_layout.cpp:289-304`): caché de
  medidas por compás con invalidación, y repintar solo las páginas cuyo contenido
  cambie. Hoy un cambio de mezcla (volumen, mute) también dispara un layout
  completo aunque la notación no lea la mezcla.
- **Secuenciado incremental**: no construir una `Sequence` nueva solo para
  comprobar si «suena igual» (`score_document.cpp:881`); clasificar los comandos
  (mezcla / información / notas) y resecuenciar únicamente los compases tocados.
- **Señales más finas**: `songChanged` notifica 12 propiedades y reconstruye la
  lista de pistas en cada nota (`score_document.cpp:891`).

## 4. Orden de trabajo recomendado

1. **Rápido y seguro (días)**: preset `dist` con LTO; `load_buffer_inplace` y
   `parse_embed_pcdata`; textura opaca; `reserve` donde falta; compactar
   `SoundingNote`/`NoteEvent`/`PlayedNote`; coalescer `relayout()`; evitar el
   layout cuando solo cambia la mezcla.
2. **Estructural (1-2 semanas)**: pool de hilos compartido con participación del
   llamante; secuenciado, carga del reproductor y guardado fuera del hilo de UI;
   cancelación de layout y pintado; listas de eventos por pista hasta el reproductor.
3. **De fondo (semanas)**: instantáneas con compartición estructural y comandos
   que declaran su alcance; layout y secuenciado incrementales; texturas por teselas.

Benchmarks que faltan para poder medir estas mejoras: latencia en el hilo de UI
de «editar una nota» de principio a fin, número de hilos creados por edición,
pico de memoria residente al importar/exportar, y memoria de texturas por nivel
de zoom.

## 5. Índice de hallazgos

| Impacto | Veredicto | Eje | Esf. | Subsistema | Hallazgo | Dónde |
|---|---|---|---|---|---|---|
| alto | confirmado | velocidad | M | app-documento | Cada edición copia la canción entera y la secuencia de nuevo en el hilo de UI (app-documento/A1) | `src/app/src/score_document.cpp:880` |
| alto | confirmado | concurrencia | L | core | El modelo no ofrece instantánea barata ni extensión del cambio: cada edición copia la canción entera y la resecuencia en el hilo de UI (core/A1) | `src/core/include/elara/core/document.hpp:47` |
| medio | confirmado | concurrencia | M | app-documento | Player::load se ejecuta en el hilo de UI en cada edición, aunque la reproducción esté parada (app-documento/A5) | `src/app/src/audio_controller.cpp:87` |
| medio | confirmado | concurrencia | M | app-pintado-qml | Los pintados obsoletos no se cancelan (solo se descarta su resultado) y una página aún sin pintar lanza un pintado completo por cada cambio de tamaño (app-pintado-qml/P2) | `src/app/src/score_page.cpp:120` |
| medio | confirmado | memoria | M | notation | Vectores anidados por beat y por columna que se liberan y vuelven a pedir en cada compás (notation/N4) | `src/notation/src/standard_staff.cpp:484` |
| medio | confirmado | memoria | M | core | soundingNotes: cada ronda de fusión asigna búferes nuevos, los inicializa y copia; las listas crecen sin reserve a través de una referencia al vector compartido (core/A4) | `src/core/src/performance.cpp:142` |
| medio | confirmado | velocidad | M | audio | El secuenciador fusiona las listas por pista en una lista global que el reproductor vuelve a partir por pista (audio/A1) | `src/audio/src/sequence.cpp:696` |
| medio | confirmado | memoria | S | io-gp | DOM con flags por defecto: un nodo PCDATA aparte por cada hoja de texto del GPIF (io-gp/GP3) | `src/io/src/guitar_pro.cpp:1354` |
| medio | confirmado | concurrencia | M | io-midi-nativo | Guardar .elara se ejecuta entero en el hilo de UI (JSON + deflate nivel 6 + escritura a disco) (io-midi-nativo/N1) | `src/app/src/score_document.cpp:843` |
| medio | parcial | concurrencia | M | app-documento | relayout() sin coalescencia ni cancelación: N ediciones rápidas = N layouts completos y N copias vivas (app-documento/A2) | `src/app/src/score_document.cpp:472` |
| medio | parcial | memoria | L | app-pintado-qml | Textura de página entera: la memoria crece con zoom² x dpr² y el cacheBuffer mantiene vivas páginas enteras fuera de pantalla (app-pintado-qml/P1) | `src/app/src/score_page.cpp:24` |
| medio | parcial | velocidad | L | notation | layoutPages vuelve a medir y maquetar todo el documento en cada edición: no hay caché por compás ni por sistema (notation/N1) | `src/notation/src/page_layout.cpp:289` |
| medio | parcial | concurrencia | M | notation | Dos tandas de std::thread creadas y destruidas por layout: no hay pool y el umbral de paralelismo es muy bajo (notation/N2) | `src/notation/src/page_layout.cpp:298` |
| medio | parcial | concurrencia | M | core | forEachIndex crea y destruye std::thread en cada llamada, sin pool ni tope global (core/A2) | `src/core/include/elara/core/parallel.hpp:44` |
| medio | parcial | velocidad | L | io-xml | Importar MusicXML: el parseo de pugixml es de un solo hilo y ya es ~85% del tiempo (io-xml/X1) | `src/io/src/musicxml.cpp:1994` |
| medio | parcial | velocidad | L | io-xml | Exportar .mxl: deflate monohilo sobre el texto entero, más dos copias del archivo resultante (io-xml/X2) | `src/io/src/archive.cpp:73` |
| medio | parcial | concurrencia | M | io-xml | forEachIndex crea y destruye hilos en cada llamada y el hilo llamante no trabaja (io-xml/X6) | `src/core/include/elara/core/parallel.hpp:44` |
| medio | parcial | memoria | S | io-gp | pugixml copia el GPIF entero: load_buffer en vez de load_buffer_inplace sobre el std::string que ya es propio (io-gp/GP1) | `src/io/src/guitar_pro.cpp:1355` |
| medio | parcial | memoria | M | io-midi-nativo | El escritor nativo construye un DOM yyjson mutable completo y luego duplica el texto (io-midi-nativo/N2) | `src/io/src/native_format.cpp:108` |
| bajo | confirmado | memoria | S | app-documento | Arranque: Bravura.otf se carga dos veces y Bravura.json (1,2 MB) se parsea de forma perezosa en la primera apertura (app-documento/A9) | `src/app/src/main.cpp:203` |
| bajo | confirmado | velocidad | S | app-pintado-qml | Reciclado de delegados: la página reutilizada enseña la textura de otra página hasta que llega su pintado, y los ítems del pool siguen repintando (app-pintado-qml/P5) | `src/app/src/score_page.cpp:67` |
| bajo | confirmado | velocidad | M | app-pintado-qml | Cursor de reproducción: QVariantMap nuevo, búsqueda lineal y cinco bindings más un handler JS por tick de un QTimer de 16 ms que no va sincronizado con el vsync (app-pintado-qml/P6) | `src/app/qml/ScoreView.qml:27` |
| bajo | confirmado | velocidad | S | app-pintado-qml | La textura de la página se crea con canal alfa aunque es opaca: mezcla a página completa y tres capas de sobrepintado por página (app-pintado-qml/P7) | `src/app/src/score_painter.cpp:292` |
| bajo | confirmado | velocidad | S | notation | stretchFor hace 60 bisecciones por sistema apretado, en serie, sobre una función lineal a trozos (notation/N8) | `src/notation/src/page_layout.cpp:185` |
| bajo | confirmado | velocidad | S | notation | toSvg crea una std::string por número y otra por elemento, sin reserve del resultado (notation/N9) | `src/notation/src/svg.cpp:16` |
| bajo | confirmado | memoria | S | core | SoundingNote ocupa 80 bytes por usar size_t e int en campos que caben en 8-16 bits (core/A7) | `src/core/include/elara/core/performance.hpp:18` |
| bajo | confirmado | velocidad | M | core | Transponer, reafinar y cambiar dinámica copian y comparan compás a compás en serie y crean un comando en el montón por compás (core/A8) | `src/core/src/transpose.cpp:138` |
| bajo | confirmado | concurrencia | S | core | forEachIndex termina el proceso si falla la creación de un hilo y no cancela el trabajo restante tras la primera excepción (core/A9) | `src/core/include/elara/core/parallel.hpp:41` |
| bajo | confirmado | velocidad | S | audio | noteOn de cuerda pulsada: tres pasadas sobre la línea de retardo, división y sin() por muestra en el hilo de audio (audio/A7) | `src/audio/src/synth.cpp:734` |
| bajo | confirmado | memoria | S | audio | El Loaded retirado no se libera mientras la reproducción está en pausa (audio/A8) | `src/audio/src/player.cpp:161` |
| bajo | confirmado | concurrencia | S | audio | position() puede devolver la posición anterior al seek durante un bloque (audio/A9) | `src/audio/src/player.cpp:166` |
| bajo | confirmado | memoria | M | io-xml | Exportar MusicXML en paralelo: la unión final copia los 124 MB y mantiene dos copias vivas (io-xml/X3) | `src/io/src/musicxml_export.cpp:1731` |
| bajo | confirmado | memoria | S | io-xml | Importar: load_buffer duplica el texto aunque el buffer ya es propio y desechable (io-xml/X4) | `src/io/src/musicxml.cpp:2026` |
| bajo | confirmado | velocidad | S | io-xml | Los buffers de lectura se rellenan a cero antes de sobrescribirlos enteros (io-xml/X7) | `src/io/src/archive.cpp:40` |
| bajo | confirmado | velocidad | S | io-xml | std::format con asignación por nota de percusión (y por bend/tempo) en el escritor (io-xml/X9) | `src/io/src/musicxml_export.cpp:1332` |
| bajo | confirmado | velocidad | S | io-gp | intOf()/trimmed() crean un std::string temporal por cada entero y por cada propiedad de texto (io-gp/GP7) | `src/io/src/guitar_pro.cpp:87` |
| bajo | confirmado | velocidad | S | io-gp | Caché de duraciones en unordered_map por pentagrama cuando los ids de Rhythm son densos (io-gp/GP8) | `src/io/src/guitar_pro.cpp:972` |
| bajo | confirmado | velocidad | M | io-midi-nativo | Importar MIDI: aritmética de Fraction con gcd repetida en los bucles calientes de rejillas y ritmos (io-midi-nativo/N3) | `src/io/src/midi_import.cpp:750` |
| bajo | confirmado | velocidad | S | io-midi-nativo | Al escribir, cada altura y duración se formatea con std::format/std::to_string a un std::string temporal y se vuelve a copiar (io-midi-nativo/N5) | `src/io/src/native_format.cpp:310` |
| bajo | confirmado | memoria | S | io-midi-nativo | Abrir .elara mantiene dos copias del JSON de 7,9 MB además del DOM y del modelo (io-midi-nativo/N9) | `src/io/src/native_format.cpp:948` |
| bajo | parcial | velocidad | M | app-documento | songChanged es demasiado grueso: cada nota reconstruye tracks(), el Repeater de pistas y el combo (app-documento/A3) | `src/app/src/score_document.cpp:891` |
| bajo | parcial | velocidad | M | app-documento | Para saber si 'suena igual' se construye otra Sequence completa y se compara elemento a elemento; el mix dispara también un relayout (app-documento/A4) | `src/app/src/score_document.cpp:881` |
| bajo | parcial | memoria | L | app-documento | Tres o más copias completas de core::Song vivas a la vez; al abrir, la copia se hace en el hilo de UI (app-documento/A6) | `src/app/src/score_document.cpp:502` |
| bajo | parcial | velocidad | M | app-documento | Cursor de reproducción movido por un QTimer preciso de 16 ms, no por el vsync, y con QVariantMap + búsqueda lineal por tick (app-documento/A7) | `src/app/src/audio_controller.cpp:16` |
| bajo | parcial | concurrencia | M | app-documento | La detección de tono (YIN) corre en el hilo de UI dentro del sondeo de 30 ms (app-documento/A8) | `src/app/src/tuner_controller.cpp:176` |
| bajo | parcial | velocidad | M | app-pintado-qml | El modelo del ListView es un entero: cuando cambia el número de páginas se destruyen todos los delegados y sus texturas (app-pintado-qml/P3) | `src/app/qml/ScoreView.qml:272` |
| bajo | parcial | velocidad | M | app-pintado-qml | Cada pintado de página vuelve a crear las fuentes desde sus bytes y a extraer todos los contornos: la caché de outlines vive solo dentro de una llamada (app-pintado-qml/P4) | `src/app/src/score_painter.cpp:130` |
| bajo | parcial | velocidad | S | notation | Tres std::vector temporales por acorde en el camino caliente de StandardWriter (notation/N3) | `src/notation/src/standard_staff.cpp:496` |
| bajo | parcial | concurrencia | M | notation | El montaje de páginas es serie: desplaza y copia todos los items del documento en el hilo llamante y sin reserve (notation/N5) | `src/notation/src/page_layout.cpp:327` |
| bajo | parcial | concurrencia | M | notation | Un layout obsoleto no se puede cancelar: layoutPages no acepta señal de parada y cada relayout lanza otro con todos los núcleos (notation/N6) | `src/notation/include/elara/notation/page_layout.hpp:30` |
| bajo | parcial | memoria | M | notation | Items de DisplayList con relleno evitable y size_t para el índice de compás (notation/N7) | `src/notation/include/elara/notation/display_list.hpp:57` |
| bajo | parcial | velocidad | S | core | Fraction calcula 2-3 gcd de 64 bits por suma/resta y vuelve a normalizar valores ya normalizados (core/A3) | `src/core/include/elara/core/fraction.hpp:50` |
| bajo | parcial | memoria | M | core | Cada nota y cada beat llevan un std::vector de 24 bytes para una curva casi siempre vacía, y cada beat una asignación propia para sus notas (core/A5) | `src/core/include/elara/core/effects.hpp:58` |
| bajo | parcial | memoria | M | core | El historial de undo se acota por número de pasos (1000), no por memoria retenida (core/A6) | `src/core/include/elara/core/document.hpp:42` |
| bajo | parcial | memoria | S | audio | eventsByTrack sin reserve y eventos duplicados (FrameEvent) respecto a Sequence (audio/A2) | `src/audio/src/playback.hpp:40` |
| bajo | parcial | concurrencia | M | audio | Cada forEachIndex del secuenciador y del render offline crea y destruye sus hilos (audio/A3) | `src/audio/src/sequence.cpp:698` |
| bajo | parcial | velocidad | S | audio | Las pistas silenciadas (mute, o no solo) se sintetizan completas y luego se descartan (audio/A4) | `src/audio/src/player.cpp:209` |
| bajo | parcial | velocidad | M | audio | Bucles por muestra de las voces: estado en memoria, ramas y salida anticipada impiden vectorizar (audio/A5) | `src/audio/src/synth.cpp:325` |
| bajo | parcial | memoria | M | audio | NoteEvent (32 B) y PlayedNote (64 B) usan size_t e int para valores de 8-16 bits (audio/A6) | `src/audio/include/elara/audio/sequence.hpp:32` |
| bajo | parcial | memoria | S | io-xml | Exportar: las reservas no tienen en cuenta el modo compacto ni las pistas sin TAB (hasta 2x de más) (io-xml/X5) | `src/io/src/musicxml_export.cpp:1726` |
| bajo | parcial | velocidad | S | io-xml | layoutPart recorre todas las notas de cada parte afinada solo para descartar <unpitched> (io-xml/X8) | `src/io/src/musicxml.cpp:482` |
| bajo | parcial | memoria | S | io-gp | En .gpx el contenedor descomprimido sigue vivo durante todo el parseo y el fichero se extrae sin reserve (io-gp/GP2) | `src/io/src/guitar_pro.cpp:1417` |
| bajo | parcial | velocidad | S | io-gp | intsOf() devuelve un std::vector<int> nuevo por compás, voz y beat (io-gp/GP4) | `src/io/src/guitar_pro.cpp:125` |
| bajo | parcial | concurrencia | M | io-gp | Fase serie antes de los hilos: las cinco tablas ById se construyen una tras otra, con vector intermedio sin reserve (io-gp/GP5) | `src/io/src/guitar_pro.cpp:978` |
| bajo | parcial | velocidad | S | io-gp | El fichero se lee byte a byte con istreambuf_iterator y sin tamaño previo (io-gp/GP6) | `src/io/src/guitar_pro.cpp:1449` |
| bajo | parcial | velocidad | S | io-gp | Lector binario gp3-5: textos descartados que se decodifican a UTF-8 y compases por pista sin reserve (io-gp/GP9) | `src/io/src/guitar_pro_binary.cpp:670` |
| bajo | parcial | concurrencia | M | io-midi-nativo | La lectura nativa procesa las pistas en serie sobre un documento yyjson inmutable (io-midi-nativo/N4) | `src/io/src/native_format.cpp:451` |
| bajo | parcial | velocidad | S | io-midi-nativo | gridSpelling consulta un std::map thread_local con clave tupla en cada candidato, celda e iteración (io-midi-nativo/N6) | `src/io/src/midi_import.cpp:453` |
| bajo | parcial | memoria | M | io-midi-nativo | Importar MIDI: vectores temporales por compás, por acorde y por celda (cientos de miles de asignaciones concurrentes) (io-midi-nativo/N7) | `src/io/src/midi_import.cpp:705` |
| bajo | parcial | memoria | M | io-midi-nativo | Exportar MIDI pasa por un MidiEvent de 48 bytes con std::string por cada note-on/off, sin reserve, y calcula los ticks con fracciones (io-midi-nativo/N8) | `src/io/src/midi_export.cpp:277` |


## 6. Detalle por subsistema

Cada hallazgo incluye la evidencia citada, la propuesta y la nota del segundo
revisor, que es la que prevalece cuando matiza o corrige al primero.

### App: documento y controladores

#### app-documento/A1 · Cada edición copia la canción entera y la secuencia de nuevo en el hilo de UI

- **Dónde:** `src/app/src/score_document.cpp:880`
- **Eje:** velocidad · **Impacto:** alto · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
song_ = std::make_shared<const core::Song>(document_->song());
...
auto sequence = std::make_shared<const audio::Sequence>(*song_);
```

**Por qué importa.** ScoreDocument::edited() se ejecuta de forma síncrona en cada tecla (typeDigit, deleteAtCaret, undo/redo...). Con los tiempos medidos en la canción grande: copia 10,7 ms (línea 877) + secuenciado 28 ms (línea 880) = ~39 ms de hilo de UI bloqueado por pulsación, es decir 2-3 frames perdidos a 60 Hz antes de que arranque siquiera el layout. Además Sequence::Sequence llama dos veces a core::forEachIndex (sequence.cpp:469 y 638), que crea y hace join de hardware_concurrency std::thread por llamada: el hilo de UI lanza y espera ~2xN hilos por tecla, compitiendo con el layout y el pintado de la edición anterior que siguen en el pool de QtConcurrent.

**Propuesta.** Sacar copia+secuencia del hilo de UI: edited() solo toma el snapshot (ver A6 para abaratarlo) y lanza UNA tarea de worker que construye Sequence y layout juntos (como ya hace openAndLayOut en la línea 167); apply() publica sequence_ y emite songChanged al terminar. duration() y cursorAt() siguen usando la secuencia anterior hasta entonces, igual que ya ocurre con pages_. Mantener en el hilo de UI solo clampCaret y historyChanged.

**Nota del revisor.** Comprobado en src/app/src/score_document.cpp:875-894: edited() copia la canción (l.877) y construye audio::Sequence (l.880) de forma síncrona; lo llaman execute() (l.869), undo/redo (l.823/830) y typeDigit (l.1121), todos Q_INVOKABLE desde QML, es decir hilo de UI. Con los tiempos medidos son ~39 ms por pulsación en la canción grande (en canciones normales es despreciable: el 'alto' vale solo para ficheros grandes). El recuento de hilos está infravalorado: además de sequence.cpp:469 y 638, hay forEachIndex en cada ronda de mezcla (sequence.cpp:698, ~5 rondas con 21 pistas) y en core::soundingNotes (performance.cpp:118/134); parallel.hpp:41-56 crea y hace join de std::thread en cada llamada, sin pool. Matices a la propuesta: (1) AudioController::songEdited (audio_controller.cpp:64-71) decide 'updateMix o load' comparando document->sequence() con loaded_ en el mismo instante en que se emite songChanged; si sequence_ se publica más tarde hace falta una señal propia (sequenceChanged) o emitir songChanged dos veces, si no el audio se queda con la secuencia vieja. (2) La comparación *sequence != *sequence_ (l.881) que conserva la identidad para ADR-0039 debe ir también al worker. (3) Hoy los resultados con generación obsoleta se descartan (l.476): si la secuencia viaja con el layout, tecleando seguido nunca se publicaría; hay que combinarlo con A2. (4) newSong (l.668-669) y apply (l.510-511) también secuencian en el hilo de UI. No afecta al hilo de audio ni al determinismo.

#### app-documento/A5 · Player::load se ejecuta en el hilo de UI en cada edición, aunque la reproducción esté parada

- **Dónde:** `src/app/src/audio_controller.cpp:87`
- **Eje:** concurrencia · **Impacto:** medio · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
player_->pause();
player_->seek(0.0);
if (document_ && document_->song() && document_->sequence()) {
    player_->load(*document_->song(), *document_->sequence());
```

**Por qué importa.** songEdited() está conectado directo a songChanged, así que corre dentro de ScoreDocument::edited(), antes de devolver el control al bucle de eventos. Player::load (player.cpp:71) reparte los 320k eventos por pista convirtiéndolos a frames (detail::eventsByTrack), calcula el headroom recorriéndolos, construye un Synth y un buffer por pista (21 en la canción grande) y libera el Loaded anterior con tidy(): del orden de milisegundos de asignación y copia por tecla, en el hilo de UI, una vez que el usuario ha pulsado play alguna vez (player_ != nullptr). Si el usuario teclea 50 notas en pausa, se construyen y se tiran 49 cargas. Efecto colateral: cada edición rebobina a 0 (seek(0.0)) y para la reproducción.

**Propuesta.** Carga perezosa: en songEdited(), si no se está reproduciendo, solo marcar stale_=true y soltar loaded_; hacer el load en togglePlayback()/seek() cuando haga falta. Si se está reproduciendo, preparar el Loaded en un worker (separar Player::load en un 'prepare' sin estado compartido que devuelva el Loaded y un 'commit' en el hilo de control que haga el exchange de pending_, que ya es lock-free) y conservar la posición en lugar de seek(0).

**Nota del revisor.** Comprobado: songChanged está conectado con conexión directa a songEdited (audio_controller.cpp:36-37, mismo hilo), que llama a loadSong() (l.81-96) -> pause, seek(0) y Player::load (l.87) dentro de edited(). Player::load (player.cpp:71-103) reparte todos los eventos con push_back sin reserve (playback.hpp:36-50), crea un Synth y un buffer por pista y libera el Loaded anterior. Matices: no es 'en cada edición' sino en cada edición que cambia la secuencia; si la secuencia es la misma (mezcla, título) songEdited solo hace updateMix (l.65-69). Y solo desde que existe player_ (primer play, l.102-107). El coste no está medido: por tamaño (320k eventos, 21 sintetizadores) son milisegundos sumados a los ~39 ms de A1, 'medio' es razonable. El rebobinado a 0 y la pausa al editar son comportamiento funcional documentado en el comentario de l.80, no un coste: cambiarlo es decisión de producto. Viabilidad: carga perezosa correcta si (1) se actualiza igualmente duration_ del Player, porque seek() acota con él (player.cpp:137-140), y (2) no se 'suelta' el Loaded que tiene el hilo de audio: solo puede retirarse por el intercambio pending_/retired_ existente. Separar prepare (worker) y commit (hilo de control) es seguro: latest_ y tidy() son solo del hilo de control y render() sigue sin asignar ni bloquear; Loaded es privado de Player, hay que exponer un tipo opaco.

#### app-documento/A2 · relayout() sin coalescencia ni cancelación: N ediciones rápidas = N layouts completos y N copias vivas

- **Dónde:** `src/app/src/score_document.cpp:472`
- **Eje:** concurrencia · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
const std::uint64_t generation = ++generation_;
setPending(+1);
QtConcurrent::run([song = song_, track = track_, view = view_] {
    return layOut(song, track, view);
```

**Por qué importa.** generation_ solo descarta el RESULTADO (línea 476); el trabajo obsoleto se ejecuta entero. Con autorepetición de tecla (~30 Hz) y vista de todas las pistas (21 ms usando todos los núcleos, 132 ms de CPU) se encolan layouts más rápido de lo que terminan: la latencia hasta ver la última edición crece linealmente y cada tarea encolada retiene su propio snapshot de Song (shared_ptr capturado, decenas de MB en la canción grande). Cada layoutPages crea además sus propios hilos (core::forEachIndex) dentro de un hilo del pool global, el mismo pool donde ScorePage pinta una tarea por página: 2 layouts solapados + pintado = sobre-suscripción de 2-3x núcleos. Hay además dobles layouts gratuitos: addTrack (líneas 808-809) hace execute()->edited()->relayout() y acto seguido setTrack()->relayout(); el primero se calcula y se tira.

**Propuesta.** Single-flight con 'último gana': si hay un layout en vuelo, marcar relayoutPending_=true y no encolar; al terminar el actual, lanzar uno solo con el snapshot más reciente. Coalescer las llamadas del mismo turno del bucle de eventos (QTimer::singleShot(0) o QMetaObject::invokeMethod en cola) para que addTrack/newSong generen un único layout. Pasar un std::stop_token/atomic<bool> a notation::layoutPages comprobado entre sistemas/páginas y activarlo al incrementar generation_. Lanzar el layout en un QThreadPool propio de 1 hilo (ya paraleliza por dentro) para no robar hilos al pintado.

**Nota del revisor.** El código es como se describe: relayout() (score_document.cpp:466-485) incrementa generation_ y lanza siempre QtConcurrent::run en el pool global; la generación solo descarta el resultado (l.476), no hay cancelación ni coalescencia; cada tarea captura su shared_ptr<const Song>. ScorePage usa el mismo pool global (score_page.cpp:120) y layoutPages crea hilos propios (page_layout.cpp:298, system_layout.cpp:593). El doble layout de addTrack es real (l.808-809: execute->edited->relayout y luego setTrack->relayout). newSong NO lo tiene (un solo relayout, l.682). Lo inflado: la 'latencia que crece linealmente' no se sostiene con los tiempos medidos; hoy el hilo de UI tarda >=39 ms por edición (A1) y el layout 3,5-21 ms, así que el cuello es el hilo de UI y como mucho se solapan 1-2 layouts y 1-2 snapshots extra; el encolado sin cota solo aparecería DESPUÉS de arreglar A1. El efecto real que sí se deduce del código es otro: con ediciones más rápidas que el layout, todos los resultados llegan con generación vieja y se descartan, la página no se refresca hasta que se suelta la tecla. Por eso la propuesta 'single-flight + último gana' debe además permitir aplicar resultados intermedios, y hoy apply() no lo admite: con layout.song != song_ (l.508-511) retrocedería song_ y secuenciaría en el hilo de UI; habría que aplicar solo pages_/pagesSong_. stop_token en layoutPages y pool propio de 1 hilo son viables y no tocan determinismo (el resultado cancelado se tira). Coalescer con singleShot(0) cambia el momento de busyChanged/layoutChanged: revisar tests que esperan esas señales.

#### app-documento/A9 · Arranque: Bravura.otf se carga dos veces y Bravura.json (1,2 MB) se parsea de forma perezosa en la primera apertura

- **Dónde:** `src/app/src/main.cpp:203`
- **Eje:** memoria · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Bravura.otf"));
```

**Por qué importa.** main() registra la fuente de forma síncrona en el hilo de UI antes de cargar el QML, y ScoreDocument::painter() (score_document.cpp:496, ScoreFonts::fromDirectory en score_painter.cpp:258-261) vuelve a leer Bravura.otf entero a un QByteArray propio junto con Arimo: dos copias de la fuente musical residentes durante toda la sesión. musicMetrics() (score_document.cpp:77-90) lee y parsea el JSON de 1,2 MB dentro del primer layOut, así que ese coste se suma al tiempo hasta la primera página del primer fichero abierto; y como placeCaret (línea 1085) y placeCaretBox (línea 1248) también llaman a musicMetrics() desde el hilo de UI, una tecla o clic durante ese primer layout bloquea la UI esperando la inicialización del static.

**Propuesta.** Precalentar en paralelo con engine.loadFromModule: nada más crear QGuiApplication, QtConcurrent::run([]{ musicMetrics(); ScoreDocument::painter(); }) (exponer un ScoreDocument::warmUp() estático). Compartir el buffer: leer Bravura.otf una sola vez y registrar con QFontDatabase::addApplicationFontFromData(fonts.music) el mismo QByteArray que usa ScorePainter. Opcional: precompilar en build las métricas que usa el layout a una tabla binaria/constexpr para no parsear 1,2 MB de JSON en cada arranque.

**Nota del revisor.** Comprobado: main.cpp:203 registra :/fonts/Bravura.otf de forma síncrona antes de cargar el QML, y ScoreDocument::painter() (score_document.cpp:495-498) vuelve a leer Bravura.otf y las dos Arimo a QByteArray propios (score_painter.cpp:54-57, 258-262): hay dos copias de la fuente musical en memoria. musicMetrics() (l.76-92) es un static perezoso que lee y parsea Bravura.json dentro del primer layOut (l.125), en el worker; placeCaret (l.1085) y placeCaretBox (l.1248) lo llaman desde el hilo de UI, así que una tecla durante ese primer layout espera a la inicialización (o la hace el propio hilo de UI tras newSong si llega antes que el worker). Todo es coste de una sola vez y sin medir: la copia duplicada son cientos de KB y el parseo se paga una vez por sesión; 'bajo' es correcto. Los tamaños (1,2 MB) no se pueden comprobar: las fuentes vienen de ELARA_FONT_DIR, fuera del repo. Propuesta viable con dos condiciones: el registro de la fuente debe seguir completándose antes de engine.loadFromModule (el logo y los símbolos de la UI la usan), así que el warmUp en worker solo sirve para musicMetrics() y el painter, no para addApplicationFontFromData; y compartir el QByteArray funciona porque es de copia implícita. Precompilar las métricas en build ya no es esfuerzo S.

#### app-documento/A3 · songChanged es demasiado grueso: cada nota reconstruye tracks(), el Repeater de pistas y el combo

- **Dónde:** `src/app/src/score_document.cpp:891`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
clampCaret();
emit caretChanged();
emit songChanged();
emit historyChanged();
relayout();
```

**Por qué importa.** songChanged es el NOTIFY de 13 propiedades (score_document.hpp:53-73: title, artist, trackNames, measureCount, duration, tempo, compás/armadura, tracks...). Tras cada tecla QML reevalúa todas. tracks() (línea 569) reconstruye un QVariantList con un QVariantMap de 12 claves por pista y llama a trackTuning(), que prueba los 15 presets construyendo core::Tuning::preset(id) en cada vuelta (líneas 628-629): 20 pistas x 15 = 300 construcciones de Tuning + cientos de QVariant/QString por edición. Peor: SongPane.qml:336 usa 'model: root.document.tracks' (array JS), así que el Repeater destruye y recrea las 20 filas (sliders, botones) en cada nota, y SongPane está siempre instanciado (Main.qml:463, solo width 0 al ocultarse), luego se paga aunque el panel esté cerrado. Main.qml:354 '[qsTr("All tracks")].concat(score.trackNames)' también reemplaza el modelo del combo. Estimación: varios ms de hilo de UI por tecla (20 delegados complejos), sumados a A1. Además caretChanged (9 propiedades NOTIFY) se emite 2 veces por edición: aquí y en placeCaretBox() (línea 1260), que emite siempre aunque página y rectángulo no cambien.

**Propuesta.** Partir la señal: infoChanged (título/artista), signaturesChanged, tracksChanged, durationChanged, y emitir cada una solo si el valor cambió (guardar el QStringList/descriptor anterior y comparar; una nota tecleada no cambia ninguna salvo duration). Exponer las pistas como QAbstractListModel con dataChanged por fila/rol en vez de QVariantList. Cachear el preset que casa con cada afinación (o comparar contra una tabla estática de Tuning ya construidos). En placeCaretBox() emitir caretChanged solo si cambian caretPage_/caretRect_/caret_, y quitar el emit duplicado de edited(). Envolver el contenido de SongPane en Loader { active: shown }.

**Nota del revisor.** Cierto: emit songChanged en edited() (l.891); es NOTIFY de 12 propiedades, no 13 (score_document.hpp:53-57 y 67-73); tracks() (l.569-594) reconstruye un QVariantMap de 12 claves por pista; SongPane está siempre instanciado (Main.qml:463-472, width 0/visible false) con 'model: root.document.tracks' (SongPane.qml:336); Main.qml:354 concatena trackNames. Inflado o no demostrable: (a) '300 construcciones de Tuning' es cota máxima: trackTuning corta en el primer preset que casa (l.628-632; 'guitar' es el primero) y las pistas sin cuerdas salen antes (l.624); Tuning es un tipo de capacidad fija sin asignación y preset() parsea una cadena de ~20 caracteres: microsegundos en total. (b) La afirmación clave, que el Repeater destruye y recrea las 20 filas en cada nota, no se puede confirmar con el código del repo y probablemente es falsa: QQuickRepeater::setModel y QQuickComboBox::setModel retornan sin hacer nada si el QVariant nuevo es igual al anterior, y un QVariantList de mapas con el mismo contenido compara igual; una nota tecleada no cambia nombre/mezcla/afinación. La recreación sí ocurre al cambiar mezcla, nombre o pistas (una vez por acción). Hay que medirlo (contador en Component.onCompleted del delegado) antes de invertir en un QAbstractListModel. Coste residual por tecla: 12 bindings + construir y comparar la lista, muy por debajo de 1 ms. (c) caretChanged no se emite dos veces en el mismo turno: una en edited() (l.890) y otra diferida en apply()->placeCaretBox() (l.532, l.1260) cuando llega el layout; la segunda es necesaria (cambia caretRect). Es cierto que placeCaretBox emite sin comparar. Propuesta: NO quitar el emit de edited(): caretStatus/caretFretted/caretMeasure leen document_ y cambian en el acto, y si el layout se descarta por generación no habría ningún aviso; mejor guardar el emit de placeCaretBox con comparación. Loader{active: shown} rompería la animación de cierre (Behavior on width): usar active: shown || width > 0.

#### app-documento/A4 · Para saber si 'suena igual' se construye otra Sequence completa y se compara elemento a elemento; el mix dispara también un relayout

- **Dónde:** `src/app/src/score_document.cpp:881`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
if (!sequence_ || *sequence != *sequence_) {
    sequence_ = std::move(sequence);
}
```

**Por qué importa.** Soltar un fader (setTrackMix), cambiar título o artista (SetSongInfo) pagan los 10,7 ms de copia + 28 ms de secuenciado solo para descubrir, con un operator== por defecto sobre 160k PlayedNote + 320k NoteEvent + measures (sequence.hpp:132), que nada cambió, y tirar el resultado. Durante la comparación hay dos Sequence vivas a la vez (pico de memoria doble: cientos de miles de eventos duplicados). Después, la línea 893 lanza relayout() incondicional: un cambio de volumen/pan/mute/solo recalcula todas las páginas (21 ms x todos los núcleos) para obtener display lists idénticas que ScorePage luego descarta comparando.

**Propuesta.** Que core::Command declare qué invalida (bitmask: Playback, Layout, Mix, Info) y que Document lo exponga para la última operación aplicada/deshecha. edited() entonces: Mix -> no secuenciar ni relayout (solo updateMix); Info -> relayout sin secuenciar; resto -> ambos (en worker, ver A1). Se elimina la comparación profunda y la segunda Sequence temporal. Si se quiere conservar la comparación como red de seguridad, hacerla en el worker.

**Nota del revisor.** Cierto en el código: edited() construye siempre una Sequence nueva y la compara con operator== por defecto (score_document.cpp:880-883; sequence.hpp:24/44/65/132), y llama a relayout() sin condición (l.893); notation no lee la mezcla (ninguna referencia a mix/mute/solo/audible en src/notation), así que el layout tras un cambio de mezcla produce páginas idénticas que ScorePage descarta comparando (score_page.cpp:111). Impacto inflado: ocurre una vez por acción discreta, no por frame ni por tecla; el arrastre del fader va por previewTrackMix (l.954-967, sin copia ni secuencia) y setTrackMix solo se ejecuta al soltar o al pulsar mute/solo (SongPane.qml:487-522) y retorna si la mezcla no cambia (l.907). Son ~40 ms + un layout desperdiciado por gesto en la canción grande; la comparación en sí es un recorrido lineal de vectores (pocos ms) y el pico de memoria doble es transitorio. El cambio de título/artista sí necesita relayout (el título se dibuja), solo sobra el secuenciado. Propuesta viable pero con riesgo: la comparación profunda es hoy la garantía de ADR-0039 de que playback continúa solo si suena igual; una máscara declarada por cada core::Command (y por undo/redo y comandos compuestos) mal puesta deja audio obsoleto sin que nada lo detecte. Recomendable mantener la comparación como aserción en debug/tests. Aun con Mix hay que refrescar song_ para tracks().

#### app-documento/A6 · Tres o más copias completas de core::Song vivas a la vez; al abrir, la copia se hace en el hilo de UI

- **Dónde:** `src/app/src/score_document.cpp:502`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** L · **Veredicto:** parcial

```cpp
if (layout.opened) {
    document_.emplace(*layout.song);
```

**Por qué importa.** El worker ya tiene la canción (línea 159) pero apply() la copia entera en el hilo de UI al abrir: 10,7 ms bloqueados justo cuando se va a mostrar la primera página. En régimen de edición conviven: document_ (la editable), song_ (snapshot, línea 877), pagesSong_ (snapshot del último layout aplicado, distinto de song_ hasta que termina el relayout, líneas 340 de la cabecera y 527) y un snapshot por cada relayout en vuelo (captura de la línea 472). Solo notas y beats son 160k x 48 B + 80k x 64 B = ~12,8 MB por copia, más vectores y cabeceras de heap: 3-4 copias = 50-80 MB transitorios tecleando rápido en la canción grande, además de los 10,7 ms de copia por tecla de A1.

**Propuesta.** Corto plazo (S): en openAndLayOut construir también en el worker la copia editable y moverla (ScoreLayout con un core::Song editable; document_.emplace(std::move(...))), y con A2 limitar a un layout en vuelo para acotar los snapshots a 3. Fondo (L): compartición estructural en el modelo: que Staff guarde std::shared_ptr<const Measure> (o Track por COW) de modo que el snapshot sea copiar ~10k punteros (decenas de microsegundos) y solo se duplique el compás editado; elimina a la vez el coste de 10,7 ms por edición y las copias duplicadas.

**Nota del revisor.** Cierto: apply() hace document_.emplace(*layout.song) (score_document.cpp:502), una copia completa en el hilo de UI al abrir (core::Document recibe Song por valor, document.hpp:45). Pero son 10,7 ms una sola vez por apertura, despreciable frente a leer y maquetar el fichero. El recuento de copias está inflado: en reposo pagesSong_ y song_ son EL MISMO shared_ptr (apply asigna ambos desde layout.song, l.509 y 527), así que hay 2 copias (document_ + snapshot), 3 solo mientras hay un relayout en vuelo tras una edición, y más solo si se solapan layouts (ver A2, acotado hoy por el hilo de UI). Los tamaños 48 B/64 B por nota/beat no se pueden verificar sin compilar; decenas de MB transitorios solo en la canción gigante. El beneficio real de la compartición estructural (quitar 10,7 ms de copia por tecla) ya está contado en A1. Propuesta corta: viable, pero ScoreLayout llega como const& a la continuación de QFuture (l.453); hay que transportar la copia editable en un std::shared_ptr<core::Song> no const y mover desde el puntero; meter un Song por valor en ScoreLayout añadiría copias. Propuesta larga (shared_ptr<const Measure>/COW): correcta si toda mutación clona antes de escribir; toca core y todos los Command (apply/revert), esfuerzo L real y riesgo alto para la ganancia.

#### app-documento/A7 · Cursor de reproducción movido por un QTimer preciso de 16 ms, no por el vsync, y con QVariantMap + búsqueda lineal por tick

- **Dónde:** `src/app/src/audio_controller.cpp:16`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
transportTimer_.setInterval(16ms);
transportTimer_.setTimerType(Qt::PreciseTimer);
```

**Por qué importa.** 16 ms son 62,5 Hz frente a 60 Hz de pantalla: el cursor repite o salta un paso periódicamente (batido), y en pantallas de 120/144 Hz se actualiza en menos de la mitad de los frames; el PreciseTimer en Windows fuerza además resolución de temporizador de 1 ms mientras suena. Cada tick emite positionChanged y QML reevalúa 'document.cursorAt(cursorSeconds)' (ScoreView.qml:27-28): score_document.cpp:377 llama a notation::cursorAt, que recorre todas las páginas y sus MeasureBox desde la primera (cursor.cpp, doble for; hasta 500 cajas por tick al final de la canción) y devuelve un QVariantMap de 4 claves QString (líneas 382-385) que se convierte a objeto JS: 4 QString + 4 QVariant + nodo de mapa asignados 62 veces por segundo, más las 4 ligaduras del playhead, el texto del reloj (Main.qml:632) y el slider (Main.qml:654).

**Propuesta.** Dirigir el cursor desde el frame: FrameAnimation en ScoreView (o QQuickWindow::afterAnimating) que lea audio.position bajo demanda mientras playingSong, y dejar un temporizador grueso (100-250 ms) solo para reloj/slider y para detectar el final. En ScoreDocument, construir en apply() un índice compás -> (página, MeasureBox*) (vector de measureCount entradas) para que cursorAt sea O(log onsets), y devolver un Q_GADGET o propiedades (cursorPage:int, cursorRect:QRectF) en vez de QVariantMap. Emitir el reloj solo cuando cambie el segundo entero.

**Nota del revisor.** Cierto: transportTimer_ de 16 ms con Qt::PreciseTimer (audio_controller.cpp:16-17); ScoreView.qml:27-28 reevalúa document.cursorAt(cursorSeconds) en cada positionChanged; cursorAt devuelve un QVariantMap de 4 claves (score_document.cpp:382-385) y notation::cursorAt hace un doble for lineal sobre páginas y MeasureBox (cursor.cpp:35-41). Impacto inflado: el temporizador solo corre mientras suena (se para en pollTransport, l.164-166, y positionChanged solo se emite si la posición cambia, l.156); playedMeasureAt ya es búsqueda binaria (sequence.cpp:735) y xAt también (cursor.cpp:21); recorrer <=500 cajas y asignar un mapa de 4 entradas 62 veces por segundo son microsegundos frente a pintados de 5-19 ms. Lo que queda es calidad visual (62,5 Hz frente al vsync), no rendimiento. Lo del timer de 1 ms en Windows es comportamiento de Qt, no verificable en el repo, y solo durante la reproducción. Matiz importante a la propuesta: Player::position() devuelve position_, que el hilo de audio solo publica al final de cada render() (player.cpp:146-150, 189): avanza a saltos de un bloque de audio. Un FrameAnimation (disponible, el proyecto exige Qt 6.12) leyendo esa posición por frame no da un cursor más suave si no se interpola en el hilo de UI desde la última posición y su instante; leer el atómico por frame es seguro para el hilo de audio. El índice compás->caja y el Q_GADGET son correctos pero de ganancia inapreciable.

#### app-documento/A8 · La detección de tono (YIN) corre en el hilo de UI dentro del sondeo de 30 ms

- **Dónde:** `src/app/src/tuner_controller.cpp:176`
- **Eje:** concurrencia · **Impacto:** bajo · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
for (std::size_t got = input_->read(block_); got > 0; got = input_->read(block_)) {
    changed = tuner_->push(std::span(block_).first(got)) || changed;
}
```

**Por qué importa.** Tuner::push (tuner.cpp:28-45) desplaza la ventana y llama a detector_.detect(window_) cada vez que hay medio frame nuevo, todo en el hilo principal: el afinador comparte hilo con las ediciones (A1 bloquea ~40 ms) y con el render de QML, así que la aguja tartamudea cuando la UI trabaja y viceversa. Tras un bloqueo del hilo de UI el bucle vacía todo el atraso del anillo con varios push seguidos, cada uno con su detect completo, justo cuando el hilo ya va tarde; solo la última ventana aporta lectura útil. showReading() (línea 220) notifica además 6 propiedades y reasigna noteName_ (QString nuevo) aunque la nota no haya cambiado.

**Propuesta.** Mover Tuner + sondeo a un QObject en un QThread propio (o a un hilo que espere en el anillo) y publicar la lectura por señal en cola con un struct pequeño; el hilo de UI solo pinta. Mientras tanto: en poll() acumular el atraso y hacer un único push con las últimas frameSize muestras (push ya toma la cola cuando samples.size() >= size), y emitir readingChanged solo si cambian nota/cents por encima de un umbral (p. ej. 0,5 cents).

**Nota del revisor.** Cierto: poll() (tuner_controller.cpp:174-190) corre con un QTimer de 30 ms en el hilo de UI (l.82-83) y Tuner::push (tuner.cpp:28-46) llama a detector_.detect cada medio frame nuevo. El coste es bajo: YIN con salida temprana en el primer mínimo (pitch_detector.cpp:83-96); peor caso ~maxLag x window = 1600 x 1600 operaciones a 48 kHz, del orden de 1 ms cada ~33 ms. Lo que no se sostiene: 'varios push seguidos, cada uno con su detect' tras un bloqueo. block_ tiene 8192 muestras (l.149) y frameSize a 48 kHz es ~3201, así que un atraso de hasta ~170 ms sale en un solo read -> un solo push -> un solo detect (push ya se queda con la cola cuando samples.size() >= size, tuner.cpp:30-32). Solo habría varios detect con atrasos mayores, acotados por el anillo de entrada. Por tanto el paliativo propuesto ('un único push con las últimas muestras') ya ocurre de hecho. readingChanged es NOTIFY de 7 propiedades (tuner_controller.hpp:27-33), no 6, y showReading emite sin comparar (l.208-221): cierto pero barato. Mover el afinador a un hilo propio es viable (un solo consumidor del anillo; setOptions y takePeak deben ir a ese hilo), pero el tartamudeo citado lo causa A1, no el afinador: arreglando A1 esto pierde casi todo su interés. Filtrar readingChanged por umbral es correcto y trivial.

**Observaciones del área (qué está bien resuelto).**

Auditoría de solo lectura (no se ha editado, compilado ni ejecutado nada). Leídos enteros: score_document.cpp/.hpp, audio_controller.cpp, tuner_controller.cpp, main.cpp, startup.cpp, score_page.cpp; por secciones: player.cpp, sequence.cpp/.hpp, caret.cpp, cursor.cpp, tuner.cpp, parallel.hpp, document.cpp y los QML que consumen el documento (Main.qml, ScoreView.qml, SongPane.qml).
Bien resuelto, no tocar:
- Abrir fichero: lectura, Sequence y layout se hacen en el worker (openAndLayOut) y generation_ impide que un resultado viejo pise uno nuevo (también en newSong, línea 666).
- ScoreDocument::page() devuelve un shared_ptr con aliasing sobre el vector de páginas: un relayout no puede liberar una página a medio pintar. ScorePage conserva la textura si la display list no cambió.
- Player: intercambio de canción lock-free (pending_/retired_ atómicos), la liberación del Loaded viejo la hace el hilo de control en tidy(), updateMix escribe niveles atómicos; en render()/renderBlock() no he visto asignaciones ni locks. No he encontrado carreras de datos en los ficheros del área: todo el estado de ScoreDocument/AudioController se toca solo desde el hilo de UI.
- transportTimer_ se para solo al dejar de sonar (pollTransport) y pollTimer_ de 50 ms solo vive durante el tono de prueba; no hay temporizadores ociosos.
- TunerController reutiliza block_ (8192 muestras, sin asignar por sondeo), la lectura del micrófono es un anillo noexcept y el pico un atomic; el micro se cierra al desactivar el afinador.
- musicMetrics() y painter() son statics con inicialización segura entre hilos y se construyen una sola vez.
- startup.cpp es solo el arnés de smoke-test/captura: el objeto Startup y su temporizador de 30 ms solo existen con --smoke-test/--screenshot/--track/--view/--seek y el temporizador se para al acabar los pasos; no añade coste a un arranque normal.
- placeCaretBox/clampCaret/caretAt son búsquedas lineales pequeñas (cajas de compás y columnas de una página): microsegundos por tecla; el problema ahí es la señal emitida de más (A3), no el cálculo. Sequence::secondsAt es lineal pero solo se llama en un clic.
Orden de ataque sugerido: A2 (coalescer) y A4 (clasificar comandos) son los de mejor relación impacto/esfuerzo; A1 depende de decidir si songChanged puede ser asíncrono; A6 (compartición estructural) es el cambio de fondo que elimina de raíz los 10,7 ms por edición.

### App: pintado y QML

#### app-pintado-qml/P2 · Los pintados obsoletos no se cancelan (solo se descarta su resultado) y una página aún sin pintar lanza un pintado completo por cada cambio de tamaño

- **Dónde:** `src/app/src/score_page.cpp:120`
- **Eje:** concurrencia · **Impacto:** medio · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
QtConcurrent::run([page = std::move(page), scale, ratio] {
        return ScoreDocument::painter().render(*page, scale, ratio);
```

**Por qué importa.** El único control de obsolescencia es `if (ticket != ticket_) { return; }` (línea 123): la tarea ya encolada o en curso en el QThreadPool global se ejecuta entera, reserva su QImage completo (3,6-134 MB, ver P1) y retiene con su shared_ptr el layout antiguo, para tirarlo al terminar. Dos caminos lo disparan: (a) `schedule()` (líneas 88-92) solo usa el temporizador de 120 ms `if (painted_)`; mientras el primer pintado no ha vuelto hace `repaint()` inmediato, así que cada página que entra durante la animación de zoom (Behavior on shownZoom, 180 ms, ~11 fotogramas con tamaño distinto en cada uno) encola un pintado por fotograma hasta que llega el primero; (b) al hacer zoom a saltos con pausas >120 ms, o teclear rápido sobre una página visible, cada generación encola 3-6 páginas mientras la anterior sigue pintando. A zoom alto son decenas de ms y decenas de MB por tarea inútil, y como la cola es FIFO retrasan justo el pintado que sí se va a mostrar. Además las páginas del cacheBuffer compiten con la visible con la misma prioridad. Agravante: el layout no usa ese pool sino `std::thread` nuevos por llamada hasta hardware_concurrency (src/core/include/elara/core/parallel.hpp:41), de modo que un relayout que coincide con pintados deja ~2N hilos ejecutables sobre N núcleos.

**Propuesta.** 1) Guardar en ScorePage un testigo de cancelación por pintado (std::shared_ptr<std::atomic_bool> o QPromise/QFuture::cancel) que `repaint()` activa antes de lanzar el siguiente y que `ScorePainter::paint` consulta entre fases (líneas, barras, cada N glifos, textos) y antes de reservar el QImage. 2) En `schedule()`, si hay un pintado en vuelo (ticket pendiente) arrancar `settle_` en vez de repintar ya, aunque `painted_` sea false: un único pintado inicial y el resto coalescido. 3) Lanzar con `QThreadPool::start(runnable, prioridad)` dando más prioridad a las páginas que cortan el viewport que a las del buffer, o usar un pool propio de pintado con maxThreadCount acotado para no solaparse con los hilos del layout.

**Nota del revisor.** Comprobado en score_page.cpp:87-93 (schedule solo usa settle_ si painted_), :119-133 (ticket; el resultado obsoleto se descarta en :123 pero la tarea se ejecuta entera y reserva su QImage) y :100/score_document.cpp:492 (el shared_ptr con alias retiene todas las paginas del layout viejo). El camino (a) es incluso peor de lo descrito: como cada fotograma con tamano distinto incrementa ticket_, TODOS los resultados anteriores se descartan y painted_ sigue en false, asi que una pagina recien creada pinta una vez por fotograma durante toda la animacion o gesto (no solo 'hasta que llega el primero') y no muestra nada hasta que el tamano se asienta. Escenario concreto: Main.qml:118-121 llama a fitPaper() en sourceChanged, que se emite (score_document.cpp:514) antes de layoutChanged (:530); al abrir otro fichero con zoom distinto, los delegados nuevos nacen en plena animacion de 180 ms: ~11 pintados por pagina, 10 tirados, y el primer pintado visible se retrasa. Matices: no hay duplicados a igual escala (la comparacion de :111 los filtra, tambien en vuelo); los delegados reutilizados tienen painted_=true y van por settle_; en (b) al teclear solo se repintan las paginas que cambian, no 3-6; todo ocurre en el pool, no en el hilo de UI. El agravante de hilos es cierto (page_layout.cpp:286-298 y system_layout.cpp:593 usan core::forEachIndex, parallel.hpp:41-44 crea std::thread por llamada, y el layout corre ademas dentro de QtConcurrent::run), pero dura 3,5-21 ms. Propuesta viable: (2) es lo mas rentable y de esfuerzo S, pero hace falta un estado 'en vuelo' que hoy no existe (ticket_ no dice si hay algo pendiente); alternativa aun mas simple: aceptar un resultado obsoleto mientras painted_ sea false y mostrarlo estirado. (1) correcto con atomic_bool o la sobrecarga de QtConcurrent::run con QPromise; (3) requiere QThreadPool::start con prioridad y que ScorePage conozca su visibilidad. Nada toca el hilo de audio.

#### app-pintado-qml/P1 · Textura de página entera: la memoria crece con zoom² x dpr² y el cacheBuffer mantiene vivas páginas enteras fuera de pantalla

- **Dónde:** `src/app/src/score_page.cpp:24`
- **Eje:** memoria · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** L · **Veredicto:** parcial

```cpp
constexpr double kMaxPixels = 32.0 * 1024 * 1024;
```

**Por qué importa.** Cada ScorePage pinta la página COMPLETA a resolución de dispositivo en un QImage ARGB32 (4 B/px, score_painter.cpp:292) y lo sube como una sola textura. A4 a 96 ppp = 794 x 1123 px lógicos. MB por página: zoom 1/dpr 1 = 3,6 MB; zoom 1/dpr 2 (o zoom 2/dpr 1) = 14,3 MB; zoom 4/dpr 1 = 3175 x 4490 = 57 MB; zoom 4/dpr 1,5 = 128 MB; zoom 4/dpr 2 topa en kMaxPixels = 134 MB (y ya se ve borroso). Si las 21 páginas estuvieran vivas serían 75 MB / 300 MB / 1,2 GB / 2,8 GB; no lo están porque el ListView virtualiza, pero ScoreView.qml:279 fija `cacheBuffer: Math.max(0, Math.round(root.pageHeight))`, que crece con el zoom: a zoom 4 siempre hay 3-4 delegados vivos (1-2 visibles + anterior y siguiente), es decir 170-230 MB de textura a dpr 1 y 400-540 MB a dpr 2 para enseñar un viewport que solo necesita ~8 MB (1920x1080) o ~33 MB (dpr 2). Además cada repintado tras asentarse el zoom vuelve a pintar esas 3-4 páginas enteras (el tiempo de 5-19 ms medido escala con los píxeles: x16 a zoom 4) y durante el relevo conviven QImage en CPU + textura vieja + textura nueva por página; la subida de 57-134 MB por textura ocurre en el hilo de render y cuesta fotogramas.

**Propuesta.** Paso corto (S): acotar el buffer a la vista, p. ej. `cacheBuffer: Math.round(Math.min(root.pageHeight, list.height))`, para que a zoom alto no haya páginas enteras precargadas. Paso de fondo (L, ya apuntado en el comentario de kMaxSide y en ADR-0015): teselas. Que ScorePage reciba el rectángulo visible (mapRectFromItem del viewport del ListView) y pinte solo teselas de p. ej. 1024x1024 px de dispositivo que lo corten más un anillo de margen, con un QSGNode hijo por tesela y un LRU con cota en MB; `ScorePainter::render` ya admite origen desplazado (painter.translate) y bastaría añadir un clip. Con eso la memoria queda acotada por el viewport (~10-40 MB) a cualquier zoom, desaparece el tope de 8192 px y el repintado tras zoom cuesta lo visible, no la página. Alternativa complementaria: textura de 8 bits (tinta como alfa) con material propio, 4x menos memoria.

**Nota del revisor.** Comprobado: score_page.cpp:23-24 (kMaxSide 8192, kMaxPixels 32 Mi), score_painter.cpp:292 (ARGB32_Premultiplied, pagina completa), ScoreView.qml:17 (maxZoom 4), :279 (cacheBuffer = pageHeight) y A4 con staffSpace 1,75 mm (score_document.cpp:58-71). Las cuentas cuadran: 3,6 MB a zoom 1/dpr 1, 57 MB a zoom 4, 128 MB a zoom 4/dpr 1,5 y tope de 134 MB. Con cacheBuffer = pageHeight a zoom alto quedan 3-4 delegados vivos (la pagina anterior y la siguiente siempre cortan el buffer). Matices que bajan el impacto: (1) esta acotado por diseno (tope por pagina + virtualizacion, ADR-0015:38-40 y :67-69 ya lo documentan como limitacion conocida); el peor caso real es ~4 x 134 MB y solo a zoom >= 2 en pantallas HiDPI; a zoom 1 son 11-15 MB. (2) El 'x16 a zoom 4' esta inflado: docs/performance.md:86-87 mide 5,4 ms a 100 %, 8,5 ms con 4x pixeles y 19 ms a 200 %, es decir, escala sublinealmente con los pixeles. (3) El pintado es en hilo de trabajo (latencia, no tirones); lo que si toca fotogramas es la subida de la textura en el hilo de render. Propuesta: el paso corto `cacheBuffer: Math.round(Math.min(root.pageHeight, list.height))` es viable y correcto (a cambio, a zoom alto las paginas vecinas apareceran en blanco con mas frecuencia porque ahi el pintado tarda mas). Teselas: viable, pero `render()` hoy no acepta origen ni clip (solo hace translate(0,-top) en :296); hay que ampliar la firma usando `paint()`. La textura de 8 bits exige material propio y revisar los knockouts, que pintan Qt::white fijo (score_painter.cpp:203). No afecta a determinismo ni al hilo de audio.

#### app-pintado-qml/P5 · Reciclado de delegados: la página reutilizada enseña la textura de otra página hasta que llega su pintado, y los ítems del pool siguen repintando

- **Dónde:** `src/app/src/score_page.cpp:67`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
index_ = index;
    emit indexChanged();
    repaint();
```

**Por qué importa.** Con `reuseItems: true` (ScoreView.qml:277) el mismo ScorePage recibe otro `index`. `setIndex` lanza el pintado asíncrono pero `painted_` sigue en true y el nodo conserva la textura anterior, así que durante los 5-19 ms (mucho más a zoom alto) el delegado muestra el contenido de OTRA página con opacidad 1; normalmente ocurre dentro del cacheBuffer y no se ve, pero arrastrando la barra de scroll o con el seguimiento del cursor a zoom alto sí. Además un ítem en el pool sigue conectado a `layoutChanged` (línea 55): cada relayout lo hace comparar y, si su página cambió, pintar y subir una textura que nadie ve. Y si su índice queda fuera del nuevo recuento, `repaint()` sale en la línea 101-103 sin soltar `shown_`, que por ser un shared_ptr con alias (score_document.cpp:492) retiene TODAS las display lists del layout anterior hasta que el ítem se destruye o se reutiliza.

**Propuesta.** En `setIndex`, invalidar lo mostrado: `shown_.reset()`, `painted_ = false` con `emit paintedChanged()` (el papel blanco y el fundido ya existentes tapan el hueco) y marcar el nodo para soltar la textura en el siguiente updatePaintNode. Atender `ListView.onPooled`/`onReused` en el delegado (o una propiedad `active` en ScorePage) para desconectar de `layoutChanged`, soltar textura y `shown_` al entrar en el pool y repintar al salir. En la salida temprana por página inexistente, hacer también `shown_.reset()`.

**Nota del revisor.** Comprobado: ScoreView.qml:277 `reuseItems: true`; setIndex (score_page.cpp:61-68) solo llama a repaint(), que es asincrono; painted_ no se resetea y el nodo conserva la textura anterior (updatePaintNode :136-152 solo la cambia cuando llega image_), asi que el delegado reutilizado muestra otra pagina a opacidad 1 hasta que llega el pintado (ScoreView.qml:331). La conexion a layoutChanged (:55) no se corta al entrar en el pool: no hay manejadores pooled/reused. Correcciones: (1) la retencion de shown_ por indice fuera de rango (:101-103) hoy es inalcanzable en la practica: si cambia el recuento, el modelo entero se reemplaza y los delegados se destruyen (P3); solo importara si se adopta P3. (2) La vida de un item en el pool es corta (Qt lo drena), asi que los repintados 'que nadie ve' son marginales; no verificable sin fuentes de Qt. Impacto bajo correcto: es sobre todo un defecto visual de 1-2 fotogramas, mayor a zoom alto. Propuesta viable (esfuerzo S), con dos avisos: poner painted_=false en setIndex mete a la pagina en el camino de pintado inmediato por fotograma de P2(a) si coincide con un zoom, y cada pagina reutilizada pasara por papel en blanco + fundido de 160 ms, lo que durante un scroll rapido puede resultar mas llamativo que el fallo actual; conviene ocultarla sin animacion.

#### app-pintado-qml/P6 · Cursor de reproducción: QVariantMap nuevo, búsqueda lineal y cinco bindings más un handler JS por tick de un QTimer de 16 ms que no va sincronizado con el vsync

- **Dónde:** `src/app/qml/ScoreView.qml:27`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
readonly property var cursor: cursorSeconds >= 0 && document.pageCount > 0
                                  ? document.cursorAt(cursorSeconds) : ({})
```

**Por qué importa.** `audio.position` cambia cada 16 ms (audio_controller.cpp:16, `transportTimer_.setInterval(16ms)`). En cada tick: `ScoreDocument::cursorAt` construye un QVariantMap con 4 claves QString (score_document.cpp:382) que se convierte a objeto JS; `notation::cursorAt` recorre linealmente las MeasureBox de todas las páginas hasta dar con el compás (src/notation/src/cursor.cpp:35-37, hasta 500 comparaciones al final de la canción); se reevalúan `cursorShown`, x/y/height del playhead, se ejecuta `onCursorChanged` (líneas 220-239) y además el reloj `window.clock(audio.position)` y el Slider de Main.qml (632, 654). En CPU son decenas de µs por tick, poco; lo que se nota es otra cosa: un QTimer de 16 ms (62,5 Hz) no está en fase con la pantalla, así que a 60 Hz hay fotogramas repetidos o saltados (el cursor tiembla) y en pantallas de 120/144 Hz el cursor avanza a la mitad de la cadencia. También genera basura para el GC de QML a 60 objetos/s.

**Propuesta.** Mover el muestreo al fotograma: un `FrameAnimation { running: audio.playingSong; onTriggered: ... }` (Qt 6.4+) en ScoreView que lea la posición del reproductor una vez por frame, o interpolar entre ticks. Exponer el cursor como propiedades tipadas de C++ (cursorPage: int, cursorRect: QRectF con su señal, emitida solo si cambia) en lugar de un `var` con mapa, y recordar en ScoreDocument el último (página, MeasureBox) encontrado para comprobarlo primero (O(1) salvo al cambiar de compás; índice compás->página construido en `apply()`). El reloj puede enlazarse a `Math.floor(audio.position)` para evaluarse una vez por segundo.

**Nota del revisor.** Comprobado: audio_controller.cpp:16-17 (QTimer de 16 ms, PreciseTimer), :149-159 (emite positionChanged solo si cambia), Main.qml:447/632/654, ScoreView.qml:27-28 y 220-239, score_document.cpp:367-386 (QVariantMap de 4 claves por llamada), cursor.cpp:35-37 (busqueda lineal). El coste de CPU es despreciable y el hallazgo lo reconoce; la parte de la busqueda lineal sobra: docs/performance.md:101 mide 0,37 µs una busqueda equivalente en las 21 paginas ('nothing to gain'), y playedMeasureAt ya es binaria (sequence.cpp:735), asi que el cache O(1) y el indice compas->pagina no merecen la pena. Lo unico con valor es el desfase 62,5 Hz frente al vsync. Matiz importante para la propuesta: Player::position() (player.cpp:146-150) lee un atomico de frames que el hilo de audio actualiza por bloque, asi que muestrear por fotograma con FrameAnimation sin interpolar seguiria avanzando a saltos del tamano del buffer de audio; hace falta extrapolar desde (ultima posicion, marca de tiempo). Leer ese atomico desde la UI es seguro y no bloquea ni asigna en el hilo de audio; hay que mantener la llamada a player_->tidy() en el hilo de UI (pollTransport). FrameAnimation esta disponible (el proyecto exige Qt 6.12, src/app/CMakeLists.txt:3); comprobar que el smoke test con QSG_RENDER_LOOP=basic sigue pasando.

#### app-pintado-qml/P7 · La textura de la página se crea con canal alfa aunque es opaca: mezcla a página completa y tres capas de sobrepintado por página

- **Dónde:** `src/app/src/score_painter.cpp:292`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
```

**Por qué importa.** La imagen se rellena de blanco opaco, pero al tener formato con alfa `window()->createTextureFromImage(image_)` (score_page.cpp:145, sin opciones) produce una textura marcada con alfa: el scene graph la dibuja con blending y fuera del lote opaco, encima del Rectangle `paper` blanco del mismo tamaño (ScoreView.qml:318-325) y, en tema oscuro, debajo de otro Rectangle translúcido a página completa (342-347). Son 2-3 pasadas de fill-rate por píxel de página en cada fotograma mientras hay animación (cursor en reproducción, scroll, zoom). Irrelevante en una GPU dedicada, apreciable en gráficas integradas con ventana 4K y zoom alto.

**Propuesta.** Pintar en `QImage::Format_RGB32` (mismo tamaño, el rasterizador de Qt es igual o más rápido sin alfa) o pasar `QQuickWindow::TextureIsOpaque` al crear la textura, para que el nodo vaya al lote opaco y el papel de debajo quede descartado por profundidad. Para el tema oscuro, llevar el tinte al propio nodo (color del material o un QSGNode con el factor) en vez de un Rectangle translúcido encima; el `paper` blanco puede quedarse solo como marcador mientras `painted` es false.

**Nota del revisor.** Comprobado: score_painter.cpp:292-293 (ARGB32_Premultiplied rellenada de blanco opaco), score_page.cpp:145 (`createTextureFromImage(image_)` sin opciones) y la pila por pagina en ScoreView.qml:309-347 (RectangularShadow, Rectangle paper con radius, ScorePage y, en tema oscuro, Rectangle translucido). `QQuickWindow::TextureIsOpaque` existe en el Qt instalado (qquickwindow.h:65); que sin esa opcion la textura herede el alfa del formato y se dibuje con blending es comportamiento conocido de Qt, no verificable aqui por falta de fuentes. Matices: el sobrepintado esta acotado por el viewport (el ListView recorta con clip:true, :276), no crece con el zoom, asi que 'apreciable a zoom alto' esta inflado; solo cuenta cuando la escena se redibuja (reproduccion, scroll, zoom). A favor: una pagina opaca tambien descartaria por profundidad los fragmentos de la sombra por shader que quedan debajo, no solo el papel. La propuesta de TextureIsOpaque (o Format_RGB32) es de una linea y correcta: todo se pinta source-over sobre blanco opaco, el alfa queda en 255, y durante el fundido de entrada Qt sigue mezclando por la opacidad del item. Llevar el tinte oscuro al nodo NO es esfuerzo S: QSGImageNode no modula color (haria falta material propio), o bien pintar con color de papel segun tema, lo que obliga a cambiar el Qt::white fijo de los knockouts (score_painter.cpp:203) y a repintar al cambiar de tema.

#### app-pintado-qml/P3 · El modelo del ListView es un entero: cuando cambia el número de páginas se destruyen todos los delegados y sus texturas

- **Dónde:** `src/app/qml/ScoreView.qml:272`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
model: root.document.pageCount
```

**Por qué importa.** Con un modelo numérico, un valor distinto es un modelo nuevo: QQuickItemView::setModel vacía la vista (clear + posición al inicio) y QQmlDelegateModel quita todas las filas e inserta las nuevas. Una edición que hace pasar la canción de 21 a 22 páginas (o al revés) anula justo la optimización de ScorePage de conservar la textura de las páginas que no cambian (`*page == *shown_`, score_page.cpp:111): se liberan todas las texturas vivas, se recrean los delegados, se repintan todas las páginas visibles y del buffer (3-6 pintados de 5-19 ms cada uno en vez de 1) y cada una vuelve a aparecer desde el papel en blanco con el fundido de 160 ms, es decir un parpadeo perceptible; la posición de scroll depende de que `revealCaret` o el seguimiento del cursor la recuperen. No lo he ejecutado: el salto de scroll conviene confirmarlo en la app, la destrucción de delegados se deduce del comportamiento documentado de Qt con modelos enteros. Cuando el recuento no cambia no pasa nada (el binding se reevalúa con layoutChanged pero el valor es igual).

**Propuesta.** Exponer en ScoreDocument un QAbstractListModel mínimo (rowCount = páginas) que en `apply()` emita beginInsertRows/beginRemoveRows solo por la cola cuando cambia el recuento, y usarlo como `model`. Los delegados existentes sobreviven, ScorePage compara y conserva su textura, y solo se crea o destruye la página nueva. Como beneficio lateral, `pageCount` puede tener su propia señal en vez de colgar de `layoutChanged`.

**Nota del revisor.** El codigo existe: ScoreView.qml:272 `model: root.document.pageCount`, y pageCount notifica con layoutChanged (score_document.hpp:61). Que un entero distinto equivale a un modelo nuevo (QQuickItemView::setModel hace clear() y recoloca al inicio) es comportamiento conocido de Qt, pero no he podido verificarlo aqui: la instalacion local de Qt 6.12.1 no trae fuentes y el autor tampoco lo ejecuto. Impacto inflado como hallazgo de rendimiento: solo ocurre cuando una edicion cambia el numero de paginas (poco frecuente: desbordar o vaciar la ultima pagina, insertar o quitar compases en el limite); el coste son 3-6 pintados asincronos de 5-19 ms mas recrear unos pocos delegados, similar a un asentamiento de zoom. Al abrir fichero o cambiar de pista o vista el repintado completo es legitimo. Lo relevante es el efecto visible (fundido desde blanco y posible salto de scroll al principio, que revealCaret corrige con una animacion de 160 ms): hay que confirmarlo en la app antes de invertir esfuerzo M. Propuesta correcta y viable; cuidar el orden en apply() (begin/endInsertRows o RemoveRows alrededor de `pages_ = layout.pages`, score_document.cpp:526) y usar reset del modelo al abrir otra cancion. Ojo: al adoptarla se vuelve alcanzable la fuga de shown_ por indice fuera de rango descrita en P5.

#### app-pintado-qml/P4 · Cada pintado de página vuelve a crear las fuentes desde sus bytes y a extraer todos los contornos: la caché de outlines vive solo dentro de una llamada

- **Dónde:** `src/app/src/score_painter.cpp:130`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
Outlines outlines(QRawFont(musicFont, kStaffSpacesPerEm * scale, QFont::PreferNoHinting));
```

**Por qué importa.** `Outlines` es una variable local de `paintGlyphs`, y en `paintTexts` (líneas 158-160) pasa lo mismo con `const QRawFont upright = textFont(fonts.text, false);` y el `std::map` de caras. Por cada render se instancia Bravura (OTF/CFF de cientos de KB) y Arimo desde el QByteArray, se resuelve `glyphIndexesForString` y se llama a `pathForGlyph` para cada glifo distinto de la página (decenas de símbolos más el alfabeto usado por cada tamaño de texto). Ese coste fijo se paga por página y por repintado: 3-6 veces al abrir, en cada asentamiento de zoom y en cada tecla que cambia una página, y pesa más cuanto más pequeña es la página pintada. No está medido; por orden de magnitud son cientos de µs a algún ms sobre un pintado de 5-19 ms, y es el mismo trabajo repetido. Lo bueno del diseño actual es que no hay mutex ni contención entre hilos de pintado, y eso hay que conservarlo.

**Propuesta.** Caché persistente y acotada sin locks en el camino caliente: como se pide PreferNoHinting los contornos escalan linealmente, así que basta guardar cada glifo una vez a un em de referencia (p. ej. 1000 px) en una tabla `thread_local` por hilo del pool (o una tabla compartida inmutable publicada con std::shared_ptr atómico) con clave (fuente, índice de glifo), y aplicar escala + traslación al rellenar con el QTransform que ya se usa en la línea 84-87. El tamaño queda acotado por el número de glifos distintos (unos cientos de paths, <1 MB) y NO crece con el zoom, a diferencia de una caché por tamaño de píxel. Guardar también el QRawFont de referencia y la tabla codepoint->índice junto a la caché. Medirlo con el benchmark de pintado antes de darlo por bueno.

**Nota del revisor.** Comprobado: `Outlines` es local a paintGlyphs (score_painter.cpp:130) y en paintTexts (:158-173) se crea QRawFont desde bytes y un std::map de caras por llamada; ScoreDocument::painter() (score_document.cpp:495-498) solo guarda los QByteArray. Es cierto que cada render vuelve a instanciar las fuentes y a extraer contornos. Pero el coste no esta medido (lo admite el propio hallazgo) y esta incluido en los 5,4-19 ms ya medidos (docs/performance.md:86-88), que corren en hilos de trabajo ('latency, not stutter'). Sin medicion no se sostiene 'medio'. Reparos a la propuesta: (1) escalar desde un em de referencia no da pixeles identicos a los actuales; hay que validar por plataforma tambien los avances con kerning (`advancesForGlyphIndexes`, :185-186), de los que depende el centrado y el ancho de los knockouts. (2) `thread_local` en hilos del QThreadPool global rinde poco: los hilos expiran a los 30 s de inactividad y cada uno calienta su propia tabla; ademas un QRawFont en thread_local o static se destruiria al terminar el hilo, posiblemente despues del desmontaje de QGuiApplication. Mejor guardar solo datos puros (QPainterPath, indices, avances). (3) Una tabla compartida entre hilos de QPainterPath solo es segura si nunca se llaman sobre ella metodos const que cachean (boundingRect, etc.); el uso actual (`translated`, `QTransform::map`) copia antes y es seguro. No afecta al determinismo del nucleo ni al hilo de audio. Recomendacion: medir primero con el benchmark [app].

**Observaciones del área (qué está bien resuelto).**

Auditoría de solo lectura (no se ha compilado ni ejecutado nada); los órdenes de magnitud de P4, P6 y P7 son estimaciones a confirmar con medición, y en P3 el salto de scroll conviene comprobarlo en la app.
Leído: src/app/src/score_page.cpp/.hpp, score_painter.cpp/.hpp, ScoreView.qml y Elevation.qml enteros; Main.qml, TunerPane.qml, score_document.cpp, audio_controller.cpp, main.cpp, src/notation/src/cursor.cpp y src/core/include/elara/core/parallel.hpp por secciones. El resto de QML (SongPane, EntryBar, IconButton, SegmentedControl, WelcomePane, Logo, Icon...) solo por búsqueda de patrones; src/app/CMakeLists.txt y qtquickcontrols2.conf no los he abierto.
Bien resuelto, no tocar:
- ScorePage NO es QQuickPaintedItem: es un QQuickItem con QSGImageNode cuya textura se pinta en un hilo de trabajo; el hilo de UI solo compara y entrega el QImage (que se suelta tras crear la textura, score_page.cpp:146).
- El ListView virtualiza: solo viven las páginas visibles más un alto de página de buffer, así que a zoom <= 1 la memoria de texturas es de pocas decenas de MB aunque la canción tenga 21 páginas. Las páginas fuera de ese rango sí se liberan.
- Un relayout NO repinta todas las páginas: `*page == *shown_` (score_page.cpp:111) conserva la textura de las que no cambian, y además solo se evalúan los delegados vivos.
- El zoom estira la textura y repinta una sola vez a los 120 ms de asentarse el tamaño (salvo el caso de P2).
- No hay `layer.enabled`, MultiEffect ni ShaderEffect en src/app/qml: las sombras (Elevation.qml y la del papel) son RectangularShadow analíticas, sin texturas intermedias.
- Caret y cursor de reproducción son ítems QML sobre la textura: moverlos no repinta la página.
- El pintado no comparte estado mutable entre hilos (objetos de fuente por llamada, sin mutex): cero contención; la mejora de P4 debe mantenerlo.
- La búsqueda del compás sonando es binaria (Sequence::playedMeasureAt).
- La animación infinita del punto del afinador solo corre con el afinador visible y escuchando.
Fuera de mi área, visto de paso y para quien audite score_document: `ScoreDocument::edited()` (src/app/src/score_document.cpp:877-880) copia la canción (`std::make_shared<const core::Song>(document_->song())`) y construye un `audio::Sequence` nuevo en el hilo de UI en cada edición; con los tiempos medidos de la canción grande son unos 10,7 + 28 ms por tecla antes de lanzar el relayout. Y `relayout()` (línea 472) tampoco cancela layouts obsoletos, solo descarta su resultado por `generation`.

### Notación (layout)

#### notation/N4 · Vectores anidados por beat y por columna que se liberan y vuelven a pedir en cada compás

- **Dónde:** `src/notation/src/standard_staff.cpp:484`
- **Eje:** memoria · **Impacto:** medio · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
chord.heads.push_back(headOf(note, beat.duration.value()));
```

**Por qué importa.** Chord tiene `std::vector<Head> heads` (línea 235) y Column `std::vector<std::size_t> chords` (línea 261). `chords_.clear()` y `columns_.clear()` (líneas 449-450) destruyen esos vectores internos, así que la capacidad de chords_ se conserva pero cada beat y cada columna vuelven a asignar en cada compás, y dos veces por layout (medición y dibujo). Lo mismo ocurre en tablatura con TabColumn::notes y ::beats (tab_staff.cpp:67-68, `column.beats.push_back(&beat);` en la 145) y con beamGroups(), que devuelve un vector<vector<size_t>> por voz y copia cada grupo (`groups.push_back(group);`, layout_common.cpp:662). Además `std::vector<std::size_t> order` se crea y ordena dos veces por compás con la misma clave (líneas 511 y 586). Estimo 3-5 asignaciones por beat y pasada: varios cientos de miles por layout de la canción grande.

**Propuesta.** Almacenamiento plano por writer: `std::vector<Head> heads_` miembro y Chord guarda {primero, cuenta} (std::span al usarlo). Las columnas son tramos consecutivos de `order` ya ordenado: guardar order_ como miembro (calculado una vez y compartido con accidentals()) y Column pasa a {primero, cuenta}. Igual en TabWriter con notes_/beats_ planos. beamGroups escribe en un buffer plano del llamante (índices más offsets) en vez de devolver vector<vector>.

**Nota del revisor.** Todo comprobado: Chord::heads (src/notation/src/standard_staff.cpp:235) y Column::chords (261) son vectores internos que chords_.clear()/columns_.clear() (449-450) destruyen en cada compás. chord.heads crece con push_back sin reserve (484), así que un acorde de varias notas realoja varias veces. En tablatura, TabColumn::notes y ::beats (src/notation/src/tab_staff.cpp:67-68, push_back en 145 y 149) hacen lo mismo. beamGroups devuelve vector<vector<size_t>> y copia cada grupo con groups.push_back(group) (src/notation/src/layout_common.cpp:662). 'order' se construye y ordena dos veces (511-517 y 586-590). Todo ocurre en medición y en dibujo. Matices: las dos ordenaciones no son idénticas, porque la de 511 excluye los silencios de compás y accidentals() retorna antes en percusión, así que compartir order_ exige calcularlo fuera de accidentals(). La categoría real es velocidad (asignaciones temporales), no memoria residente. El número de asignaciones es una estimación coherente con el código pero sin medir, y el beneficio es una parte de un layout de 3,5-21 ms fuera del hilo de UI. La propuesta es viable y no afecta al determinismo ni a los hilos (writers por job). Paso previo casi gratis: chord.heads.reserve(beat.notes.size()) y groups.push_back(std::move(group)). El almacenamiento plano toca mucho código de StandardWriter y TabWriter (esfuerzo M real).

#### notation/N1 · layoutPages vuelve a medir y maquetar todo el documento en cada edición: no hay caché por compás ni por sistema

- **Dónde:** `src/notation/src/page_layout.cpp:289`
- **Eje:** velocidad · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** L · **Veredicto:** parcial

```cpp
const std::vector<MeasureWidth> widths =
        detail::measureWidths(song, staves, font, first, last, threads);
```

**Por qué importa.** La app llama a notation::layoutPages(*song, staves, *font, pageOptions()) con la canción entera en cada relayout (src/app/src/score_document.cpp:135). Cambiar una nota cuesta lo mismo que abrir el fichero: 3,5 ms en todos los núcleos (9,4 ms de CPU) con 1 pista y 21 ms (132 ms de CPU) con 20. Además cada compás se lee dos veces por layout: collect() en measureRange (system_layout.cpp:539) y otra vez en layoutLine (system_layout.cpp:658), con springsOf recalculado (línea 662). El ancho de un compás es independiente del contexto: measureRange llama a header(m)/changes(m)/collect(m)/barline(m) y clearItems() por compás, y solo depende del compás m y de las cabeceras m-1 y m. Con caché, una edición tocaría 1 compás medido, el DP de saltos sobre doubles y 1-3 sistemas: estimo menos de 0,5 ms en un solo hilo (estimación, no medido).

**Propuesta.** 1) Caché de MeasureWidth por compás, con clave (selección de staves, índice de compás, revisión del compás y de las cabeceras m-1/m), mantenida fuera de layoutPages (objeto LayoutCache que se pasa por parámetro). Los comandos de edición invalidan los compases que tocan; insertar o borrar compases desplaza índices. 2) Reejecutar siempre LineBreaker (barato: vectores natural_/tightest_/ideal_ ya precalculados). 3) Caché de DisplayList por sistema sin desplazar, con clave (first, last, stretch, numbered, named) más la revisión de sus compases y de los vecinos first-1 y last (tiesOut lee el compás siguiente; dynamicBefore mira hacia atrás); solo se llama a layoutLine para los sistemas cuya clave cambia. 4) El apilado en páginas se rehace entero porque es O(sistemas).

**Nota del revisor.** Código confirmado: layoutPages (src/notation/src/page_layout.cpp:289-304) mide todos los compases y maqueta todos los sistemas en cada llamada; no hay caché. La app lo llama con la canción entera (src/app/src/score_document.cpp:135) y cada compás pasa dos veces por collect() y springsOf (system_layout.cpp:539/551 y 658/662). Impacto inflado: el layout corre en un hilo de QtConcurrent (score_document.cpp:472), no en el de UI, y cuesta 3,5-21 ms. En cada edición, edited() (score_document.cpp:877-880) copia la canción y la secuencia en el hilo de UI (10,7 + 28 ms medidos), que es el coste dominante. El '<0,5 ms' con caché es una estimación sin medir. La propuesta no es correcta tal como está: (a) no existe ninguna 'revisión' por compás en core (grep sin resultados) y la app copia el Song entero por valor en cada edición, así que hace falta infraestructura nueva de invalidación; (b) la clave de sistema con vecinos first-1 y last es insuficiente: dynamicBefore (layout_common.cpp:209) retrocede un número arbitrario de compases hasta encontrar notas atacadas, así que editar un compás lejano puede cambiar las dinámicas de un sistema cuya clave no cambia; (c) el ancho y el dibujo dependen también de song.measureCount() (barline final, system_layout.cpp:543 y 679) y de propiedades de pista que la clave no recoge (clave, afinación, writtenOffset, nombre); (d) la caché se mutaría desde el hilo de trabajo, y relayout() puede tener varios layouts en vuelo, por lo que necesita propiedad exclusiva o sincronización. Viable pero con riesgo alto de resultados obsoletos; esfuerzo L como mínimo.

#### notation/N2 · Dos tandas de std::thread creadas y destruidas por layout: no hay pool y el umbral de paralelismo es muy bajo

- **Dónde:** `src/notation/src/page_layout.cpp:298`
- **Eje:** concurrencia · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
core::forEachIndex(0, lines.size(),
                       std::min<unsigned>(threads, static_cast<unsigned>(lines.size())),
```

**Por qué importa.** forEachIndex crea los hilos en cada llamada (src/core/include/elara/core/parallel.hpp:44 `workers.emplace_back([&, t] {`) y hace join al terminar. layoutPages lo llama dos veces: measureWidths (system_layout.cpp:593) y los sistemas (page_layout.cpp:298). Son hasta 2 x hardware_concurrency hilos creados por layout; crear y unir un hilo en Windows cuesta decenas de microsegundos, comparable al trabajo de un sistema de 3-4 compases. Encaja con la escala medida: 9,4 ms en un hilo frente a 3,5 ms en todos (2,7x). Con kParallelMeasures = 48 (page_layout.cpp:31), una canción de 60 compases ya lanza ~15 hilos para ~15 sistemas minúsculos. El layout ya corre en un hilo de QtConcurrent (score_document.cpp:472) y el pintado de páginas usa ese mismo pool (score_page.cpp:120), así que los hilos del layout se suman a los de Qt: sobre-suscripción.

**Propuesta.** Pool persistente en core/parallel.hpp: singleton perezoso con hardware_concurrency-1 workers, en el que el hilo llamante también ejecuta trabajos; forEachIndex encola en él, conservando el reparto por índice atómico y el orden de las excepciones. En notation: una sola región paralela por layout cuando sea posible, grano mínimo por tarea (agrupar sistemas hasta ~16-32 compases por job, como kMeasuresPerJob) y umbral de paralelismo basado en trabajo real (compases x staves) en lugar de 48 compases.

**Nota del revisor.** Hechos confirmados: forEachIndex crea y une std::thread en cada llamada (src/core/include/elara/core/parallel.hpp:41-56) y layoutPages lo invoca dos veces (system_layout.cpp:593 y page_layout.cpp:298). kParallelMeasures = 48 (page_layout.cpp:31), así que 60 compases lanzan min(núcleos, nº de sistemas) hilos para los sistemas; measureWidths solo lanza min(núcleos, jobs de 32 compases), o sea 2 en ese ejemplo, de modo que '2 x hardware_concurrency' solo vale para canciones largas. El layout corre en el pool de Qt (score_document.cpp:472), igual que el pintado (score_page.cpp:120). Impacto inflado: el coste de crear hilos no está medido, y la cifra '9,4 ms en un hilo' contradice a N1, que da 9,4 ms como CPU total con todos los núcleos, así que el 2,7x no demuestra nada sobre la creación de hilos. En el caso de 21 ms son como mucho ~1-2 ms, fuera del hilo de UI. Dato a favor que el hallazgo no menciona: forEachIndex también lo usan audio::Sequence (src/audio/src/sequence.cpp:469, 638, 698) y core/performance.cpp:118/134, y Sequence se construye en el hilo de UI en cada edición, así que un pool ayudaría ahí. Riesgos de la propuesta: el singleton lo compartirían el hilo de UI (secuenciación) y el worker de layout, con lo que la UI puede esperar tras trabajos de layout salvo que el llamante ejecute trabajos; hay que soportar reentrada sin interbloqueo, redefinir el 'orden de hilo' de las excepciones como orden de índice y cuidar la destrucción estática. No afecta al determinismo (cada job escribe en su hueco) ni al hilo de audio. Antes del pool hay un arreglo S: limitar los hilos por trabajo real (p. ej. compases/32) y subir el umbral.

#### notation/N8 · stretchFor hace 60 bisecciones por sistema apretado, en serie, sobre una función lineal a trozos

- **Dónde:** `src/notation/src/page_layout.cpp:185`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
for (int i = 0; i < kBisections; ++i) {
            const double middle = (low + high) / 2;
            if (lineWidth(widths_, line.first, line.last, middle) > sums.room) {
```

**Por qué importa.** best() llama a stretchFor para cada línea en el hilo llamante (línea 113). Cada sistema que hay que apretar evalúa lineWidth 60 veces, y cada evaluación recorre todos los springs de sus compases: unas 60 x 4 compases x ~10 springs = 2-3 mil springLength por sistema. Con ~150 sistemas por pista y la mitad apretados son del orden de 200k evaluaciones, unas décimas de ms en serie de los 3,5 ms del layout de 1 pista (estimación). 60 iteraciones sobre un intervalo de 0,2 superan además la precisión de un double (~52 bits).

**Propuesta.** Resolver exacto: el ancho es monótono y lineal a trozos en stretch, con un punto de quiebre por spring en s_i = ink_i/ideal_i - 1. Recoger los quiebres dentro de [-kMaxShrink, 0], ordenarlos y recorrerlos acumulando la pendiente (suma de ideal de los springs aún no saturados) hasta alcanzar room: O(k log k) y sin iterar. Alternativa mínima: parar cuando high - low < 1e-9 (~28 iteraciones). Ambas cambian los últimos bits del resultado: revisar los tests de referencia.

**Nota del revisor.** Confirmado: stretchFor (src/notation/src/page_layout.cpp:169-194) hace kBisections = 60 iteraciones (línea 27) solo para sistemas que hay que apretar (natural > room y tightest < room), y cada una llama a lineWidth, que recorre los springs de los compases de la línea (MeasureWidth::width, src/notation/src/layout_common.hpp:465-471). Se ejecuta en serie desde best() (línea 113) o every() (123). Con kMaxShrink = 0,2 (layout_common.hpp:427) las últimas iteraciones ya no mueven el punto medio, así que sobran sin ser incorrectas. El coste es del orden de décimas de ms como mucho y fuera del hilo de UI; impacto bajo correcto. Sobre la propuesta: la solución exacta es válida (ancho monótono y lineal a trozos, quiebre en ink/ideal - 1), pero debe conservar el invariante actual de que el resultado nunca supera room (hoy se devuelve 'low', que siempre cumple lineWidth <= room) y tratar los springs con ideal = 0. Cualquiera de las dos variantes cambia los últimos bits de stretch y por tanto todas las coordenadas del sistema: habrá que regenerar las salidas de referencia. Sigue siendo determinista.

#### notation/N9 · toSvg crea una std::string por número y otra por elemento, sin reserve del resultado

- **Dónde:** `src/notation/src/svg.cpp:16`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
std::string text = std::format("{:.3f}", value);
```

**Por qué importa.** Cada <line> llama a number() 5 veces (6 con trazo discontinuo) (línea 131), cada <polygon> de barra 8, cada <text> 2-3; cada una pasa por std::format con análisis de la cadena de formato y devuelve una std::string. Luego otro std::format construye el elemento en una cadena temporal que supera el SSO (asignación en heap) y se concatena con `svg +=`. Como svg no tiene reserve, una página de ~5k items (~0,5 MB de texto) se realoja ~20 veces. Solo lo usa la CLI (src/cli/src/render_commands.cpp:406 `file << notation::toSvg(pages[p], svg);`), no la UI: afecta a la exportación de muchas páginas, no a la latencia interactiva.

**Propuesta.** svg.reserve(256 + 110 * (lines + beams + glyphs + texts + curves + polygons)). Sustituir number() por `void appendNumber(std::string& out, double v)` con std::to_chars(buf, buf + 32, v, std::chars_format::fixed, 3) sobre un buffer de pila y recorte de ceros en sitio. Escribir cada elemento con std::format_to(std::back_inserter(svg), ...) o con append directos, sin cadenas intermedias; point() y escapeAttribute() pasan a escribir en la salida. Estimo 3-5x en exportación SVG (no medido).

**Nota del revisor.** Confirmado: number() crea una std::string con std::format por cada valor (src/notation/src/svg.cpp:15-24), y cada elemento se monta con otro std::format temporal antes de 'svg +=' (127-185): 5 llamadas por <line> (6 con dash), 8 por barra, 2-3 por texto. toSvg no hace reserve. Su único llamante es la CLI (src/cli/src/render_commands.cpp:406); no interviene en la UI ni en el audio, así que impacto bajo es correcto. El '3-5x' no está medido. La propuesta es viable: std::to_chars con chars_format::fixed y precisión 3 redondea igual que std::format("{:.3f}"), de modo que la salida no cambia si se conserva el recorte de ceros y el caso '-0' (incluido '-0.000' procedente de valores como -0,0004). Dos cuidados: un buffer de 32 bytes no basta para un double enorme en formato fijo, así que hay que comprobar el ec de to_chars y tener un camino alternativo; y si se compila para macOS con libc++, to_chars de coma flotante exige un deployment target reciente.

#### notation/N3 · Tres std::vector temporales por acorde en el camino caliente de StandardWriter

- **Dónde:** `src/notation/src/standard_staff.cpp:496`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
chords_[c].stemUp = stemUpFor({c});
```

**Por qué importa.** stemUpFor recibe `const std::vector<std::size_t>&` (línea 641), así que `{c}` construye un vector en el heap por cada acorde sin barra. placeAccidentals crea `std::vector<std::size_t> columnOf(chord.heads.size(), 0);` (línea 752) por acorde aunque no tenga alteraciones (el return temprano está después, en la 773). draw() crea `std::vector<double> dotYs;` (línea 803) y hace push_back por cabeza aunque duration.dots() sea 0 y dots() no haga nada. read() se ejecuta dos veces por compás (medición y dibujo), de modo que las dos primeras se duplican. En la canción de referencia (80k beats) son del orden de 300-400k pares malloc/free por layout completo, repartidos entre todos los núcleos sobre el mismo heap del proceso, lo que añade contención y limita el escalado.

**Propuesta.** stemUpFor(std::span<const std::size_t>) y llamar con `std::span(&c, 1)`. En placeAccidentals, salir antes si ningún Head tiene accidental y convertir columns/columnOf/rightEdge en vectores miembro reutilizados (clear() conserva la capacidad). En draw(Chord&), rellenar dotYs solo si chord.duration.dots() > 0 y usar un vector miembro de trabajo.

**Nota del revisor.** Las tres asignaciones existen: stemUpFor({c}) construye un std::vector porque la firma es const std::vector<std::size_t>& (src/notation/src/standard_staff.cpp:496 y 641); columnOf(chord.heads.size(), 0) se crea antes del return temprano (752 frente a 773); dotYs se rellena siempre (803-822) aunque dots() retorna si count == 0 (853-856). read() se ejecuta en las dos pasadas porque collect() llama a read() (1229-1230). Matices: stemUpFor({c}) solo afecta a acordes sin barra; columnOf y dotYs no afectan a silencios (shape() y draw() retornan antes); dotYs solo ocurre en la pasada de dibujo. Los 300-400k pares son por tanto una cota superior, no una medida. La afirmación de contención en el heap que limita el escalado no está respaldada por ningún dato. Ganancia acotada: una fracción de un layout de 3,5-21 ms que corre fuera del hilo de UI. El hallazgo omite un cuarto temporal igual: dots(x + chord.headRight, {y - 0.5}, ...) en la línea 800 crea un vector por cada silencio. La propuesta es correcta y segura: cada job crea sus propios writers con makeWriters, así que los vectores miembro no se comparten entre hilos, y std::span(&c, 1) es válido. Merece hacerse por ser S, pero con impacto bajo.

#### notation/N5 · El montaje de páginas es serie: desplaza y copia todos los items del documento en el hilo llamante y sin reserve

- **Dónde:** `src/notation/src/page_layout.cpp:327`
- **Eje:** concurrencia · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
detail::shift(system, options.margin, placed[k].y + extra * static_cast<double>(k));
            detail::append(page, std::move(system));
```

**Por qué importa.** Tras la región paralela, flush() recorre en un solo hilo todos los items de todos los sistemas: shift() reescribe cada punto y append() los copia a la página con `into.insert(into.end(), std::make_move_iterator(...` (layout_common.cpp:164) sin reservar. Con ~8-10 sistemas por página A4 los vectores de la página crecen geométricamente: cada item se mueve ~2 veces de media y la capacidad sobrante (hasta ~50% con el crecimiento 1,5x de MSVC) queda viva mientras viva el documento, porque las páginas se guardan en pages_. Cada item ya fue desplazado por staff en layoutLine (system_layout.cpp:711) y otra vez por la sangría (línea 750). Para 20 pistas son decenas de MB de items recorridos en serie: estimo 3-6 ms de los 21 ms, y es la parte que no escala con núcleos (estimación, no medido).

**Propuesta.** Separar colocación de montaje: el bucle actual solo calcula, por página, la lista Placed{system, y} (O(sistemas), serie). Después, core::forEachIndex sobre páginas: cada página suma los tamaños de sus sistemas, hace reserve exacto de glyphs/lines/beams/texts/curves/polygons/measures/columns y aplica shift+append. Créditos y números de página se añaden al final como ahora. Opcional: que layoutLine reciba el desplazamiento vertical de cada staff y lo aplique al emitir, ahorrando una pasada de shift.

**Nota del revisor.** Confirmado que flush() (src/notation/src/page_layout.cpp:318-332) aplica shift() y append() en el hilo llamante después de la región paralela, y que append inserta sin reserve (src/notation/src/layout_common.cpp:164); el primer sistema de cada página se mueve sin copia (160-162). Imprecisiones: el shift por sangría (system_layout.cpp:750) solo ocurre en el primer sistema del documento (options.named = s == 0), no en cada sistema; el shift por staff (711) no hace nada para el primer staff (dy = 0) y ambos están ya dentro de la región paralela. Los '3-6 ms de 21' son una estimación sin medir; en el caso de 1 pista (3,5 ms) es despreciable y todo ocurre fuera del hilo de UI. Sí es real la capacidad sobrante retenida en pages_. Errores en la propuesta: (1) los créditos de título no se añaden 'al final como ahora': addCredits se ejecuta antes de colocar los sistemas (línea 310) y su resultado 'top' condiciona la colocación; solo los números de página y el copyright van al final (357-370). Moverlos cambiaría el orden de los textos de la página 1 y con ello la igualdad de DisplayList que usa la app para no repintar (src/app/src/score_page.cpp:111) y las salidas de referencia. (2) Con reserve exacto, el atajo 'if (into.empty()) into = std::move(items)' de append descartaría la capacidad reservada, así que hay que cambiar append. (3) Sería una tercera tanda de hilos por layout mientras no exista pool (N2). Lo barato y seguro es solo el reserve exacto por página (S); paralelizar, únicamente si una medición lo justifica.

#### notation/N6 · Un layout obsoleto no se puede cancelar: layoutPages no acepta señal de parada y cada relayout lanza otro con todos los núcleos

- **Dónde:** `src/notation/include/elara/notation/page_layout.hpp:30`
- **Eje:** concurrencia · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
unsigned threads = 0; ///< 0 lays out on every core when the song is long enough
```

**Por qué importa.** PageLayoutOptions no tiene stop_token ni bandera de cancelación, y ni measureRange ni el bucle de sistemas consultan nada entre trabajos. La app descarta el resultado viejo solo al terminar (score_document.cpp:476 `if (generation != generation_) { return; }`) y lanza un QtConcurrent::run por cada relayout() (línea 472) sin fusionar peticiones. Con ediciones seguidas (tecla mantenida, ~30 Hz) y layouts de 21 ms que usan todos los núcleos, se solapan varios layouts, cada uno con hardware_concurrency hilos propios; cada layout descartado tira hasta 132 ms de CPU en la canción grande y retrasa el que sí se va a mostrar.

**Propuesta.** Añadir a PageLayoutOptions un `std::stop_token stop` (o `const std::atomic<bool>* cancelled`) que se comprueba al empezar cada job de measureWidths (32 compases) y cada sistema; si está activo, layoutPages devuelve vacío o lanza una excepción propia. En la app: pedir parada del layout anterior al llamar a relayout() y fusionar peticiones (como mucho uno en vuelo y uno pendiente, gana el último).

**Nota del revisor.** Hechos confirmados: PageLayoutOptions no tiene señal de parada (src/notation/include/elara/notation/page_layout.hpp:17-31), ni measureRange ni el bucle de sistemas consultan nada, y relayout() lanza un QtConcurrent::run por llamada y solo descarta por generación al terminar (src/app/src/score_document.cpp:470-477). Keys.onPressed (src/app/qml/ScoreView.qml:134) no filtra la autorrepetición, así que una tecla mantenida sí genera ediciones seguidas. Lo que no se sostiene con los tiempos medidos es el solapamiento: cada edición pasa por edited() (score_document.cpp:875-893), que en el hilo de UI copia la canción y la secuencia (10,7 + 28 ms en la canción grande) antes de llamar a relayout(). El intervalo mínimo entre dos lanzamientos es por tanto ~39 ms, mayor que los 21 ms del layout más caro (3,5 ms con una pista), así que en régimen estable el layout anterior ya ha terminado cuando se lanza el siguiente y cancelar ahorraría muy poco. Solo podrían solaparse cambios rápidos de pista o vista (setTrack/setView, líneas 400-418), que no copian ni secuencian. Inferencia mía sin medir: con la UI saturada, cada resultado puede llegar ya obsoleto y descartarse, pero eso lo arregla abaratar edited(), no la cancelación. La propuesta es viable y segura (comprobación por job de 32 compases y por sistema, sin relación con el hilo de audio); si se lanza una excepción propia, hay que comprobar la generación antes de fail(), como ya ocurre en la línea 476.

#### notation/N7 · Items de DisplayList con relleno evitable y size_t para el índice de compás

- **Dónde:** `src/notation/include/elara/notation/display_list.hpp:57`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
struct GlyphItem {
    Glyph glyph{};
    Point origin;
    Role role{};
    std::size_t measure = 0;
    double scale = 1.0;
```

**Por qué importa.** Tamaños calculados a mano para x64/MSVC, no medidos: GlyphItem 48 bytes (Glyph y Role son uint8_t, cada uno con 7 bytes de relleno, más size_t measure), LineItem 64, BeamItem 48, TextItem 72 (std::string de 32 bytes para textos que casi siempre son 1-2 dígitos de traste), CurveItem 96, PolygonItem 40 más un bloque de heap por cada punta de flecha de 3 puntos (tab_staff.cpp:585). Con todas las pistas en notación y tablatura (160k notas: cabeza + número de traste + plicas y barras) estimo 20-30 MB de items vivos, duplicados mientras conviven las páginas viejas y las nuevas durante un relayout (pages_ es un shared_ptr que la pintura también retiene). Los mismos bytes se recorren en shift(), inkExtents() y al pintar.

**Propuesta.** Reordenar y estrechar: GlyphItem {Point origin; double scale; uint32_t measure; Glyph glyph; Role role;} = 32 bytes (-33%); LineItem con uint32_t measure y Role juntos = 56; BeamItem 48 -> 40 bajando measure a uint32_t (tras los doubles); TextItem con los 4 campos de 1 byte y uint32_t measure juntos = 64; CurveItem 96 -> 88. PolygonItem con almacenamiento en línea para 3-4 puntos (std::array<Point, 4> más contador) para quitar la asignación por flecha. Mantener double en coordenadas para no cambiar resultados. Añadir static_assert(sizeof(...)) como ya se hace con Note/Beat.

**Nota del revisor.** Tamaños actuales verificados sobre las definiciones (Glyph y Role son uint8_t, src/notation/include/elara/notation/smufl.hpp:20 y display_list.hpp:18; Point son dos double): GlyphItem 48, LineItem 64, BeamItem 48, TextItem 72 (con std::string de 32 bytes en MSVC release), CurveItem 96, PolygonItem 40. Errores del hallazgo: (1) BeamItem no baja a 40: 16+16+8+4 = 44, que con alineación 8 vuelve a ser 48; no hay ahorro. (2) 'static_assert(sizeof) como ya se hace con Note/Beat' es falso: no hay ningún static_assert en src. (3) El único productor de polígonos es la punta de flecha de bend (src/notation/src/tab_staff.cpp:585 a través de StaffWriter::polygon, layout_common.cpp:580); son pocos items y el almacenamiento en línea no compensa cambiar la API pública. Impacto inflado: los 20-30 MB son una estimación sin medir y el ahorro real se concentra en GlyphItem (48 a 32), con LineItem 64 a 56, TextItem 72 a 64 y CurveItem 96 a 88; en conjunto ~15-20 % de los items, unos pocos MB en el peor caso de una app de escritorio. La capacidad sobrante de los vectores por página (N5) pesa lo mismo o más. Coste omitido: reordenar campos rompe los inicializadores designados, que en C++ deben ir en orden de declaración (p. ej. system_layout.cpp:490, y al menos 19 sitios en src/notation y tests/app/test_score_painter.cpp), y pasar measure a uint32_t introduce conversiones de estrechamiento en inicialización con llaves en todos los productores. No afecta al determinismo si las coordenadas siguen en double.

**Observaciones del área (qué está bien resuelto).**

Auditoría de solo lectura del área notation: no se ha editado, compilado ni ejecutado nada.
Leídos enteros: display_list.hpp/.cpp, page_layout.hpp/.cpp, layout_common.hpp/.cpp, system_layout.cpp, standard_staff.cpp, smufl.cpp, svg.cpp, caret.cpp, cursor.cpp, además de core/parallel.hpp y los puntos de llamada en src/app/src/score_document.cpp. De tab_staff.cpp se leyeron las líneas 1-609 de 610. De smufl.hpp solo se revisaron las declaraciones por grep. No se abrieron caret.hpp, cursor.hpp, staff_layout.hpp, svg.hpp ni drum_map.cpp/.hpp.
Bien resuelto, no tocar:
- Glifos SMuFL: Glyph es un enum uint8_t y EngravingFont::metrics() indexa un std::array<GlyphMetrics, kGlyphCount>. No hay mapas por nombre en el camino caliente; glyphNamed() es lineal pero no se usa en layout, y los metadatos se parsean una vez (static en la app).
- roleName() es una tabla constexpr. No hay std::function: todo son lambdas y plantillas (forEachIndex es plantilla).
- LineBreaker: programación dinámica con corte temprano y sumas natural_/tightest_/ideal_ precalculadas.
- measureRange y layoutLine reutilizan writers y vectores de trabajo (columns, all, merged, positions) entre compases; clearItems() conserva capacidad.
- inkExtents() solo se llama una vez por staff al cerrar la línea (close/finish), no por compás. markTempo ya evita la búsqueda cuadrática del tempo.
- append() mueve el vector entero cuando el destino está vacío.
- Los números de traste caben en el SSO de std::string: no asignan en heap, solo ocupan (ver N7).
- Seguridad de hilos: los jobs de forEachIndex escriben en índices disjuntos (systems[s], subspans de widths) y solo leen Song y EngravingFont const. No he visto carreras en notation, y el resultado es determinista.
- No hay cachés en notation, así que no hay cachés sin cota. Nada de este módulo corre en el hilo de audio.
- cursorAt() y caretAt()/slotX() hacen búsquedas lineales sobre las MeasureBox/ColumnBox de las páginas, pero son cientos de elementos por llamada (sub-microsegundo); no merece hallazgo. Si se quisiera, cursorAt admite búsqueda binaria porque los compases están en orden.
- StaffWriter::finish() inserta las líneas del pentagrama al principio de list_.lines (memmove de unos KB por staff): coste despreciable.
Dependencias entre hallazgos: N1 es el de mayor retorno para la edición interactiva; N3 y N4 reducen el coste de todo layout completo (abrir, cambiar pista o vista) y son independientes de N1. N2 y N6 tocan core/parallel.hpp y la app además de notation.

### Core (modelo y edición)

#### core/A1 · El modelo no ofrece instantánea barata ni extensión del cambio: cada edición copia la canción entera y la resecuencia en el hilo de UI

- **Dónde:** `src/core/include/elara/core/document.hpp:47`
- **Eje:** concurrencia · **Impacto:** alto · **Esfuerzo:** L · **Veredicto:** confirmado

```cpp
[[nodiscard]] const Song& song() const noexcept { return song_; }
```

**Por qué importa.** Document solo expone la Song por referencia y Command (document.hpp:29-35) no dice qué tocó (pista/pentagrama/compases). Staff guarda `std::vector<Measure> measures_` por valor (song.hpp:62), así que cualquier consumidor que necesite una copia estable para otro hilo tiene que copiar todo. El consumidor real lo hace en cada tecla: src/app/src/score_document.cpp:877 `song_ = std::make_shared<const core::Song>(document_->song());` y :880 `std::make_shared<const audio::Sequence>(*song_)`, ambos síncronos en el hilo de UI. Con los tiempos medidos son 10,7 ms de copia + ~26-28 ms de secuenciado = ~40 ms de bloqueo por pulsación en la canción grande (2-3 frames a 60 Hz), para un comando que cuesta 0,60 us. Además viven siempre dos copias completas del modelo (Document::song_ y la instantánea), y una tercera mientras un layout en curso retiene la anterior: ~15 MB cada una.

**Propuesta.** Dar al core compartición estructural y extensión del cambio: (1) Staff con los compases en un bloque copy-on-write (p. ej. `std::shared_ptr<const std::vector<Measure>>` que se separa en el `measure()` no const, o trozos de 32-64 compases), de modo que copiar la Song sea O(pentagramas) y una edición solo duplique el pentagrama/trozo tocado (~0,5 ms por pista de 500 compases en vez de 10,7 ms); (2) añadir a Command un `extent()` (pista, pentagrama, primer compás, número, o 'estructura') que Document acumule junto a un contador de revisión, para que secuenciador y layout puedan rehacer solo lo afectado y para sacar el secuenciado completo del hilo de UI.

**Nota del revisor.** Comprobado: document.hpp:47 solo expone `const Song&`; Command (document.hpp:18-36) no declara extensión; Staff guarda `std::vector<Measure> measures_` por valor (song.hpp:62). En src/app/src/score_document.cpp, `edited()` (875-894) copia la canción (877) y construye la Sequence (880) de forma síncrona; se llama desde execute/undo/redo, todos en el hilo de UI, y solo después lanza `relayout()` con QtConcurrent (893). Con los tiempos medidos son ~10,7 + 26-28 ms por edición en la canción grande; además la línea 881 compara la secuencia nueva con la anterior (`*sequence != *sequence_`), coste que el hallazgo no cuenta. La tercera copia viva también es cierta (la lambda de relayout captura `song = song_`). Matices: (a) es un coste conocido y aceptado por escrito, no un descubrimiento: ADR-0031 (líneas 50-54) y ADR-0032 (58-61) ya dicen que cada edición copia la canción y que hará falta copy-on-write de compases; (b) los ~40 ms son solo para la canción de estrés (20 pistas, 160k notas); en canciones normales queda dentro de un frame; (c) los ~15 MB por copia no están medidos, son una estimación coherente con Note 48 B / Beat 64 B. Sobre la propuesta: el COW a nivel de Staff/trozo es viable y no choca con ADR-0005 (que rechazó estructuras persistentes para no convertir Measure/Beat/Note en handles) siempre que esos tipos sigan siendo valores; ojo con que `operator==` de Staff es `= default` y pasaría a comparar punteros, y con que cualquier `measure()` no const separaría el bloque (con un bloque único por pentagrama cada edición recopia ~0,5 ms; mejor trozos). El COW solo quita 10,7 de los ~40 ms: la mayor parte se elimina sin tocar el core, construyendo la Sequence en un worker con contador de generación como ya hace relayout (la instantánea ya es `shared_ptr<const Song>`). `extent()` para resecuenciar parcialmente es mucho más dudoso (ligaduras entre compases, repeticiones, tempo) y no debería venderse como parte del mismo esfuerzo.

#### core/A4 · soundingNotes: cada ronda de fusión asigna búferes nuevos, los inicializa y copia; las listas crecen sin reserve a través de una referencia al vector compartido

- **Dónde:** `src/core/src/performance.cpp:142`
- **Eje:** memoria · **Impacto:** medio · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
merged[i].resize(a.size() + b.size());
```

**Por qué importa.** SoundingNote ocupa 80 bytes y sus valores por defecto no son cero (key = 60, velocity = 80), así que resize() escribe cada elemento antes de que std::ranges::merge (línea 143) lo sobrescriba: doble pasada de escritura. Con 160k notas son 12,8 MB por ronda y unas 5 rondas para 20 listas: ~64 MB asignados, tocados por primera vez (fallos de página en asignaciones grandes recién pedidas) y liberados por secuenciado, con pico de 2x (25,6 MB) y las últimas rondas en 2 y 1 hilos. Además, en la línea 120 `std::vector<SoundingNote>& list = lists[i];` cada hilo hace push_back a través de la cabecera del vector que vive en el array compartido `lists` (24 bytes por cabecera, varias por línea de caché: falso compartido cuando voces contiguas tienen notas) y sin reserve (unas 14 realocaciones por lista de 8k notas).

**Propuesta.** En la lectura: construir en un vector local con `reserve` (estimación barata: beats de la voz x 2, o un conteo previo) y moverlo a `lists[i]` al terminar. En la fusión: reservar una sola vez dos búferes del tamaño total y alternar entre ellos por rondas (cada lista es un tramo [inicio, fin) del búfer), fusionando con std::merge de tramo a tramo; así desaparecen las asignaciones por ronda, la inicialización previa y el pico queda acotado a 2x fijo. Alternativa: una fusión de k vías en una sola pasada sobre el búfer final.

**Nota del revisor.** Comprobado: performance.cpp:142 hace `resize(a.size()+b.size())` y la 143 fusiona encima; SoundingNote tiene inicializadores no nulos (key = 60, velocity = 80; performance.hpp:23-24) y mide 80 bytes, así que cada ronda escribe dos veces. Con 20 listas son 5 rondas (10, 5, 3, 2, 1) y la última es de un solo hilo sobre las 160k notas (12,8 MB): es la parte que más pesa. `lists[i]` se rellena por referencia y sin reserve (línea 120 y push_back en addVoice, línea 57). Matices que rebajan parte del texto: el falso compartido solo ocurre cuando dos voces del mismo pentagrama tienen notas; las cabeceras de la voz 0 de pentagramas consecutivos están a 96 bytes (4 voces x 24), en líneas de caché distintas, y las voces vacías no escriben. El número de realocaciones depende de la biblioteca (1,5x en MSVC son más de 14). Los ~64 MB no son exactos porque las listas impares se mueven, no se copian. El ahorro no está medido; razonablemente son 2-4 ms de los 10,7 ms de soundingNotes, que se pagan en cada edición y en la exportación MIDI, por eso mantengo medio (pasaría a bajo si el secuenciado sale del hilo de UI como propone A1). Sobre la propuesta: reservar búferes de SoundingNote con `resize` o `new[]` sigue ejecutando los inicializadores; la corrección mínima y segura es `reserve` + `std::ranges::merge(a, b, std::back_inserter(merged[i]), earlier)`, o dos búferes alternos inicializados una sola vez. El determinismo se conserva porque el comparador incluye pista, pentagrama y voz (no hay empates entre listas). El mismo patrón está en sequence.cpp:696-711 con los 320k eventos y el hallazgo no lo menciona.

#### core/A2 · forEachIndex crea y destruye std::thread en cada llamada, sin pool ni tope global

- **Dónde:** `src/core/include/elara/core/parallel.hpp:44`
- **Eje:** concurrencia · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
workers.emplace_back([&, t] {
```

**Por qué importa.** Cada llamada arranca `threads` hilos nuevos y hace join; el hilo llamante no trabaja, solo espera. threadsFor (línea 21) pide `hardware_concurrency()` sin saber qué más corre. soundingNotes llama a forEachIndex una vez por lectura y otra por cada ronda de fusión (performance.cpp:118 y :134), y sequence.cpp otras tres (469, 638, 698 en bucle): en la máquina de referencia (24 hilos lógicos) un solo secuenciado crea del orden de 60-100 hilos. En Windows crear+unir un hilo cuesta decenas de us, es decir, varios ms de los 26-28 ms del secuenciado, que además se ejecuta por edición. Como no hay pool compartido, un relayout (page_layout.cpp:298, system_layout.cpp:593) lanzado desde QtConcurrent coincide con el secuenciado y con el pintado de páginas en hilos de trabajo: 2-3x de sobre-suscripción en el momento en que se mide la latencia de la edición. La granularidad es de un índice por `next++` (línea 46), con la línea de caché de `next` rebotando entre todos los hilos cuando los trabajos son pequeños (un compás).

**Propuesta.** Mantener la firma de forEachIndex y cambiar la implementación por un pool persistente perezoso (hardware_concurrency()-1 trabajadores, creado una vez) en el que el hilo llamante también toma índices; reparto por bloques (p. ej. `next.fetch_add(chunk)` con chunk = max(1, n/(8*hilos))) y un límite global de trabajadores activos para que llamadas concurrentes no superen el número de núcleos. Con ello el umbral `parallelFrom` puede bajar porque arrancar ya no cuesta.

**Nota del revisor.** Cierto: parallel.hpp:41-56 crea `threads` std::thread por llamada y el llamante solo hace join; threadsFor (línea 21) usa hardware_concurrency() con tope de trabajos y 64. El recuento de hilos por secuenciado es correcto e incluso corto: performance.cpp:118 (80 partes -> 24 hilos) + rondas de fusión 10+5+3+2 (134) + sequence.cpp:469 (20 bloques de 8192) + 638 (20 pistas) + 698 (~20) = ~100 hilos por edición en la máquina de 24 hilos. Que eso sean "varios ms" es plausible en Windows pero no está medido en el repo. Lo que no se sostiene: (1) la sobre-suscripción "en el momento en que se mide la latencia": en score_document.cpp `edited()` secuencia de forma síncrona (880) y solo después llama a `relayout()` (893), así que el layout de una edición nunca coincide con su propio secuenciado; solo puede solaparse con el layout/pintado de la edición anterior si se teclea más rápido que los 3,5-21 ms de layout. (2) El rebote de la línea de caché de `next` con trabajos de "un compás": ningún llamante usa trabajos tan finos; son bloques de 8192 notas (sequence.cpp:35), 32 compases (system_layout.cpp:64), un sistema, una pista o una voz entera, frente a los cuales un fetch_add es despreciable. Por eso bajo el impacto a medio. Propuesta viable con condiciones: forEachIndex no se llama desde el hilo de audio (offline_renderer es fuera de tiempo real); el llamante debe participar para evitar interbloqueo si hay llamadas anidadas o concurrentes (layout en worker de QtConcurrent + UI); el resultado ya es por índice, así que el determinismo se conserva, pero el contrato "primera excepción en orden de hilo" hay que redefinirlo; un pool estático en una cabecera header-only exige cuidar la destrucción al salir. Bajar los umbrales `parallelFrom` requiere volver a medir, no se deduce.

#### core/A7 · SoundingNote ocupa 80 bytes por usar size_t e int en campos que caben en 8-16 bits

- **Dónde:** `src/core/include/elara/core/performance.hpp:18`
- **Eje:** memoria · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
std::size_t track = 0;
```

**Por qué importa.** track, staff y voice son size_t (24 bytes) cuando voice es 0..3 y los otros dos son pequeños; key y velocity son int para valores 0..127; measure es size_t. Con los dos Fraction (32 bytes) el resto cabría en 16 bytes: 48 bytes en total frente a 80. Son 160k x 32 = 5,1 MB menos por vector de notas sonantes, y como la fusión por pares mueve todas las notas en cada ronda (unas 5), un 40 % menos de tráfico de memoria en soundingNotes (10,7 ms medidos) y en quien las recorre (secuenciador, exportación MIDI).

**Propuesta.** Reordenar y estrechar: `Fraction start, length; std::uint32_t measure; std::uint32_t beat; std::uint16_t track; std::uint8_t staff, voice, note, key, velocity;` (48 bytes), validando los rangos al construir en addVoice. Añadir un static_assert del tamaño para que no vuelva a crecer.

**Nota del revisor.** Comprobado: performance.hpp:18-29 tiene track/staff/voice/measure como size_t, key/velocity como int y beat/note como uint32; 24 + 32 + 8 + 8 + 8 = 80 bytes (el comentario de la línea 26 dice que se mantuvo a propósito en 80). El layout propuesto suma 47 y queda en 48 por alineación: correcto. Los rangos caben: voice < 4 (Measure::kMaxVoices), key 0..127, velocity <= 127 (dynamicVelocity). Matiz: el vector de notas sonantes es transitorio (local en el constructor de Sequence, sequence.cpp:465, y en la exportación MIDI), así que no es memoria residente sino ancho de banda durante el secuenciado; la categoría real es velocidad y el ahorro en tiempo no está medido. Impacto bajo, de acuerdo. Para aplicarlo: los inicializadores designados de addVoice (performance.cpp:57-66) y de los tests van en orden de declaración, así que reordenar campos obliga a tocarlos (falla al compilar, no en silencio) y las conversiones de int a uint8 necesitan cast; no hay en el core un máximo de notas por beat ni de pistas, así que `note` en uint8 y `track` en uint16 necesitan una comprobación explícita (una vez por parte para track/staff/voice, por beat para note), no solo un static_assert de tamaño.

#### core/A8 · Transponer, reafinar y cambiar dinámica copian y comparan compás a compás en serie y crean un comando en el montón por compás

- **Dónde:** `src/core/src/transpose.cpp:138`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
Measure content = staff.measure(m);
```

**Por qué importa.** makeTranspose copia cada compás, lo transforma y luego lo compara en profundidad con el original (línea 140, `if (content != staff.measure(m))`) para decidir si hay cambio; el mismo patrón está en retune.cpp:77/83 y entry.cpp:748/766. Para toda la canción grande es una copia completa (10,7 ms) más la comparación completa más hasta 10.000 `std::make_unique<ReplaceMeasure>` (objetos de ~160 bytes con un std::string de nombre cada uno), todo en un hilo y en el hilo que invoca la orden: del orden de 20-30 ms de bloqueo. Los pentagramas son independientes (el estado `followed` es por pentagrama), así que es paralelizable sin cambiar el resultado.

**Propuesta.** Hacer que transposeMeasure/soundFrets/placeOnStrings devuelvan si cambiaron algo (evita la comparación profunda, y permite copiar solo cuando la primera nota va a cambiar); repartir los pentagramas con forEachIndex recogiendo los resultados por índice para mantener el orden determinista; y sustituir los N ReplaceMeasure por un único comando de rango (pista, pentagrama, primer compás, `std::vector<Measure>`) que intercambie el tramo entero.

**Nota del revisor.** Comprobado el patrón copia + transforma + compara + make_unique por compás en transpose.cpp:138-143, retune.cpp:77-86 y entry.cpp:748-769, todo en un hilo y en el hilo que invoca la orden. ReplaceMeasure (commands.hpp:75-93) ocupa ~160 bytes; su std::string de nombre ("Edit measure", "Change dynamic") cabe en SSO, así que no hay asignación extra por el nombre. Los 20-30 ms son una estimación sin medir (el coste de placeOnStrings no está en docs/performance.md), y son órdenes de menú poco frecuentes cuya latencia queda además dominada por los ~40 ms de A1; impacto bajo, de acuerdo. `followed` se declara dentro del bucle de pentagramas (transpose.cpp:136), así que los pentagramas son independientes y paralelizables. Condiciones para la propuesta: transposeMeasure lanza invalid_argument con el número de compás (45-47) y forEachIndex relanza la primera excepción en orden de hilo, no de índice; para que el mensaje sea el mismo que en serie hay que capturar por pentagrama y relanzar el de menor índice (como hace guitar_pro.cpp:1241-1281). En retune y setDynamic no hay nada que paralelizar: es un solo pentagrama con estado encadenado entre compases. En transposición con intervalo no nulo todo compás con notas cambia, así que la comparación profunda puede sustituirse por "tiene notas"; en retune hace falta el booleano de cambio real. El comando de rango único es viable y elimina las 10.000 asignaciones, pero no reduce la memoria retenida en undo (ver A6).

#### core/A9 · forEachIndex termina el proceso si falla la creación de un hilo y no cancela el trabajo restante tras la primera excepción

- **Dónde:** `src/core/include/elara/core/parallel.hpp:41`
- **Eje:** concurrencia · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
std::vector<std::thread> workers;
```

**Por qué importa.** Si `workers.emplace_back` (línea 44) lanza std::system_error con hilos ya arrancados (límite de hilos o memoria, más probable precisamente porque cada llamada crea hasta 64), el vector se destruye con std::thread unibles y se llama a std::terminate: la aplicación se cierra en lugar de degradar a menos hilos. Por otro lado, cuando un trabajo lanza, su hilo guarda el error y sale (líneas 49-51), pero los demás siguen consumiendo índices hasta el final: al abrir un fichero corrupto se importan todas las pistas restantes antes de informar del error, trabajo que se descarta.

**Propuesta.** Usar std::jthread (o un guardián que haga join en el destructor) y, si la creación falla, continuar con los hilos ya creados más el llamante. Añadir un `std::atomic<bool> failed` que cada trabajador comprueba antes de tomar el siguiente índice; para conservar el contrato de 'primera excepción en orden' basta con seguir relanzando la del hilo de menor índice. Ambos puntos se resuelven de paso si se adopta el pool de A2.

**Nota del revisor.** Comprobado: `workers` es un `std::vector<std::thread>` (parallel.hpp:41) y si `emplace_back` (44) lanza system_error con hilos ya arrancados, su destructor encuentra hilos unibles y llama a std::terminate. Es un caso muy improbable en escritorio (falla la creación de un hilo), así que es un defecto de robustez de impacto bajo, como se propone. También es cierto que tras una excepción los demás hilos siguen tomando índices (46-51, no hay bandera de cancelación). Matices: el ejemplo del fichero corrupto solo aplica a los llamantes que dejan salir la excepción (musicxml.cpp:1906-1919, readPart); el importador de Guitar Pro captura dentro del trabajo y guarda el daño por índice (guitar_pro.cpp:1241-1281) para dar siempre el mismo error, así que una bandera en forEachIndex no le ahorra nada. El trabajo desperdiciado solo ocurre en el camino de error y como mucho equivale a una importación normal (~100 ms). Sobre la propuesta: std::jthread arregla el terminate (el orden de declaración next, errors, workers hace que el join ocurra antes de destruir lo que los hilos referencian); degradar a menos hilos exige capturar el system_error y que el llamante trabaje. Con cancelación temprana el error que se informa pasa a depender del momento en que cada hilo falla: hoy ya depende del orden de hilos y no del índice, pero conviene fijar el contrato en "excepción del menor índice de trabajo" para no romper la garantía documentada de mismos errores con cualquier número de hilos (docs/performance.md:67).

#### core/A3 · Fraction calcula 2-3 gcd de 64 bits por suma/resta y vuelve a normalizar valores ya normalizados

- **Dónde:** `src/core/include/elara/core/fraction.hpp:50`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
friend constexpr Fraction operator-(const Fraction& a, const Fraction& b) { return a + -b; }
```

**Por qué importa.** operator+ (líneas 46-47) hace un gcd de denominadores y luego construye con el constructor público, que llama a normalise() (otro gcd, línea 90). El menos unario (línea 43, `Fraction(-num_, den_)`) vuelve a normalizar algo que ya lo está, así que una resta son 3 gcd; Duration::length() añade otro por beat. Es el patrón de todos los bucles calientes del modelo: `position += length` por beat en performance.cpp:69, Voice::length() en measure.cpp:12, validación. docs/performance.md ya lo señala: en validar (3,0 ms) 'half of it summing voice lengths' y en importar MIDI en un hilo 287 ms 'mostly fraction arithmetic in the grids'. Casi todos los denominadores reales son potencias de dos (o iguales entre sí), caso en el que no hace falta ningún gcd.

**Propuesta.** Añadir un constructor privado etiquetado que no normaliza y usarlo en el menos unario y en los resultados ya reducidos; en operator+ y operator- un camino rápido para `a.den_ == b.den_` y otro para denominadores potencia de dos (`(d & (d - 1)) == 0`): sumar con desplazamientos y reducir con `std::countr_zero` en lugar de gcd; implementar operator- directamente. Opcional: tabla constexpr de 9 valores x 4 puntillos para Duration::length() sin grupo irregular. Estimación: sumar duraciones pasa de ~3 gcd a 0 en el caso común; validar bajaría hacia ~1,7 ms y el beneficio mayor está en las rejillas del importador MIDI.

**Nota del revisor.** El código es como se describe: operator+ (45-48) hace gcd de denominadores y el constructor público vuelve a normalizar (gcd en la línea 90); el menos unario (43) pasa por el constructor y normaliza otra vez; operator- (50) es `a + -b`, es decir 3 gcd. Duration::length() (duration.hpp:83-90) añade uno por beat. Las citas de docs/performance.md son literales (líneas 44 y 63). El impacto sí está inflado: (a) `validate` no se llama desde src/app, así que los 3,0 ms no están en el camino de edición; (b) los 287 ms del importador MIDI son el caso de un solo hilo, por defecto son 40,5 ms; (c) en el camino caliente real (soundingNotes y sequence.cpp:476-478, unos 5 gcd por nota) el trabajo ya está repartido entre ~20 hilos, de modo que el ahorro en tiempo de pared por edición es del orden de décimas de ms a ~1 ms sobre ~40. Los gcd son sobre enteros pequeños (pocos ns cada uno). Es una microoptimización barata y correcta, no un problema de impacto medio. Correcciones a la propuesta: el camino `a.den_ == b.den_` por sí solo no evita reducir (1/4 + 1/4 = 2/4); solo el caso potencia de dos permite cero gcd con countr_zero (cuidando numerador 0 y negativos); con tresillos (denominadores 12, 24) se sigue necesitando gcd, aunque basta gcd(num, g) en vez de gcd(num, den). El constructor privado sin normalizar para el menos unario es correcto. Todo sigue siendo constexpr. Las cifras estimadas (validar a ~1,7 ms) no están medidas.

#### core/A5 · Cada nota y cada beat llevan un std::vector de 24 bytes para una curva casi siempre vacía, y cada beat una asignación propia para sus notas

- **Dónde:** `src/core/include/elara/core/effects.hpp:58`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
std::vector<BendPoint> points_{};
```

**Por qué importa.** BendCurve es la mitad de Note (24 de 48 bytes, vía NoteEffects::bend, línea 98) y más de un tercio de Beat (BeatEffects::whammyBar, línea 122: 24 de 64). En la canción grande son 160k x 24 + 80k x 24 = 5,8 MB de punteros nulos sobre un modelo de ~15 MB, multiplicado por cada copia viva (Document, instantánea de la app, portapapeles, compases retenidos en undo). También impide que Note sea trivialmente copiable: docs/performance.md atribuye lo que queda de los 10,7 ms de copia a 'copying each note's (usually empty) bend curve'. BendPoint usa dos int (8 bytes) para una posición 0..100 y unos cents de +-2400. Aparte, `std::vector<Note> notes` por Beat (measure.hpp:32) supone ~80k asignaciones de montón al copiar la canción, que es lo que domina el tiempo de copia.

**Propuesta.** Paso 1 (S/M): guardar la curva tras un único puntero (nulo si está vacía; bloque con contador + puntos de 4 bytes: int8 posición, int16 cents) manteniendo la API de BendCurve: Note 48 -> 32 bytes y Beat 64 -> 48, unos 3,8 MB menos por copia de la canción grande. Paso 2 (L): sacar las curvas a una tabla lateral por Voice (la nota guarda un índice de 8-16 bits) para que Note sea trivialmente copiable, y aplanar las notas por voz (`std::vector<Note>` en Voice; Beat con desplazamiento + cuenta) para pasar de ~80k asignaciones a una por voz: la copia completa debería caer a pocos ms.

**Nota del revisor.** Los hechos son ciertos: BendCurve es un `std::vector<BendPoint>` (effects.hpp:58), va por valor en NoteEffects::bend (98) y BeatEffects::whammyBar (122); BendPoint son dos int (16-21); Beat lleva `std::vector<Note> notes` (measure.hpp:32); docs/performance.md:42-43 da Note 48 B, Beat 64 B y atribuye el resto de la copia a la curva vacía. La aritmética (160k x 24 + 80k x 24 = 5,8 MB) cuadra. Lo que no se sostiene: (1) el hallazgo se contradice sobre qué domina los 10,7 ms de copia: cita al documento (la curva) y a la vez afirma que dominan las ~80k asignaciones de `notes`; ninguna de las dos cosas está medida por separado. (2) El impacto: 4-6 MB por copia en una aplicación de escritorio y solo en la canción de estrés es poco; el tiempo de copia solo importa porque la app copia en cada edición, y eso lo resuelve A1, no esto. (3) El paso 1 no hace Note trivialmente copiable (un puntero con contador sigue teniendo constructor de copia, y el contador tendría que ser atómico porque las instantáneas se leen desde workers), así que apenas mejora el tiempo de copia. (4) "Manteniendo la API de BendCurve" no es cierto: `points()` devuelve `const std::vector<BendPoint>&` (línea 46) y con puntos compactos habría que devolver otra cosa; hay 5 usos en 4 ficheros (sequence.cpp, musicxml_export.cpp, native_format.cpp, tab_staff.cpp), asumible pero es cambio de API. El paso 2 (aplanar notas por voz) toca `beat.notes` en todos los importadores, notación y edición y rompe los tipos valor de ADR-0004: es de esfuerzo L con beneficio que se solapa con A1.

#### core/A6 · El historial de undo se acota por número de pasos (1000), no por memoria retenida

- **Dónde:** `src/core/include/elara/core/document.hpp:42`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
static constexpr std::size_t kDefaultUndoLimit = 1000;
```

**Por qué importa.** El recorte de document.cpp:29 (`while (done_.size() > undoLimit_)`) solo cuenta comandos. Un ReplaceMeasure retiene un compás (~1-2 KB) y 1000 de ellos son ~2 MB, pero los comandos gruesos retienen copias completas: makeTranspose sobre toda la canción genera un ReplaceMeasure por compás cambiado de cada pista (transpose.cpp:141-142), es decir, una copia íntegra del contenido (~15 MB) por paso; makeRetune (retune.cpp:84-85) y setDynamic (entry.cpp:767-768, que recorre hasta el final del pentagrama cuando toda la pasada comparte dinámica) retienen una pista entera; RemoveTrack, otra. Doce transposiciones seguidas de un semitono son ~180 MB retenidos y el límite teórico es 1000 x 15 MB. La pila de rehacer (`undone_`) tampoco tiene cota propia.

**Propuesta.** Añadir a Command un `retainedBytes()` (estimación barata: compases x beats x sizeof + notas) y recortar `done_` por presupuesto de bytes además de por número (p. ej. 64-128 MB configurables), conservando siempre al menos el último paso. Como complemento, fusionar pasos consecutivos equivalentes (transposiciones seguidas del mismo ámbito) en un único comando que retenga solo el estado original.

**Nota del revisor.** Cierto: kDefaultUndoLimit = 1000 (document.hpp:42) y el recorte de document.cpp:29-34 solo cuenta pasos. makeTranspose genera un ReplaceMeasure con copia nueva por cada compás cambiado (transpose.cpp:138-142), de modo que transponer toda la canción retiene una copia completa por paso; makeRetune (retune.cpp:77-85) y setDynamic (entry.cpp:744-768, que llega al final del pentagrama si toda la pasada comparte dinámica) retienen un pentagrama. Matices: (a) RemoveTrack no añade memoria: mueve la pista al comando (ADR-0005, "the history holds exactly what the song no longer holds"), solo impide liberarla; (b) la pila de rehacer sí está acotada de hecho: `undone_` solo recibe pasos que salen de `done_` y `execute()` la vacía (línea 28), así que done + undone nunca supera el límite; (c) el escenario es raro: 12 transposiciones de la canción entera de estrés son ~180 MB en una máquina de escritorio, y en canciones normales diez veces menos; el límite teórico de 1000 x 15 MB exige mil transposiciones completas. Es un límite de robustez, no un coste que se pague en uso normal; la cifra de ~15 MB es estimada. Sobre la propuesta: `retainedBytes()` es viable (calcularlo una vez tras apply; para un grupo de 10.000 ReplaceMeasure supone recorrer todo el contenido, ~1 ms) y debe contar también `undone_` y respetar `savedDepth_` como ya hace el bucle de recorte. Fusionar transposiciones consecutivas no es una optimización neutra: cambia lo que hace Ctrl+Z (deshace varias de golpe) y ADR-0005 deja la fusión de ediciones como decisión de producto pendiente.

**Observaciones del área (qué está bien resuelto).**

Alcance: leídos enteros song.hpp/cpp, measure.hpp/cpp, effects.hpp, fraction.hpp, duration.hpp, document.hpp/cpp, commands.hpp/cpp, entry.cpp, validation.cpp, performance.hpp/cpp, parallel.hpp, clipboard.cpp, retune.cpp; por secciones transpose.cpp, fret_placement.cpp, pitch.hpp, tuning.hpp. Para A1 y A2 consulté además los puntos de llamada en src/app/src/score_document.cpp (líneas 472 y 875-893) y un grep de forEachIndex en todo src; no he leído el interior de audio/sequence.cpp ni de notation, así que los recuentos de hilos de A2 fuera de core son estimaciones a partir de las firmas de llamada.
Bien resuelto, no tocar:
- Comandos de edición: ReplaceMeasure, SetMeasureHeader, SetSongInfo, RemoveMeasures, Insert/RemoveTrack intercambian o mueven, no copian; RemoveMeasures reserva antes de mover (commands.cpp:29-30) y Document::execute/redo reservan el hueco antes de apply() para que nada falle a medias.
- Entrada por tecla (entry.cpp): cada orden copia un único compás (`measureAt`, línea 62) y lo devuelve en un ReplaceMeasure; son microsegundos (0,60 us medidos para editar + deshacer). El coste por pulsación no está aquí sino en lo que se recalcula después (A1). dynamicAt/previousNote recorren hacia atrás solo hasta la primera nota, y clampCaret/lastIndex suman un compás: despreciable.
- Validación: `validate` es secuencial y completa (3 ms), pero en la app no se llama por edición; solo la usa la CLI (edit_commands.cpp:514, file_commands.cpp:96/204, song_report.cpp:114). No necesita caché ni hilos.
- Tuning es de capacidad fija sin asignación; Pitch, Duration, Tuplet y FretPosition ocupan 2-4 bytes; MeasureHeader agrupa sus flags; Song::playOrder hace reserve y tiene guarda de 256x.
- threadsFor ya evita hilos por debajo de un umbral de trabajo y limita a min(núcleos, trabajos, 64); las excepciones se relanzan en orden de hilo, y el resultado es determinista porque cada trabajo escribe en su propio índice.
- El core no tiene estado global mutable, locks ni código que corra en el hilo de audio; no he encontrado carreras de datos dentro del módulo (los trabajos de soundingNotes solo comparten lecturas de la Song y escriben en `lists[i]`/`merged[i]` propios; lo señalado en A4 es falso compartido de caché, no una carrera).
Observaciones menores que no llegan a hallazgo: Measure reserva siempre 4 voces (`std::array<Voice, kMaxVoices>`, 96 bytes por compás y pentagrama, ~1 MB en la canción grande); Song::secondsAt, tempoAt y measureStart son O(compases) por llamada con aritmética de Fraction (sin llamadores en bucle dentro de core; notation/system_layout.cpp:463 llama a tempoAt y convendría que el área de notación compruebe si es por compás); struckStarts (entry.cpp:99) construye un std::map por compás en setDynamic.

### Audio

#### audio/A1 · El secuenciador fusiona las listas por pista en una lista global que el reproductor vuelve a partir por pista

- **Dónde:** `src/audio/src/sequence.cpp:696`
- **Eje:** velocidad · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
while (tracks.size() > 1) {
        std::vector<std::vector<NoteEvent>> merged((tracks.size() + 1) / 2);
```

**Por qué importa.** Sequence ya tiene los eventos ordenados por pista (`tracks[t]`, línea 637) y los funde en 5 rondas (20 -> 10 -> 5 -> 3 -> 2 -> 1). Cada ronda hace `merged[i].resize(a.size() + b.size())` (línea 707: inicializa 320k x 32 B) y luego el merge los reescribe: unos 100 MB de tráfico de memoria por secuencia, y la última ronda es un único merge de 320k en un solo hilo. En la app el único consumidor de `events()` es `detail::eventsByTrack` (playback.hpp:38), que deshace la fusión nada más cargarse. Solo la CLI y los tests leen la lista global. Ocurre en cada edición: score_document.cpp:880 construye una Sequence nueva y audio_controller.cpp:87 llama a Player::load en el hilo principal (player.hpp:23 lo documenta como hilo de control).

**Propuesta.** Guardar en Sequence las listas por pista como dato primario (`eventsOfTrack(t)`, más la del metrónomo) y hacer que Player::load y OfflineRenderer las consuman directamente. Dejar `events()` como fusión perezosa (o función aparte) para la CLI y los tests. `operator==` compararía pista a pista, con el mismo resultado. Elimina las 5 rondas, sus hilos y el reparto posterior.

**Nota del revisor.** Comprobado en src/audio/src/sequence.cpp:637-714: `tracks[t]` ya sale ordenado por pista y el bucle de 696-711 lo funde por parejas con `resize(a.size()+b.size())` (707, inicializa a cero) + `std::ranges::merge`. Con 20 pistas son 5 rondas (con metrónomo, 21 listas, también 5). NoteEvent mide 32 B (sequence.hpp:56-64). Consumidores de `events()` fuera de tests: solo playback.hpp:38 (`eventsByTrack`, usado por player.cpp:73 y offline_renderer.cpp:50) y la CLI (audio_commands.cpp:220-278); `eventAt` no tiene llamantes en src. Ocurre en el hilo de UI en cada edición: `ScoreDocument::edited()` (score_document.cpp:875-883) construye la Sequence de forma síncrona, incluso cuando solo cambia la mezcla. Matices: (1) el impacto no está medido; el techo es una fracción de los 28 ms de secuenciar (las dos últimas rondas van en 1-2 hilos sobre ~10 MB cada una; estimo 5-10 ms), por eso bajo a medio. (2) Player::load solo se ejecuta si ya existe `player_` (tras el primer play). (3) La propuesta es correcta: la fusión conserva el orden relativo de cada lista, así que partir la lista global por pista devuelve exactamente `tracks[t]` y comparar pista a pista equivale al `operator==` actual. (4) Ojo con la 'fusión perezosa': una caché mutable rompería el contrato de la clase (sequence.hpp:93-94, solo lectura tras construir, legible sin bloqueos); debe ser una función que devuelva por valor para CLI/tests, no un `events()` const con caché.

#### audio/A7 · noteOn de cuerda pulsada: tres pasadas sobre la línea de retardo, división y sin() por muestra en el hilo de audio

- **Dónde:** `src/audio/src/synth.cpp:734`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
for (std::uint32_t i = 0; i < voice.length; ++i) {
                line[i] /= peak;
            }
```

**Por qué importa.** Cada noteOn de guitarra, overdrive o bajo recorre `voice.length` muestras tres veces (líneas 716, 728 y 733): 585 muestras para un Mi grave a 48 kHz, 1170 para un bajo. Incluye una división por muestra y, en armónicos, `std::sin` en double por muestra (línea 718-719), además de tres `std::pow` (líneas 29, 598 y 707). Son del orden de 3-12 µs por nota (estimado). Los acordes de varias pistas coinciden en el mismo frame: 20 pistas x 6 notas da un pico de ~0,5 ms dentro de un solo bloque, apreciable con buffers de 64-128 frames (1,3-2,7 ms).

**Propuesta.** Una sola pasada que genere la excitación y acumule suma, mínimo y máximo; el pico tras restar la media es max(max - media, media - min). Una segunda pasada aplica `(x - media) * (1 / pico)`: 3 pasadas pasan a 2 y la división a multiplicación. Para el armónico, girar un fasor (cos/sin calculados una vez) en lugar de `std::sin` por muestra. `keyFrequency` y la curva de velocidad pueden ser tablas de 128 entradas calculadas en el constructor.

**Nota del revisor.** Confirmado en synth.cpp:716-736: tres pasadas sobre `voice.length` (generación + media, resta + pico, división), `std::sin` en double por muestra solo para Touch::Harmonic (718-719) y `std::pow` en 29, 598 y 707; se ejecuta dentro de `noteOn`, que `playTrack` llama desde el hilo de audio (playback.hpp:146). Las longitudes cuadran (~582 muestras para Mi grave a 48 kHz, ~1165 para Mi de bajo). El coste por nota (3-12 µs) y el pico de 0,5 ms son estimaciones sin medir y el caso de 120 ataques en el mismo frame es poco frecuente; impacto bajo es correcto. Sobre la propuesta: el pico calculado como max(max - media, media - min) es bit a bit idéntico al actual, porque la resta en float es monótona y simétrica. En cambio `* (1 / pico)` no da los mismos bits que `/ pico`: sigue siendo determinista, pero cambia la salida de audio respecto a la actual; para conservarla basta mantener la división en la segunda pasada (se vectoriza igual). El fasor para el armónico también cambia los valores y solo afecta a un toque raro. Las tablas de 128 entradas deben calcularse en el constructor de Synth con la misma expresión (no como static local, cuya guarda de inicialización podría ejecutarse y bloquear en el hilo de audio); su ganancia es marginal frente a las pasadas.

#### audio/A8 · El Loaded retirado no se libera mientras la reproducción está en pausa

- **Dónde:** `src/audio/src/player.cpp:161`
- **Eje:** memoria · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
retired_.store(current_, std::memory_order_release);
```

**Por qué importa.** El hilo de audio deja la canción anterior en `retired_` en su siguiente callback, y solo la libera `tidy()`, que llaman `load()` y `AudioController::pollTransport`. Tras una edición en pausa, loadSong() llama a load() y a pollTransport() antes de que el audio haya hecho el intercambio, y el temporizador se detiene (audio_controller.cpp:164-166, `if (!playing) { transportTimer_.stop(); }`). El Loaded antiguo queda vivo hasta la siguiente edición o el siguiente play: 7,7 MB de eventos más hasta 20 sintetizadores (unos 307 KB de líneas de retardo por pista pulsada: 2402 floats x 32 voces) y sus buffers, en torno a 14 MB muertos en la canción grande. Está acotado a uno, no es una fuga.

**Propuesta.** Tras `player_->load(...)`, programar un `QTimer::singleShot` (50-100 ms) que llame a `player_->tidy()`, o mantener el temporizador de transporte un tick más mientras quede algo por liberar (exponer `bool Player::hasRetired() const` leyendo `retired_`).

**Nota del revisor.** Seguido el flujo: `loadSong()` (audio_controller.cpp:81-96) hace pause, seek(0), `load()` (que llama a `tidy()` antes de publicar `pending_`, player.cpp:98-102) y `pollTransport()`, que vuelve a llamar a `tidy()` y, al no estar reproduciendo, para el temporizador (164-166). El hilo de audio hace el intercambio en su siguiente callback (player.cpp:159-164) y deja el Loaded anterior en `retired_`; nadie llama a `tidy()` hasta la siguiente edición, seek, play o stop. Queda acotado a un Loaded muerto, como dice el hallazgo. El tamaño es verosímil: `strings_` = (48000/20 + 2) x 32 voces x 4 B, unos 307 KB por pista pulsada (synth.cpp:548-549), más los eventos y buffers. Solo ocurre si ya existe `player_` (tras el primer play). Propuesta viable y segura: `tidy()` es del hilo de control. Usar `QTimer::singleShot` con `this` como contexto y comprobar `player_` dentro. Si se opta por mantener el temporizador, la condición debe incluir que `pending_` siga sin tomarse y tener un tope, porque si el dispositivo no está llamando a render() el intercambio no llega y el temporizador de 16 ms giraría indefinidamente.

#### audio/A9 · position() puede devolver la posición anterior al seek durante un bloque

- **Dónde:** `src/audio/src/player.cpp:166`
- **Eje:** concurrencia · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
if (const std::int64_t asked = seekTo_.exchange(-1, std::memory_order_acq_rel); asked >= 0) {
```

**Por qué importa.** El hilo de audio pone `seekTo_` a -1 al principio de render(), pero no publica `position_` hasta el final (línea 189, tras sintetizar todo el bloque). En esa ventana, `position()` (líneas 147-148) ve `asked == -1` y lee el `position_` antiguo: el cursor de reproducción salta un instante a la posición previa al seek, lo que puede disparar un desplazamiento o cambio de página de la partitura y su vuelta. La ventana dura lo que tarda el bloque en sintetizarse (0,2-2 ms por bloque), así que afecta a un pequeño porcentaje de los seeks sondeados por temporizador. `play()` también usa `position()` para decidir si rebobina (línea 127).

**Propuesta.** Publicar antes de limpiar: `asked = seekTo_.load()`; si `asked >= 0`, `frame_ = asked; position_.store(frame_)` y después `seekTo_.compare_exchange_strong(asked, -1)`. Si el CAS falla es que llegó un seek más nuevo, que se toma en el siguiente render.

**Nota del revisor.** Confirmado: `render()` pone `seekTo_` a -1 en la línea 166 y no publica `position_` hasta la 189, después de sintetizar todo el buffer; mientras tanto `position()` (146-150) ve -1 y devuelve el `position_` anterior al seek. También es real el efecto en `play()` (127): tras llegar al final, un seek seguido de play dentro de esa ventana vería posición >= duración y rebobinaría a 0, pisando el seek. La ventana dura lo que tarda el callback y solo la puede ver un tick del temporizador de 16 ms durante la reproducción; en pausa no se observa, porque `seek()` del controlador sondea de inmediato (aún con `seekTo_` puesto) y el temporizador se para. Que provoque un cambio de página visible no lo he verificado en el QML; impacto bajo es correcto. La propuesta es correcta y sin esperas: publicar `position_` antes de limpiar cierra la ventana (el lector solo puede ver -1 después del CAS, que va después del store), y un CAS fuerte único que falla deja el seek más nuevo para el siguiente render, igual que hoy con dos seeks consecutivos. No asigna ni bloquea.

#### audio/A2 · eventsByTrack sin reserve y eventos duplicados (FrameEvent) respecto a Sequence

- **Dónde:** `src/audio/src/playback.hpp:40`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
result[event.track].push_back({.frame = std::llround(event.time * rate),
```

**Por qué importa.** 320k push_back repartidos en 21 vectores sin reserve: cada vector (~16k elementos) se realoja decenas de veces y deja capacidad sobrante. FrameEvent ocupa 24 B (int64 + 1 + 1 + 2 + 4 + int + int), así que son 7,7 MB más la holgura, duplicando los 10,2 MB de `Sequence::events_` (NoteEvent 32 B) que siguen vivos en `sequence_`/`loaded_`. Además puede haber hasta tres Loaded a la vez (current_, pending_, retired_). Se ejecuta en el hilo principal en cada Player::load (player.cpp:73).

**Propuesta.** Mínimo: una pasada previa que cuente eventos por pista y `reserve` exacto. Mejor, junto con A1: convertir cada lista por pista con su tamaño conocido, y encoger FrameEvent a 16 B (frame uint32 o relativo, key y velocity uint8, kind y touch en 1 byte). Alternativa sin copia: que Loaded comparta un shared_ptr<const Sequence> y calcule `llround(time * rate)` al despachar (una multiplicación por evento).

**Nota del revisor.** El código existe tal cual (playback.hpp:35-50): `result(tracks)` sin reserve y push_back por evento. FrameEvent mide 24 B (8+1+1+2+4+4+4) y NoteEvent 32 B; la duplicación respecto a `Sequence::events_` es real y puede haber transitoriamente tres Loaded (current_, pending_, retired_). Se ejecuta en el hilo de control en Player::load (player.cpp:73). Pero el impacto está inflado: es una vez por carga, no por frame; los realojos geométricos copian ~2-3 veces el tamaño final (unos 20 MB de memcpy, pocos ms frente a los 28 ms de secuenciar) y la holgura media es ~25 % de 7,7 MB. Propuesta: contar + reserve es trivial y correcta. Encoger FrameEvent a 16 B exige saturar antes de estrechar: `Synth::noteOn` hace clamp de key (synth.cpp:561) y NoteEvent.key es int; truncar a uint8 antes del clamp cambiaría el resultado para claves fuera de rango, y `LoggedEvent.frame` sigue siendo int64. La alternativa con shared_ptr<const Sequence> solo es viable junto con A1 (listas por pista) y cambia la firma de `Player::load(const Sequence&)`; `llround` al despachar no asigna, así que no viola la regla del hilo de audio.

#### audio/A3 · Cada forEachIndex del secuenciador y del render offline crea y destruye sus hilos

- **Dónde:** `src/audio/src/sequence.cpp:698`
- **Eje:** concurrencia · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
core::forEachIndex(0, merged.size(),
                           core::threadsFor(0, total, kParallelEvents, merged.size()),
```

**Por qué importa.** `forEachIndex` (core/parallel.hpp:44, `workers.emplace_back([&, t] {`) lanza std::thread nuevos en cada llamada y hace join. Una Sequence de la canción grande lo invoca en las líneas 469, 638 y una vez por ronda de fusión: en 16 núcleos son 16 + 16 + 10 + 5 + 3 + 2 = 52 hilos creados por secuencia (34 en 8 núcleos). A 30-60 µs por hilo en Windows (estimado) son 1,5-3 ms de los 28 ms medidos, en cada edición. `OfflineRenderer::render` (offline_renderer.cpp:86) hace lo mismo por llamada, y la CLI lo llama una vez por segundo de audio (audio_commands.cpp:318-321): unos 16k hilos para una canción de 1000 s.

**Propuesta.** Un pool persistente en core (hw-1 trabajadores, con el hilo llamante participando) sobre el que se implemente `forEachIndex` con la misma semántica y el mismo orden determinista. En OfflineRenderer, mantener los trabajadores vivos mientras dure el renderer. Con A1 las rondas de fusión desaparecen y quedan 2 regiones paralelas por secuencia.

**Nota del revisor.** Cierto que `forEachIndex` (core/parallel.hpp:41-56) crea std::thread y hace join en cada llamada. El recuento cuadra: 469 y 638 con min(núcleos, trabajos), y rondas de 10+5+3+2 hilos (la ronda de 1 trabajo corre en línea, parallel.hpp:33). De hecho se queda corto: `core::soundingNotes` (performance.cpp:118 y 134), que el constructor llama en la línea 465, también usa forEachIndex. Aun así el coste está inflado para 'medio': los 30-60 µs por hilo son una estimación sin medir y dan 1,5-3 ms de 28 ms (5-10 %), y la creación se solapa con el trabajo de los hilos ya lanzados. En OfflineRenderer (offline_renderer.cpp:84-88) son como mucho 16 hilos por segundo de audio renderizado, despreciable frente a sintetizar 1 s de 20 pistas; además solo lo usan la CLI y los tests, la app no. Propuesta: viable pero con más riesgo del que indica. Un pool global debe admitir varios llamantes a la vez (score_document.cpp:451 y 472 lanzan apertura y maquetación con QtConcurrent mientras el hilo de UI secuencia en `edited()`), llamadas anidadas y el relanzamiento de la primera excepción en orden. El determinismo no se ve afectado porque cada trabajo escribe en su índice. Si se aplica A1 desaparecen los 20 hilos de las fusiones y la ganancia restante es mínima.

#### audio/A4 · Las pistas silenciadas (mute, o no solo) se sintetizan completas y luego se descartan

- **Dónde:** `src/audio/src/player.cpp:209`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
detail::playTrack(track.synth, track.events, track.next, frame_, out,
                              [](const detail::FrameEvent&) noexcept {});
```

**Por qué importa.** renderBlock sintetiza todas las pistas y solo después `mixInto` (playback.hpp:106) descarta las de nivel 0. Con una pista en solo sobre 20, alrededor del 95 % del trabajo del hilo de audio (noteOn con excitación de cuerda, bucles por voz, tanh del overdrive) se tira. OfflineRenderer hace lo mismo (offline_renderer.cpp:86-88), y allí los niveles son fijos durante todo el render, así que una pista a nivel 0 nunca se oirá.

**Propuesta.** En Player: si `track.applied` y `target` son ambos {0,0}, no sintetizar. Al entrar en ese estado, `synth.reset()` una vez; en cada bloque, avanzar `track.next` con `firstAt(events, frame_ + n)`. Al quitar el mute las notas ya iniciadas no suenan hasta el siguiente ataque, la misma semántica que seek() (player.hpp:59). El bloque en que cambia el nivel se sigue renderizando para la rampa. En OfflineRenderer: saltar las pistas con nivel 0 cuando `logEvents` es false.

**Nota del revisor.** Confirmado el código: `renderBlock` (player.cpp:204-217) sintetiza todas las pistas y `mixInto` (playback.hpp:105-108) descarta después las de nivel 0; OfflineRenderer hace lo mismo (offline_renderer.cpp:86-96). Impacto inflado: no mejora el peor caso (todas las pistas audibles deben caber igualmente en tiempo real), solo el consumo medio con mute/solo; con la propia estimación de A5 (unos 5 M voz-muestra/s) todo el hilo de audio ronda un pequeño porcentaje de un núcleo, y una pista sin voces activas ya es barata. No hay medida que indique cortes. La propuesta cambia el comportamiento audible y eso hay que decidirlo como producto: hoy, al quitar el mute o el solo, las notas ya iniciadas se oyen al instante con rampa; con la propuesta callan hasta el siguiente ataque. Además el nivel {0,0} no llega solo por mute/solo: `levelOf` da 0 con volumen 0 (playback.hpp:81-82), así que arrastrar un fader hasta 0 y volver (`updateMix(mixes)` en previewMix) cortaría las notas sostenidas. `firstAt` y `reset()` no asignan ni bloquean, así que es seguro en el hilo de audio. La parte offline no beneficia a nadie tal como está: el único usuario de producción es la CLI y siempre pasa `logEvents = true` (audio_commands.cpp:305). Revisar test_player.cpp:166-204, que quita el mute tras 0,7 s y exige señal.

#### audio/A5 · Bucles por muestra de las voces: estado en memoria, ramas y salida anticipada impiden vectorizar

- **Dónde:** `src/audio/src/synth.cpp:325`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
sample += value;
        if (ended(voice, value, context)) {
            return;
        }
```

**Por qué importa.** Es el coste dominante del hilo de audio. En renderPartials, renderSaw, renderString y renderDrum cada muestra escribe en `out` (float) y lee o escribe campos float de `Voice` por referencia; el compilador no puede probar que no se solapan, así que recarga y guarda todo el estado en cada muestra. Además hay ramas por muestra: `envelope()` según la etapa, `voice.age % kTidyEvery` (líneas 312 y 428), el `return` de `ended()`, y el bucle de parciales con contador variable `p < voice.partials` (línea 316) sobre arrays que ya son float[4]. Estimación: 20 pistas x ~5 voces x 48 kHz son unos 5 M voz-muestra/s en un solo hilo; en el peor caso (32 voces de piano por pista) una pista cuesta en torno al 2 % de un núcleo.

**Propuesta.** Copiar el estado caliente a variables locales al entrar y escribirlo al salir. Recorrer siempre kMaxPartials parciales (los no usados tienen amplitud y fasor a 0 tras `voice = Voice{}`) para que el bucle interno sea de 4 carriles fijos (SSE). Partir el sub-bloque en tramos sin ramas: hasta el fin del ataque (número de frames calculable desde `attackStep`), hasta el siguiente múltiplo de kTidyEvery y hasta `minimumLife`. Mantener el seguidor por muestra con min/max sin rama y cortar en el índice exacto donde termina la voz, para conservar la salida bit a bit independiente del troceo.

**Nota del revisor.** El código citado existe (synth.cpp:310-330, y bucles análogos en renderSaw 345, renderString 361 y renderDrum 426) y es cierto que `sample` (float&) puede solaparse con los campos float de `Voice` y de `Context`, lo que obliga a recargar estado, y que hay ramas por muestra (`envelope`, `age % kTidyEvery` en 312 y 428, `ended`). Lo exagerado: (1) 'impiden vectorizar' atribuye al aliasing lo que es recurrencia serie entre muestras (fasor, envolvente, seguidor, all-pass y realimentación de Karplus-Strong); el único eje vectorizable son los 4 parciales, y solo vale para Piano/Mallet/Organ, no para cuerda pulsada, que es el timbre dominante en una app de tablaturas. Para cuerda, sierra y batería la ganancia se limita a pasar estado a locales. (2) 'coste dominante' y los porcentajes son estimaciones sin medir; existen benchmarks (tests/bench/bench_audio.cpp:56-65) que no se citan. Con sus propias cifras el hilo de audio usa un pequeño porcentaje de un núcleo, así que la mejora absoluta es baja. Viabilidad: recorrer siempre kMaxPartials es correcto (tras `voice = Voice{}` en 594 los parciales no usados tienen re, im, amplitud y decay a 0 y la suma parte de +0), pero la suma de los 4 carriles debe hacerse en el mismo orden para no cambiar bits. El contrato 'la salida no depende de cómo se trocea render()' (synth.hpp:52-53) está cubierto por tests bit a bit (test_synth.cpp:249 y 399-416, test_offline_renderer.cpp:226-245): trocear por fin de ataque, múltiplos de kTidyEvery y minimumLife y cortar en el índice exacto de fin de voz es factible pero delicado; el esfuerzo M es optimista.

#### audio/A6 · NoteEvent (32 B) y PlayedNote (64 B) usan size_t e int para valores de 8-16 bits

- **Dónde:** `src/audio/include/elara/audio/sequence.hpp:32`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
std::size_t track = 0;
    std::size_t staff = 0;
    std::size_t voice = 0;
    std::size_t pass = 0;
```

**Por qué importa.** PlayedNote: 4 size_t + 2 double + 2 int + 2 bytes = 64 B, x 160k notas = 10,2 MB. NoteEvent (línea 56-64: double + 2 bytes + int16 + float + size_t track + int key + int velocity) = 32 B, x 320k = 10,2 MB. Son 20 MB por Sequence, y en cada edición conviven la antigua y la nueva (score_document.cpp:880-881) y `operator==` las recorre enteras cuando solo cambió la mezcla. El tamaño también pesa en los merges, los sort y eventsByTrack.

**Propuesta.** PlayedNote a 32 B: track uint16, staff y voice uint8, pass uint32, start y end double, key y velocity uint8, touch, struck. NoteEvent a 24 B: track uint16, key y velocity uint8. Ahorra unos 7,7 MB por Sequence (5,1 + 2,6) y reduce en proporción el tráfico de memoria de comparación, fusión y carga. Los accesores pueden seguir devolviendo size_t/int para no tocar a los llamantes.

**Nota del revisor.** Los tamaños son correctos: PlayedNote = 4 size_t + 2 double + 2 int + 2 bytes + relleno = 64 B (sequence.hpp:31-45) y NoteEvent = 32 B (56-66); las disposiciones propuestas dan 32 B y 24 B y el ahorro de 5,1 + 2,6 MB cuadra con 160k notas y 320k eventos (cifras del benchmark bench_audio.cpp:27). También es cierto que en `edited()` conviven la Sequence nueva y la antigua y que el `operator==` por defecto las recorre enteras cuando son iguales (score_document.cpp:880-881). Impacto inflado: son unos 7,7 MB en una app de escritorio y un 25-50 % menos de tráfico en pasos que no dominan los 28 ms. Dos errores en la propuesta: (1) no hay 'accesores' que puedan seguir devolviendo size_t/int: son structs con campos públicos que leen directamente la CLI (audio_commands.cpp:223-240, 270-281), offline_renderer.cpp:31-38, los tests y los inicializadores designados de sequence.cpp:522-531, donde size_t a uint16/uint8 es conversión con estrechamiento y no compila sin casts. (2) `key` puede estar fuera de 0..127: el secuenciador lo filtra explícitamente (sequence.cpp:126, 150, 657 y harmonicKey en 387), así que un uint8 necesita saturación o centinela para no cambiar qué notas generan eventos. Viable con esas correcciones y comprobando los límites (pistas < 65536, pass en uint32).

**Observaciones del área (qué está bien resuelto).**

Auditoría de solo lectura: no se ha compilado ni medido nada. Que la Sequence de cada edición se construya en el hilo principal (score_document.cpp:880) no lo he comprobado; que Player::load corre en él sí está documentado en player.hpp:23.
Bien resuelto, no tocar:
- Traspaso de canción al hilo de audio (player.cpp): punteros atómicos pending_/retired_, niveles en std::atomic<float>, y render() sin asignaciones ni locks. No he encontrado carreras de datos ni punteros colgantes en latest_.
- Callbacks de JUCE (system_audio.cpp, microphone_input.cpp): buffers dimensionados en audioDeviceAboutToStart, sin asignar ni bloquear; setSource solo con el callback retirado.
- SampleRing: SPSC lock-free correcto (acquire/release, máscara potencia de dos). written_, read_ y dropped_ comparten línea de caché, pero el productor escribe unas 100 veces por segundo: el false sharing es irrelevante.
- Synth: sin asignaciones tras construir (strings_ y scratch_ preasignados), mixInto con rampa sin clics, limit() con tanh solo por encima de la rodilla.
- Un solo hilo de audio para todas las pistas es suficiente: la carga típica estimada es de pocos puntos porcentuales de un núcleo. No recomiendo paralelizar el callback.
Observaciones menores que no entran en los 9 hallazgos:
- Denormales: no hay ScopedNoDenormals ni FTZ/DAZ en ningún callback. El sintetizador lo cubre a mano (kTiny, flushed(), fin de voz a kSilence). El único resto que he visto es `voice.toneDelta *= voice.toneSweep` (synth.cpp:437), que nunca se limpia y puede entrar en subnormales al final de un plato chino (tecla 52), unas decenas de ms. Poner FTZ/DAZ al inicio del callback (system_audio.cpp:49) lo haría robusto y permitiría quitar el `flushed` por muestra de synth.cpp:882.
- Overdrive: `std::tanh` por muestra (synth.cpp:881) también con la pista en silencio y en pausa; estimo 1-2 ms por segundo y pista. Bastaría saltarlo cuando no hay voces activas y toneState_ es 0.
- Afinador: YIN acumula en double sobre muestras float (pitch_detector.cpp:29-36), lo que reduce a la mitad el ancho SIMD, y se ejecuta desde el temporizador de TunerController::poll (tuner_controller.cpp:177). Peor caso por detección: 1601 retardos x 1600 muestras, unas 30 detecciones por segundo. Tuner::push desplaza la ventana entera (3201 floats) en cada push (tuner.cpp:35): despreciable.
- OfflineRenderer::renderRest (offline_renderer.cpp:118) reserva pistas x frames x 4 B, más mix_ y out: del orden de 4 GB para 1000 s y 21 pistas. Hoy solo lo usan los tests; la CLI renderiza en bloques de 1 s. Si se usa para exportar, debería trocear internamente.
- Constructor de Sequence: sounding, timings, articulations, writtenAs, head y ends siguen vivos durante la fusión, con un pico transitorio de decenas de MB; se pueden liberar antes de la fase de eventos.
Ficheros principales revisados:
- src/audio/src/player.cpp
- src/audio/src/playback.hpp
- src/audio/src/synth.cpp
- src/audio/src/sequence.cpp
- src/audio/include/elara/audio/sequence.hpp
- src/audio/src/offline_renderer.cpp
- src/audio/src/pitch_detector.cpp
- src/audio/src/tuner.cpp
- src/audio/src/sample_ring.cpp
- src/audio/backend_juce/src/system_audio.cpp
- src/audio/backend_juce/src/microphone_input.cpp
- src/core/include/elara/core/parallel.hpp (forEachIndex)
- src/app/src/audio_controller.cpp y score_document.cpp (solo los puntos de llamada)

### IO: MusicXML y contenedores

#### io-xml/X1 · Importar MusicXML: el parseo de pugixml es de un solo hilo y ya es ~85% del tiempo

- **Dónde:** `src/io/src/musicxml.cpp:1994`
- **Eje:** velocidad · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** L · **Veredicto:** parcial

```cpp
const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size());
```

**Por qué importa.** docs/performance.md deja la importación de la canción grande (33 MB) en 118 ms y anota que 'pugixml alone parses the text in about 100 ms' y que 'what is left is mostly the XML parse'. Todo el documento se parsea en un único xml_document antes de repartir las partes entre hilos (importScore, línea 1917), así que la parte ya paralelizada es solo ~15-20 ms y el resto es serie (Amdahl). Además el DOM completo sigue vivo hasta el final de importScore.

**Propuesta.** Trocear el texto por los elementos <part ...>...</part> de primer nivel (búsqueda lineal de '<part ' / '</part>'; si aparece CDATA o un comentario que contenga '<part', caer al camino actual) y parsear cada tramo en su propio pugi::xml_document dentro del mismo job que ya lee la parte (parseo + layoutPart + PartReader::read). La cabecera (part-list, identification, credit) se parsea aparte con parse_fragment. La parte 0 sigue yendo primero para escribir ScoreState, pero su parseo puede solaparse con el de las demás. Con load_buffer_inplace sobre rangos disjuntos no hay escrituras compartidas. Estimación: ~100 ms de parseo pasan a ~100/N + la parte mayor (20 partes iguales, 8 núcleos: ~15 ms), importación total del orden de 35-45 ms; y cada DOM de parte puede liberarse al terminar su lector, bajando el pico.

**Nota del revisor.** Comprobado: musicxml.cpp:1994 hace un único document.load_buffer sobre todo el texto antes de importScore; readPart(0) va en serie (1912) y solo las partes 1..N-1 pasan por forEachIndex (1917). El DOM y el texto siguen vivos hasta que importMusicXml retorna. docs/performance.md (filas 52-54) da 265 -> 238 -> 118 ms y dice 'pugixml alone parses the text in about 100 ms' y 'what is left is mostly the XML parse', así que el ~85 % es coherente (238-100 = 138 ms de lectura en un hilo, repartidos dan ~18 ms). Impacto rebajado: en la app la importación corre en un hilo de trabajo (score_document.cpp:451, QtConcurrent::run -> openAndLayOut -> io::openFile), no bloquea la UI, es una vez por apertura y el caso es un fichero extremo de 33 MB; pasar de ~118 a ~50 ms es latencia de carga, no fluidez. La propuesta es viable pero con más aristas de las que cuenta: (1) el job de una parte i>=1 no puede 'parsear y leer' en el mismo paso porque la lectura necesita el ScoreState que escribe la parte 0: hace falta una fase de parseo en paralelo de todos los tramos y luego la lectura actual; (2) la cabecera troceada queda con <score-partwise> sin cerrar y pugixml da status_end_element_mismatch aunque se use parse_fragment: hay que copiarla y añadirle el cierre; (3) load_buffer autodetecta codificación (UTF-16, latin1 por declaración): el troceo por bytes ASCII solo vale para UTF-8, el resto debe caer al camino actual; (4) '<part ' no cubre '<part\n', '<part\t' ni '<part>' y hay que validar que tras el último </part> solo queda el cierre; (5) el mensaje 'invalid XML at byte {}' necesita el offset rebasado y elegir el error de menor offset para no depender de los hilos; (6) un documento mal anidado entre tramos podría aceptarse donde hoy se rechaza; (7) load_buffer_inplace exige buffer mutable y la API pública recibe string_view const: sin X4 cada tramo tendría que usar load_buffer (copia por tramo, en paralelo). Estimación de 35-45 ms optimista: el parseo de pugixml está limitado por memoria y no escala lineal; razonable 45-60 ms. Esfuerzo L sobre una ruta de entrada no fiable.

#### io-xml/X2 · Exportar .mxl: deflate monohilo sobre el texto entero, más dos copias del archivo resultante

- **Dónde:** `src/io/src/archive.cpp:73`
- **Eje:** velocidad · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** L · **Veredicto:** parcial

```cpp
written = written && mz_zip_writer_add_mem(&zip, name.c_str(), entry.content.data(),
```

**Por qué importa.** El XML se genera en 88 ms con todos los núcleos, pero el .mxl tarda 391 ms: ~300 ms son un único mz_zip_writer_add_mem (nivel 6) en un hilo sobre ~60 MB de texto compacto. Para comprimir, antes hay que concatenar todas las partes en un solo std::string (musicxml_export.cpp:1731), y al terminar el archivo se copia dos veces: bytes.assign(begin, begin + size) (archive.cpp:82) y result.data.assign(bytes.begin(), bytes.end()) (musicxml_export.cpp:1747), con el texto sin comprimir todavía vivo. El heap inicial de miniz se pide como total / 4 + 4096 (línea 66): ~15 MB para un resultado de 566 KB.

**Propuesta.** Comprimir por trozos en los mismos hilos que escriben cada parte (técnica pigz): cada job hace deflate crudo de su texto con tdefl y termina con TDEFL_SYNC_FLUSH (el último trozo con FINISH), calcula su mz_crc32 y libera el texto; después se concatenan los flujos en orden de partitura, se combina el CRC (crc32_combine, ~30 líneas, miniz no lo trae) y se añade la entrada ya comprimida con mz_zip_writer_add_mem_ex(..., MZ_ZIP_FLAG_COMPRESSED_DATA, uncomp_size, crc). El troceo es por parte, no por hilo, así que los bytes siguen siendo los mismos con cualquier número de hilos. Estimación: ~300 ms de deflate pasan a ~300/N (8 núcleos: ~40-50 ms), .mxl total ~130 ms; desaparece la concatenación de ~60 MB y el pico baja a la suma de trozos comprimidos + los textos en curso. De paso, que writeZip devuelva el buffer de miniz sin copiarlo (o escriba directo al fichero con el callback m_pWrite) y que ExportResult no exija la segunda copia a std::string. El mismo writeZip sirve a encodeNative (.elara).

**Nota del revisor.** Comprobado: archive.cpp:73 llama a mz_zip_writer_add_mem una vez por entrada, en serie, con level 6 por defecto (archive.hpp:57); el XML compacto entero se concatena antes (musicxml_export.cpp:1730-1732) y se pasa como una sola entrada (1745). performance.md fila 58: 391 ms frente a 88 ms del XML indentado en paralelo, luego ~300 ms son deflate + CRC en un hilo: la afirmación principal es cierta. Partes infladas: las 'dos copias del archivo' (archive.cpp:82 y musicxml_export.cpp:1747) copian el archivo YA comprimido, 566 KB en el benchmark: coste despreciable; y el heap inicial total/4+4096 (~15 MB) es una reserva que no se toca, además razonable porque el propio doc avisa de que las canciones reales comprimen mucho menos. Alcance: exportMusicXml/exportMusicXmlFile solo se llaman desde el CLI (src/cli/src/file_commands.cpp:242); la app no exporta MusicXML (en src/app/src solo aparecen io::openFile y io::saveNative), así que hoy no hay ruta interactiva que pague los 391 ms: impacto medio, no alto. Propuesta técnicamente correcta (trozos deflate crudo independientes terminados en SYNC_FLUSH y el último en FINISH, concatenados; crc32_combine; mz_zip_writer_add_mem_ex con MZ_ZIP_FLAG_COMPRESSED_DATA), con matices: miniz viene por vcpkg y no está en el árbol, así que la firma/flag no se ha podido verificar en el repo; para que los bytes sean iguales con cualquier número de hilos el camino threads<=1 (1716-1720, que hoy escribe directo en xml) debe trocear exactamente igual, y no hay test que compare bytes de .mxl entre hilos (el de test_musicxml_export.cpp:728 solo cubre texto sin comprimir); cada job necesita su tdefl_compressor (~300 KB) en heap, no en pila. No sirve tal cual para encodeNative: .elara es una sola entrada song.json sin partes, ahí haría falta troceo por tamaño fijo.

#### io-xml/X6 · forEachIndex crea y destruye hilos en cada llamada y el hilo llamante no trabaja

- **Dónde:** `src/core/include/elara/core/parallel.hpp:44`
- **Eje:** concurrencia · **Impacto:** medio · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
workers.emplace_back([&, t] {
```

**Por qué importa.** io/parallel.hpp no duplica nada: solo hace 'using core::forEachIndex; using core::threadsFor;'. El coste está en core: cada llamada lanza 'threads' std::thread nuevos (hasta hardware_concurrency, tope 64) y el llamante se queda bloqueado en join, es decir N+1 hilos para N de trabajo. Crear y unir un hilo en Windows cuesta del orden de 50-100 us, 1-3 ms por llamada en una máquina de 16-32 hilos: irrelevante en una importación de 118 ms, pero la misma función se usa en 26 puntos (io, audio/sequence, notation/page_layout y system_layout) con operaciones de 3-30 ms, y al no haber pool común dos operaciones simultáneas (p. ej. abrir en el hilo de trabajo de la app mientras se maqueta o pinta) suman cada una todos los núcleos.

**Propuesta.** Paso barato: lanzar threads-1 hilos y ejecutar el mismo bucle 'next++' en el hilo llamante (misma semántica y mismo orden de errores). Paso completo: un pool perezoso compartido en core (hilos = hardware_concurrency, creado una vez) al que forEachIndex encola el bucle, con el llamante participando; así se elimina la creación por llamada y se acota la sobre-suscripción entre módulos. El reparto por índice atómico (una parte por job) es adecuado y no hay que tocarlo.

**Nota del revisor.** Comprobado: forEachIndex (core/parallel.hpp:39-56) crea 'threads' std::thread por llamada, el llamante solo hace join (N+1 hilos) y io/parallel.hpp son dos using. Corrección: no son 26 puntos; hay 13 llamadas a forEachIndex en src (io 5, audio/sequence 3, audio/offline_renderer 1, core/performance 2, notation 2); 28 es el recuento textual de forEachIndex|threadsFor incluyendo README, definición y using. Algunas están en bucle (sequence.cpp:696-711, una llamada por ronda de mezcla), así que construir una Sequence lanza del orden de 60-100 hilos, y maquetar un pentagrama de 500 compases lanza ~16 (system_layout.cpp:593) + ~24 (page_layout.cpp:298) para una operación de 3,5 ms. Eso hace plausible un peso apreciable ahí (el doc da 9,4 ms en un hilo frente a 3,4-3,6 ms en 12 núcleos, solo 2,7x), pero el coste por hilo (50-100 us) es una estimación sin medir en el repo y parte del arranque corre en el hilo nuevo, no en el llamante. La afirmación de sobre-suscripción entre operaciones simultáneas es razonable (la app usa QtConcurrent para abrir, maquetar y pintar) pero no demostrada. Ninguna llamada está en el hilo de audio de tiempo real (offline_renderer es render offline), así que la regla de no asignar ni bloquear no se ve afectada. Paso barato (threads-1 hilos y el llamante trabajando) correcto y sin riesgo. Pool compartido viable con condiciones: debe admitir varios llamantes concurrentes, no bloquearse si alguna vez se anida, respetar el argumento 'threads' explícito que usan los tests de determinismo ({1,2,3,8}), mantener 'primera excepción en orden' y cuidar la destrucción estática; jamás debe llamarse desde el callback de audio. Medir antes de adoptar el pool.

#### io-xml/X3 · Exportar MusicXML en paralelo: la unión final copia los 124 MB y mantiene dos copias vivas

- **Dónde:** `src/io/src/musicxml_export.cpp:1731`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
for (const std::string& text : texts) {
            xml += text;
        }
```

**Por qué importa.** En el camino con hilos cada parte se escribe en texts[i] y luego se copia entera a xml; texts no se libera hasta salir del bloque, así que al acabar el bucle conviven ~124 MB (texts) + ~124 MB (xml) = ~248 MB tocados para un fichero de 124 MB. La copia es un memcpy de 124 MB sobre páginas recién reservadas (xml.reserve en la línea 1703): del orden de 15-30 ms entre copia y fallos de página, frente a los 88 ms totales de la exportación, y es serie.

**Propuesta.** Mínimo (S): liberar cada trozo en cuanto se copia (std::string().swap(texts[i]) dentro del bucle), con lo que el pico queda en ~124 MB + una parte. Mejor (M): no concatenar para el camino a fichero: exportMusicXmlFile escribe en el .saving la cabecera, cada texts[i] en orden y el cierre con out.write sucesivos (writeFileAtomically aceptando un span de string_view), eliminando el memcpy de 124 MB y una de las dos copias; exportMusicXml (API en memoria) puede seguir concatenando.

**Nota del revisor.** Comprobado: en el camino con hilos cada parte se escribe en texts[i] (1724-1729) y luego se copia a xml con xml += text (1730-1732); texts vive hasta la llave de 1733 y xml está reservado entero en 1703, así que coexisten las dos copias (~124 + ~124 MB en el benchmark). La copia es serie. El coste de 15-30 ms no está medido, pero es verosímil y hasta conservador: la exportación solo baja de 155 a 88 ms con 12 núcleos, señal de que lo que queda es serie y de memoria (fallos de página sobre 124 MB recién reservados). Impacto rebajado a bajo por alcance: la exportación MusicXML solo se invoca desde el CLI (file_commands.cpp:242), no desde la app, y el pico de 248 MB es transitorio y solo en una canción extrema (20 pistas x 500 compases con TAB). Propuesta correcta: liberar texts[i] tras copiarlo es de una línea y deja el pico residente en ~124 MB + una parte; la variante M (escribir cabecera, trozos y cierre con write sucesivos en el .saving) es la única que elimina el memcpy, obliga a un camino interno que devuelva los trozos porque ExportResult.data es un std::string, y no aplica al camino .mxl, que necesita el texto contiguo para writeZip salvo que se haga X2.

#### io-xml/X4 · Importar: load_buffer duplica el texto aunque el buffer ya es propio y desechable

- **Dónde:** `src/io/src/musicxml.cpp:2026`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
[&options](const std::string& xml) { return importMusicXml(xml, options); });
```

**Por qué importa.** pugi::xml_document::load_buffer trabaja sobre un buffer inmutable y se hace una copia privada. En los dos caminos a fichero el buffer es un temporal propio: el std::string extraído del .mxl (línea 2026) o el vector de readFile (línea 2034, 'const std::vector<std::uint8_t>& bytes'). En la canción grande conviven 33 MB del fichero + 33 MB de copia de pugixml + el DOM (varias veces el texto) + el modelo; la copia cuesta además una reserva y un memcpy de 33 MB (unos ms). Con el límite de entrada de 512 MB (kMaxEntryBytes) un .mxl hostil duplica hasta 1 GB solo en texto.

**Propuesta.** Añadir un camino interno importMusicXmlOwned(char* datos, size_t n) que use document.load_buffer_inplace(datos, n) y llamarlo desde importMusicXmlFile (tomando el vector por valor/mutable en el and_then) y desde el camino .mxl (el std::string devuelto por mxlScore ya es mutable). La API pública importMusicXml(std::string_view) conserva load_buffer. Ahorra 33 MB de pico y la copia; combina con X1 (parseo in situ por tramos).

**Nota del revisor.** Comprobado: importMusicXml (1994) usa load_buffer, que hace copia privada del texto; en el camino .mxl el std::string que devuelve mxlScore es un temporal propio (2025-2026) y en el camino a fichero el vector de readFile también (2034), ambos recibidos por const&. Conviven fichero + copia de pugixml + DOM. Impacto rebajado: son 33 MB transitorios sobre un pico dominado por el DOM (varias veces el texto) y unos pocos ms (reserva + memcpy + fallos de página, quizá 5-10 ms de 118); en el caso hostil de 512 MB el DOM pesaría varios GB, la copia es lo de menos. Propuesta viable y correcta: load_buffer_inplace con un buffer que sobrevive al documento, que aquí se destruye antes de retornar; and_then sobre un expected rvalue permite recibir el valor por valor o &&. Matices: hay que reestructurar importMusicXmlFile, que hoy pasa por importMusicXmlBytes(span const); solo ahorra la copia si el texto es UTF-8 (en otra codificación pugixml convierte a un buffer nuevo igualmente, sigue siendo correcto); y el benchmark actual (bench_io.cpp:105) llama a importMusicXml(string), que conservaría load_buffer, así que la mejora no se verá en esa fila salvo que se añada una medición por fichero.

#### io-xml/X7 · Los buffers de lectura se rellenan a cero antes de sobrescribirlos enteros

- **Dónde:** `src/io/src/archive.cpp:40`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
std::string content(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
```

**Por qué importa.** ZipReader::entry pone a cero todo el destino y acto seguido mz_zip_reader_extract_to_mem lo sobrescribe; readFile hace lo mismo con 'std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));' (línea 127) antes de in.read. Es una pasada extra de memoria por fichero: para los 33 MB de MusicXML unos 3-6 ms (escritura + fallos de página adelantados), y lo pagan todos los formatos (.elara, .gp, .mxl, MIDI) porque ambas funciones son comunes.

**Propuesta.** En entry(): content.resize_and_overwrite(n, [&](char* p, std::size_t n) { ...extract_to_mem(p, n)...; return n; }) (C++23, ya es el estándar del repo). En readFile(): devolver un buffer sin inicializar (std::string con resize_and_overwrite o std::make_unique_for_overwrite<std::uint8_t[]> + tamaño); de paso ese buffer mutable es el que necesita X4 para load_buffer_inplace.

**Nota del revisor.** Comprobado: archive.cpp:40 construye std::string(n, '\0') y la línea 41 lo sobrescribe entero con mz_zip_reader_extract_to_mem; readFile (127) crea vector<uint8_t>(size) puesto a cero antes de in.read (129). Es una pasada extra real, pero pequeña: el memset de 33 MB son ~2-4 ms (los fallos de página se pagarían igual al leer), algo menos que los 3-6 ms citados, y no entra en la fila de 118 ms porque el benchmark importa desde un string en memoria. Corrección de alcance: readFile solo lo usan MusicXML (musicxml.cpp:2034) y .elara (native_format.cpp:998); Guitar Pro (guitar_pro.cpp:1445-1450) y MIDI (midi_import.cpp:1283-1288) leen con istreambuf_iterator, byte a byte, que es peor y no está en el hallazgo. ZipReader::entry sí es común a .elara, .mxl y .gp; MIDI no usa ninguna de las dos. Propuesta viable (C++23 fijado en CMakeLists.txt:9) con una condición: la operación de resize_and_overwrite no puede lanzar (precondición del estándar), así que el fallo de extracción debe guardarse en una variable, devolver 0 y lanzar FormatError fuera de la lambda. Cambiar el tipo de retorno de readFile toca a sus dos llamantes; devolver std::string con resize_and_overwrite es lo más simple y sirve a X4.

#### io-xml/X9 · std::format con asignación por nota de percusión (y por bend/tempo) en el escritor

- **Dónde:** `src/io/src/musicxml_export.cpp:1332`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
xml.attribute("id", std::format("{}-I{}", id_, midi + 1));
```

**Por qué importa.** El resto del XmlWriter escribe con std::to_chars y append directo al buffer, sin temporales; este es el único punto del camino por nota que construye un std::string con std::format (id de 6-8 caracteres, cabe en SSO pero paga el formateo, del orden de 100-200 ns por nota). Lo mismo, por elemento menos frecuente, con decimal() = std::format("{}", value) en bend-alter (líneas 1519 y 1526) y per-minute (1031). En una pista de batería de 8k-16k notas son 1-3 ms dentro de un job que ronda los 4-8 ms por parte: puede ser la parte más lenta y por tanto la que fija el tiempo de pared.

**Propuesta.** Escribir el atributo por piezas: out_ += id_; out_ += "-I"; appendNumber(midi + 1) (un método attributeId(prefix, number) en XmlWriter), o precalcular en PartWriter una tabla de ids por tecla de batería (drumKeys_ ya existe). Para decimal(), añadir a XmlWriter un endDecimal/textDecimal con std::to_chars(double) sobre un buffer de pila.

**Nota del revisor.** Comprobado: musicxml_export.cpp:1332 llama a std::format("{}-I{}", id_, midi + 1) por cada nota cuando percussion_ es cierto; el resto del XmlWriter usa std::to_chars y append (136-140). decimal() = std::format("{}", value) (183-185) se usa en bend-alter (1519, 1526), per-minute (1031, 1041) y volume/pan (512, 515), por elemento poco frecuente. Matices: el benchmark es de 20 guitarras, sin percusión, así que este coste no aparece en ninguna cifra medida; la afirmación de que la pista de batería 'puede fijar el tiempo de pared' es especulativa, porque una batería escribe una sola pauta y una guitarra dos (pauta + TAB, ~7,7 ms por parte en un hilo), de modo que con 100-200 ns extra por nota seguiría por debajo de una guitarra con las mismas notas. Además la exportación solo se invoca desde el CLI. Propuesta correcta y sin cambio de bytes: id_ es "P{n}" y no necesita escape, y std::to_chars(double) en forma más corta produce exactamente el mismo texto que std::format("{}", double) por especificación, así que el round-trip y el determinismo se conservan. Ganancia de 1-2 ms en una pista de batería grande: bajo.

#### io-xml/X5 · Exportar: las reservas no tienen en cuenta el modo compacto ni las pistas sin TAB (hasta 2x de más)

- **Dónde:** `src/io/src/musicxml_export.cpp:1726`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
texts[i].reserve(songScan.work[i] * kBytesPerItem * 2);
```

**Por qué importa.** kBytesPerItem = 260 está medido sobre el texto indentado con pauta + TAB (480k ítems x 260 = 124 MB). Pero la reserva por hilo multiplica siempre por 2 aunque la pista no lleve TAB (el total de la línea 1699 sí distingue 'tab ? 2 : 1'), y ni esta ni xml.reserve(work * kBytesPerItem + 4096) (línea 1703) miran options.compressed, cuando el XML compacto ocupa la mitad ('halves what deflate reads'). En .mxl se reservan ~124 MB + ~124 MB para escribir ~60 MB + ~60 MB; en Windows las reservas grandes del heap cuentan como memoria comprometida aunque no se toquen. Además xml se reserva entero aunque en el camino paralelo solo recibe la cabecera hasta la unión.

**Propuesta.** Calcular bytesPorItem según el modo (p. ej. 260 indentado, ~130 compacto, medido igual que el actual) y usar el mismo factor 'tab ? 2 : 1' por pista en texts[i].reserve. En el camino paralelo, reservar xml justo antes de unir con la suma exacta de texts[i].size() (o no reservarlo si se aplica X3/X2).

**Nota del revisor.** Comprobado: 1726 reserva work[i]*260*2 siempre, mientras el total de 1699 sí usa 'tab ? 2 : 1'; ni esa reserva ni xml.reserve (1703) miran options.compressed; SongScan.work es beats + notas (392), y 240k ítems x 2 x 260 = 124,8 MB cuadra con el texto indentado. xml se reserva entero antes de saber si irá por el camino paralelo. Pero el coste real es casi nulo: reserve no toca las páginas; en Windows una reserva grande solo suma carga de commit, no memoria residente ni tiempo. En el benchmark todas las pistas llevan TAB, así que el x2 por pista es exacto ahí; el exceso solo aparece en modo compacto (~2x de reserva virtual) y en pistas sin TAB. Además es ruta solo de CLI. Riesgo de la propuesta: quedarse corto es peor que pasarse (realocación y copia de cadenas de decenas de MB con crecimiento 1,5x en MSVC), así que el factor compacto debe medirse y redondearse hacia arriba. Reservar xml justo antes de unir con la suma exacta de tamaños es correcto y sin riesgo.

#### io-xml/X8 · layoutPart recorre todas las notas de cada parte afinada solo para descartar <unpitched>

- **Dónde:** `src/io/src/musicxml.cpp:482`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
unpitched = std::any_of(notes.begin(), notes.end(), [](pugi::xml_node note) {
                return static_cast<bool>(note.child("unpitched"));
```

**Por qué importa.** El corte '!unpitched' solo ahorra trabajo en partes de percusión. En una parte afinada (casi todas) el pre-pase visita todos los <note> de todos los compases y, en cada uno, child("unpitched") compara el nombre de todos sus hijos sin encontrarlo: es una segunda pasada completa por el DOM, justo lo que scanNote evitó en la lectura. En la canción grande son ~320k <note> (pauta + TAB) x ~10-15 comparaciones de cadena; del orden de 10-30 ms de CPU repartidos entre hilos, y en serie para la parte 0.

**Propuesta.** Decidir la percusión sin recorrer notas cuando ya hay señal suficiente: clave 'percussion' en los <attributes> (ya se lee en el mismo bucle) o midi-unpitched / canal 10 en el <score-part> (readPartList ya lo lee); solo si no hay señal, mirar las notas y parar en el primer compás con notas afinadas (<pitch>) o en la primera <unpitched>. Alternativa sin cambiar semántica: que el pre-pase use la misma iteración de hijos de scanNote y corte en cuanto vea <pitch> o <unpitched> en cada nota en lugar de recorrerlos todos.

**Nota del revisor.** Comprobado: layoutPart (479-485) recorre, mientras unpitched sea falso, todos los <note> de cada compás y llama a note.child("unpitched"), que es un barrido lineal de hijos; en una parte afinada nunca corta, y scanNote (747) documenta justo ese coste. Cifras infladas unas 4x: el fichero del benchmark de importación (bench_io.cpp:26-72) tiene 160k <note> (20 x 500 x 16, una sola pauta con string/fret en <technical>), no 320k, y cada nota tiene 5-6 hijos (chord?, pitch, duration, voice, type, notations), no 10-15; además ningún nombre empieza por 'u', así que cada comparación falla en el primer carácter. Queda del orden de 1M de comparaciones baratas: unos pocos ms de CPU repartidos entre hilos, ~1 ms de pared como mucho (la parte 0 en serie, décimas). Impacto bajo, casi despreciable. Sobre la propuesta: saltarse el barrido cuando ya se ha visto una clave 'percussion' es seguro (unpitched solo alimenta 'percussion', 493-497); en cambio parar en el primer compás con <pitch> o fiarse de midi-unpitched/canal 10 CAMBIA la semántica (una parte con notas afinadas al principio y <unpitched> después hoy es percusión) y podría alterar resultados de importación. La alternativa sin cambio de semántica (recorrer los hijos y cortar en el primer pitch, unpitched o rest de cada nota) es la correcta; hay que incluir <rest> en el corte.

**Observaciones del área (qué está bien resuelto).**

Leído: src/io/src/musicxml.cpp (lector de partes, build, importScore, mxlScore y entradas públicas), musicxml_export.cpp (XmlWriter, scan, exportMusicXml/File), musicxml_shared.hpp, archive.cpp/.hpp, open.cpp, parallel.hpp, warnings.hpp (por grep) y core/parallel.hpp.
Bien resuelto, no tocar:
- open.cpp: detección solo por extensión (minúsculas), una única lectura del fichero, sin sondeos repetidos; el resultado se mueve.
- io/parallel.hpp NO es un duplicado: son dos 'using' sobre core::forEachIndex/threadsFor. El umbral de paralelismo (1 MiB de texto al importar, 20.000 ítems al exportar) evita hilos en ficheros pequeños.
- Sin carreras visibles en importScore: la parte 0 se lee antes de lanzar hilos y es la única que escribe ScoreState; warnings[i] y readers[i] son por índice y se fusionan en orden de partitura.
- XmlWriter: to_chars + append directo, escape por tramos, sin ostringstream ni std::format en el camino caliente (salvo X9). Lector: scanNote en una pasada, buffers por compás reutilizados, reserve de beats y acordes.
- Nivel de deflate 6 y XML compacto en archivos: ya medido (nivel 1 ahorra 40 ms y da 5,5 MB frente a 566 KB); no cambiar el nivel, sino paralelizar (X2).
- Guarda anti zip-bomb (kMaxEntryBytes y límite de 1 MiB para container.xml), escritura atómica con .saving + rename, mensajes std::format solo en caminos de error.
- En la app, openFile se llama desde openAndLayOut (score_document.cpp:149), documentado como hilo de trabajo: la importación no bloquea el hilo de UI.
Cuántas copias conviven al exportar (respuesta directa a la pregunta): sin comprimir y con hilos, 2 copias del texto (texts + xml, ~248 MB) hasta salir del bloque y 1 al escribir; en .mxl, 2 copias del texto compacto (~60 MB cada una) durante la unión, 1 durante deflate, más 3 copias sucesivas del archivo comprimido (heap de miniz, vector de writeZip, std::string de ExportResult; dos a la vez como máximo). Al importar: fichero (33 MB) + copia interna de pugixml (33 MB) + DOM + modelo; en .mxl se suma el zip original.

### IO: Guitar Pro

#### io-gp/GP3 · DOM con flags por defecto: un nodo PCDATA aparte por cada hoja de texto del GPIF

- **Dónde:** `src/io/src/guitar_pro.cpp:1354`
- **Eje:** memoria · **Impacto:** medio · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
pugi::xml_document document;
```

**Por qué importa.** GPIF es casi todo hojas del tipo `<Fret>0</Fret>`, `<String>0</String>`, `<Number>40</Number>`, `<Step>E</Step>`: con parse_default cada una cuesta dos nodos (elemento + PCDATA hijo). Con 160k notas y varias hojas por nota son del orden de un millón de nodos extra (decenas de MB de DOM y sus reservas en el parseo serie). El DOM vive hasta el final de importScore, a la vez que la Song ya construida.

**Propuesta.** Parsear con `pugi::parse_default | pugi::parse_embed_pcdata`: el texto se guarda en el propio elemento y se ahorra el nodo hijo. El lector accede al texto siempre con `node.text().get()` / `text().as_double()` (textOf, línea 74), que es compatible con ese flag; validar con los tests de importación existentes. Opcionalmente quitar parse_eol/parse_wconv_attribute si no se necesitan.

**Nota del revisor.** Comprobado: `pugi::xml_document document;` (1354) + load_buffer sin opciones = parse_default, que crea un nodo PCDATA por cada hoja con texto. En el GPIF de referencia (bench_io.cpp:182-240) son ~590k hojas (3 por nota x 160k, Notes por beat, Beats por voz, Clef/Voices por compás) x 64 B de xml_node_struct = ~38 MB de DOM menos, no 'un millón' pero mismo orden; en ficheros reales hay más hojas por nota. Ataca además lo que performance.md:67 identifica como coste dominante restante (el parseo XML). Compatibilidad verificada: todo el texto se lee con textOf() = node.text().get() (línea 74) o text().as_double (434, 936); no hay usos de child_value(), first_child() ni node.value() en el fichero (solo attribute().value()), y BeatFields/NoteFields iteran hijos por nombre de elemento. CDATA (nombres, título) no se ve afectado. Matices: (1) la parte opcional de quitar parse_eol/parse_wconv_attribute ya se midió y 'gained nothing' (performance.md:67), además cambiaría el texto multilínea: descartarla; (2) es memoria transitoria de una importación en hilo de trabajo, no coste por edición; (3) no he podido comprobar la versión de pugixml instalada (viene de vcpkg; el flag existe desde 1.8). Sin efecto en determinismo ni hilos.

#### io-gp/GP1 · pugixml copia el GPIF entero: load_buffer en vez de load_buffer_inplace sobre el std::string que ya es propio

- **Dónde:** `src/io/src/guitar_pro.cpp:1355`
- **Eje:** memoria · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
const pugi::xml_parse_result parsed = document.load_buffer(xml.data(), xml.size());
```

**Por qué importa.** load_buffer reserva un buffer interno y hace memcpy del XML completo antes de parsear. El llamante ya tiene el texto en un std::string propio y mutable (`score`, líneas 1399 y 1418) que no se vuelve a usar, así que durante toda la importación conviven dos copias del GPIF (N MB + N MB) más el DOM y la Song. Para un GPIF de decenas de MB son decenas de MB de pico y un memcpy + una reserva grande evitables en la fase serie (la que no escala con hilos).

**Propuesta.** Hacer que importScore reciba el std::string por valor/rvalue (`importScore(std::move(*score), parts, options)`) y parsear con `document.load_buffer_inplace(score.data(), score.size())` manteniendo el string vivo junto al documento. importGpif(std::string_view) puede conservar load_buffer porque no es dueño del buffer.

**Nota del revisor.** Comprobado: guitar_pro.cpp:1355 usa `document.load_buffer(xml.data(), xml.size())` y los llamantes de las ramas zip (1399-1412) y gpx (1418-1431) son dueños de un `std::optional<std::string> score` mutable que no se reutiliza; load_buffer copia el buffer completo. Hecho cierto, impacto inflado: (a) la importación corre en un hilo de trabajo (score_document.cpp:147-154, 'Runs on a worker thread'), una vez por apertura, no por edición ni por frame; (b) docs/performance.md:67 mide 125 ms para el GPIF de 39 MB y dice que lo que queda es el parseo XML: el memcpy + reserva de 39 MB son del orden de 10 ms (<10 %), más 39 MB de pico transitorio; (c) en .gpx el score no pasa de ~4 MB (una sola entrada de sectores, ver GP2), así que solo importa en .gp (zip). La propuesta es viable: load_buffer_inplace modifica el buffer y exige que el string viva más que el documento, lo cual se cumple si importScore recibe el string por valor; hay que separar el parseo de importGpif(string_view), que debe seguir con load_buffer. Ojo: el benchmark actual ('import the large song from GPIF', bench_io.cpp:318) entra por importGpif, así que no reflejará la mejora; hace falta un benchmark por importGuitarPro con zip. No afecta a determinismo ni a hilos.

#### io-gp/GP7 · intOf()/trimmed() crean un std::string temporal por cada entero y por cada propiedad de texto

- **Dónde:** `src/io/src/guitar_pro.cpp:87`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
const std::string clean = trimmed(text);
```

**Por qué importa.** intOf se usa para cada id de las listas (vía intsOf, línea 134), para cada id al construir ById (línea 584) y por nota en String, Fret, Midi/Number y Octave (líneas 826, 849, 850, 409); `trimmed` además por beat en Dynamic (línea 748) y dos veces por nota en pitchOf (Step y Accidental, líneas 406 y 413). Son del orden de 1,5-2 millones de std::string temporales en la canción de referencia. Caben en SSO, así que no hay malloc, pero sí construcción + copia + destrucción por token: del orden de 10-20 ms de CPU repartidos, y es código trivial de eliminar.

**Propuesta.** Añadir `std::string_view trimmedView(std::string_view)` (mismo find_first_not_of/find_last_not_of, devolviendo substr de la vista) y usarla en intOf, pitchOf, dynamicFor(...), noteValueFor(...), el clef de readBar (línea 640), HType (935) y Tremolo (806). Dejar `trimmed` con std::string solo donde el resultado se guarda (nombres, info de la canción).

**Nota del revisor.** Comprobado: trimmed (77-84) devuelve std::string e intOf (86-94) lo usa en la línea 87; las referencias son exactas: 134 (cada id de intsOf), 584 (cada id de ById), 826/849/850 (Number, String, Fret por nota), 406/409/413 (pitchOf), 748 (Dynamic por beat), 640, 806, 935. El recuento de ~1,6 M temporales en la referencia cuadra (6 por nota + ids). Caben en SSO, así que no hay malloc; coste estimado 8-16 ms de CPU, casi todo en la fase paralela (más ~2-3 ms serie en ById), o sea pocos ms de reloj: 'bajo' es correcto. La propuesta es viable y segura: clefFor, noteValueFor y dynamicFor ya reciben std::string_view (155, 364, 384), y pitchOf solo usa size(), [0], iteración y == sobre el texto, todo válido con una vista (el texto de pugixml vive mientras viva el documento). from_chars se comporta igual sobre la vista. Mantener std::string donde el valor se guarda (nombres, info, 1162-1171).

#### io-gp/GP8 · Caché de duraciones en unordered_map por pentagrama cuando los ids de Rhythm son densos

- **Dónde:** `src/io/src/guitar_pro.cpp:972`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
std::unordered_map<int, Duration> durations_;
```

**Por qué importa.** durationOf hace una búsqueda hash por beat (80k en la referencia) y cada StaffReader (uno por pentagrama, 20+) vuelve a parsear los mismos <Rhythm> y a poblar su propio mapa con nodos de heap. Los ids de Rhythm se numeran desde 0 (ById ya los guarda en `table_`), así que basta un array indexado. Ganancia pequeña (pocos ms), pero elimina hashing y asignaciones en el bucle paralelo.

**Propuesta.** Sustituir por `std::vector<std::optional<Duration>> durations_` dimensionado con `rhythms_.size()` e indexado por id (con el mapa como reserva solo si ById cayó al modo disperso). Se conserva el comportamiento de avisos, que hoy se emiten la primera vez que cada pentagrama resuelve el ritmo.

**Nota del revisor.** Comprobado: `std::unordered_map<int, Duration> durations_` (972) es miembro de StaffReader, uno por pentagrama (1244), y durationOf (700-730) hace un find por beat (745). Cierto, pero la ganancia es mínima incluso para 'bajo': ~80k búsquedas hash de un int son ~1-2 ms de CPU repartidos entre hilos (décimas de ms de reloj), y el número de ritmos distintos es pequeño (1 en la referencia), así que las reservas del mapa son despreciables. Última prioridad. La propuesta es viable si: se comprueban los límites (el id sale de attribute("ref").as_int(-1), puede ser negativo o inexistente y debe seguir lanzando el mismo FormatError de la línea 706); se mantiene un mapa cuando ById cae al modo disperso (size() devuelve entonces el número de entradas, no el id máximo); y la caché sigue siendo por StaffReader, porque compartirla entre hilos rompería la seguridad de hilos y el orden determinista de los avisos.

#### io-gp/GP2 · En .gpx el contenedor descomprimido sigue vivo durante todo el parseo y el fichero se extrae sin reserve

- **Dónde:** `src/io/src/guitar_pro.cpp:1417`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: alto) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
std::optional<detail::GpxContainer> container;  ...  return importScore(*score, parts, options);
```

**Por qué importa.** `container` (con `unpacked_`, hasta kMaxUnpacked = 64 MB) no se destruye hasta que importScore retorna. En ese momento hay vivos a la vez: bytes del fichero, `unpacked_` (N), `score` (N, copia sector a sector), la copia interna de pugixml (N, ver GP1), el DOM y la Song: tres copias del mismo XML. Además findFile construye `content` con append de 4 KiB sin reservar (gpx_container.cpp:156 `std::string content;`), lo que añade ~log2(N/4K) realocaciones con copia y un pico transitorio de hasta ~2N.

**Propuesta.** Tras leer `score` y `parts`, hacer `container.reset()` antes de llamar a importScore. En findFile, `content.reserve(std::min(size, fs.size()))` cuando `wanted`. Combinado con GP1 el pico de texto pasa de ~3N a ~1N.

**Nota del revisor.** Comprobado: `container` (1417) sigue vivo hasta que retorna importScore (1431) y findFile declara `std::string content;` sin reserve (gpx_container.cpp:156) y añade sector a sector (170). Pero el impacto está muy inflado: findFile solo lee los sectores listados en UNA entrada (slots de offset+0x94 a offset+0x1000 = 987 sectores), así que un fichero extraído no supera ~4,04 MB (lo confirma el comentario de bench_io.cpp:248 'at most about 4 MB'). Por tanto 'tres copias de N con N hasta 64 MB' no es posible para el XML: N <= 4 MB, y lo que libera container.reset() es `unpacked_` (típicamente pocos MB; 64 MB solo en un fichero hostil). En el caso BCFS sin comprimir el contenedor solo guarda un span y no hay nada que liberar. La falta de reserve son como mucho ~15-20 realocaciones sobre <=4 MB (<1-2 ms; la medición de 28-31 ms del .gpx de 4,4 MB está dominada por el desempaquetado bit a bit). Las dos propuestas son correctas y seguras (`parts` y `score` son copias propias; el reserve queda acotado por min(size, fs.size()), mejor aún por 987*4096), pero la ganancia es marginal.

#### io-gp/GP4 · intsOf() devuelve un std::vector<int> nuevo por compás, voz y beat

- **Dónde:** `src/io/src/guitar_pro.cpp:125`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
std::vector<int> values;  //  for (const int noteId : intsOf(textOf(fields.notes)))
```

**Por qué importa.** Se llama en el camino caliente: línea 649 (Voices por compás), 664 (Beats por voz) y 749 (Notes por beat). En la canción de referencia son ~10k + ~10k + 80k llamadas, cada una con al menos una reserva de heap y, al no haber reserve, 2-3 realocaciones para listas de 2-4 ids: del orden de 150-250k malloc/free, hechos desde todos los hilos de pistas a la vez (contención en el asignador, que limita el escalado paralelo).

**Propuesta.** Sustituir por un iterador/visitor sin heap: `template<class F> void forEachInt(std::string_view, F&&)` que haga from_chars directamente sobre la vista, y usarlo en readBar/readBeat. Alternativa mínima: un `std::vector<int> scratch_` miembro de StaffReader reutilizado con clear() (uno para voces, otro para beats/notas, ya que los bucles se anidan).

**Nota del revisor.** Comprobado: intsOf (124-140) devuelve un std::vector<int> nuevo sin reserve y se llama en 649 (por compás de pentagrama), 664 (por voz) y 749 (por beat), dentro de readStaff, que corre en paralelo (1274). El recuento es correcto para la referencia: 10k + 10k + 80k llamadas; con el crecimiento de MSVC (1,2,3,4,6,9) salen ~260k malloc/free. El impacto está inflado: son ~10-20 ms de CPU repartidos entre los hilos de pistas, es decir ~5-8 % con un hilo (211 ms) y 1-3 ms de reloj de los 125 ms medidos con varios hilos, donde manda el parseo XML serie. La 'contención en el asignador' no está demostrada, y la propia construcción del modelo (vector<Note> por beat, vector<Beat> por voz) hace un número similar de reservas que seguirán ahí. La propuesta principal (forEachInt con from_chars sobre la vista) es correcta y conserva el comportamiento (los tokens inválidos se saltan igual). La alternativa de buffers miembro está mal planteada: hay TRES niveles anidados vivos a la vez (voces en 649 -> beats en 664 -> notas en 749 dentro de readBeat), así que compartir un buffer para 'beats/notas' corrompería la iteración de beats; harían falta tres. Sin problema de hilos (un StaffReader por hilo).

#### io-gp/GP5 · Fase serie antes de los hilos: las cinco tablas ById se construyen una tras otra, con vector intermedio sin reserve

- **Dónde:** `src/io/src/guitar_pro.cpp:978`
- **Eje:** concurrencia · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
: root_(root), bars_(root.child("Bars"), "Bar"), voices_(root.child("Voices"), "Voice"), beats_(root.child("Beats"), "Beat"), notes_(root.child("Notes"), "Note"),
```

**Por qué importa.** El constructor de GpifReader recorre en el hilo llamante los ~10k Bar, ~10k Voice, 80k Beat y 160k Note (comparando el nombre de cada hijo y buscando el atributo id), y cada ById acumula primero en `std::vector<std::pair<int, pugi::xml_node>> found;` (línea 580, 16 B por elemento, sin reserve: ~2,5 MB y ~18 realocaciones solo para Notes) para copiar después a `table_`. Todo eso es tiempo serie previo a readStaves, que es lo único paralelizado; por Amdahl es lo que acota la ganancia con 20 pistas.

**Propuesta.** (1) Construir la tabla en una sola pasada: mientras los ids lleguen como 0,1,2... hacer push_back directo en `table_` y degradar al camino actual (found + mapa) solo al ver el primer id fuera de secuencia. (2) Construir las cinco colecciones en paralelo con `detail::forEachIndex(0, 5, ...)` cuando la partitura supere el umbral: el DOM de pugixml es de solo lectura aquí y las colecciones son independientes.

**Nota del revisor.** Comprobado: el constructor de GpifReader (977-981) construye las cinco ById en serie y ById (579-603) acumula en `found` sin reserve antes de copiar a `table_`. Pero la conclusión por Amdahl es incorrecta: la parte serie que acota la ganancia es el parseo de pugixml (~110 ms de los 125 ms; performance.md:67 'most of what is left is the XML parse'), no estas tablas. Recorrer 270k hermanos con comparación de nombre + atributo id + intOf son del orden de 5-15 ms; el vector `found` (2,5 MB, ~30 realocaciones con factor 1,5) cuesta ~1 ms. Ganancia realista: (1) pasada única, 1-2 ms; (2) paralelizar las cinco colecciones, como mucho ~40 % de esa fase porque Notes es el 60 % de los elementos y no se puede trocear (lista enlazada), menos el coste de crear 5 std::thread (forEachIndex no usa pool, parallel.hpp:41-56): 3-5 ms. Viabilidad: la lectura concurrente del DOM es segura y el resultado no depende del orden, pero (a) las ById se construyen en la lista de inicialización y `threads` solo llega en read(threads), así que hay que reestructurar y propagar la opción para respetar {.threads = 1}; (b) la vía rápida debe conservar 'el primer elemento con un id gana' y el criterio denso/disperso de la línea 592.

#### io-gp/GP6 · El fichero se lee byte a byte con istreambuf_iterator y sin tamaño previo

- **Dónde:** `src/io/src/guitar_pro.cpp:1449`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
```

**Por qué importa.** Con iteradores de entrada el vector no puede reservar: crece geométricamente (realocaciones con copia y pico transitorio de ~1,5-2x el fichero) y cada byte pasa por el streambuf (unos ns por byte; en MSVC es especialmente lento). Para un .gp/.gpx de varios MB son decenas de ms evitables en la apertura. Además `bytes` queda vivo durante toda la importación aunque en .gp/.gpx solo hace falta hasta extraer score.gpif.

**Propuesta.** Leer en bloque: `const auto size = std::filesystem::file_size(path, ec); std::vector<std::uint8_t> bytes(size); in.read(reinterpret_cast<char*>(bytes.data()), size);` (comprobando gcount). Opcional: liberar `bytes` tras extraer el GPIF en las ramas zip/BCFZ, moviendo la lectura de entradas a un bloque propio.

**Nota del revisor.** Comprobado: importGuitarProFile (1449-1450) construye el vector con istreambuf_iterator, sin tamaño previo, y es el camino real de apertura (open.cpp:50 <- score_document.cpp:154 y CLI). El hecho es cierto pero el impacto está inflado: lo que se lee así es el fichero en disco (comprimido), no el GPIF: un .gp/.gpx real suele ir de decenas de KB a ~1 MB; a unos ns por byte en release son ~1-5 ms, una sola vez por apertura y en el hilo de trabajo, frente a los 28-125 ms del resto de la importación. 'Decenas de ms' solo valdría para ficheros de varios MB o compilaciones de depuración. Que `bytes` siga vivo es irrelevante (es el tamaño del fichero comprimido, y la rama binaria gp3-5 lo necesita hasta el final). La propuesta (file_size + read en bloque comprobando gcount) es correcta y trivial; conviene mantener un camino de reserva si file_size falla y no fiarse del tamaño para ficheros que cambien mientras se leen.

#### io-gp/GP9 · Lector binario gp3-5: textos descartados que se decodifican a UTF-8 y compases por pista sin reserve

- **Dónde:** `src/io/src/guitar_pro_binary.cpp:670`
- **Eje:** velocidad · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
(void)in_.intByteString();
```

**Por qué importa.** Los textos que se van a tirar (texto de beat en la línea 670, marcadores en 534, plantillas de cabecera/pie en 312-314, efectos RSE en 576-577, nombres en 752) pasan por utf8FromWindows1252: reserva un std::string, lo rellena carácter a carácter y lo destruye. En ficheros con texto por beat es una reserva de heap por beat para nada. Además `track.measures.push_back(std::move(measure));` (línea 655) crece sin reserve aunque `measureCount` se conoce en la línea 333: ~10 realocaciones por pista, moviendo cada vez todos los Measure (4 voces cada uno), con 20 pistas x 500 compases.

**Propuesta.** Añadir a ByteReader `skipIntByteString()` / `skipByteString(size)` que validen el tamaño y avancen `at_` sin construir el string, y usarlos en todos los `(void)in_....String()`. Pasar `measureCount` a readTrack (o reservar justo después del bucle de la línea 336) con `track.measures.reserve(measureCount)`.

**Nota del revisor.** Comprobado: guitar_pro_binary.cpp:670 `(void)in_.intByteString();`, y también 312-314, 534, 576-577, 740, 748/752 (estos dos son nombres de acordes con byteString(22)); intByteString (166-179) construye el std::string con utf8FromWindows1252 (83-98, que sí hace reserve). `track.measures.push_back` (655) crece sin reserve y measureCount se conoce en 333. Existe, pero el coste es despreciable: los textos descartados son 11 por fichero, 2 por pista y solo los beats con el flag 0x04 (texto), que son raros; los vacíos no reservan y los cortos caben en SSO. Las ~10-15 realocaciones por pista mueven Measure de 96 B (4 vectores): 20 pistas x 500 compases son decenas de microsegundos. No hay benchmark de gp3-5 que lo respalde. Advertencias sobre la propuesta: (1) los skip deben conservar exactamente las validaciones (size < 1 lanza con el mismo mensaje y byte; need(field)) para que los errores de ficheros dañados no cambien; (2) `track.measures.reserve(measureCount)` tal cual es peligroso: measureCount lo dicta el fichero hasta kMaxMeasures = 100.000 y hay hasta 128 pistas, de modo que un fichero hostil de ~115 KB forzaría reservar 128 x 100.000 x 96 B = ~1,2 GB por adelantado, cuando hoy la memoria crece solo con los datos realmente leídos. Si se hace, acotar la reserva por los bytes que quedan en el fichero.

**Observaciones del área (qué está bien resuelto).**

Auditoría de solo lectura de src/io/src/guitar_pro.cpp, guitar_pro_binary.cpp/.hpp, gpx_container.cpp/.hpp, gp_directions.hpp y general_midi.hpp. No se ha compilado ni medido nada: las magnitudes son estimaciones a partir de la canción de referencia (80k beats, 160k notas) y del código leído; el tamaño real del GPIF de esa canción no lo he comprobado, por eso las cifras de memoria van en función de N (tamaño del XML).
Bien resuelto, no tocar:
- ById (guitar_pro.cpp:577) ya usa tabla indexada con mapa solo como reserva; BeatFields/NoteFields leen hijos y propiedades en una pasada.
- readStaves (1227) reparte por pentagrama con índice atómico (reparto dinámico, no hay desequilibrio estático entre hilos), cada hilo escribe solo en su Staff, su Warnings y su Damage, y el resultado es determinista. threadsFor limita a min(núcleos, trabajos, 64) y hay umbral kParallelNotes para no lanzar hilos en partituras pequeñas. Los hilos se crean por llamada (core/parallel.hpp:41), pero en una importación puntual ese coste (del orden de 1 ms) es irrelevante.
- gpx_container.cpp: BitReader de 64 bits, `out.reserve(std::min(expected, packed.size() * 16))` y copia de referencias con copy_n; el contenedor se desempaqueta una sola vez para score.gpif y PartConfiguration. Límite de 64 MB contra bombas de descompresión.
- Lector binario gp3-5: ByteReader trabaja sobre un span sin copiar el fichero, con comprobación de límites barata; headers_ ya tiene reserve; los compases de TrackData se MUEVEN a la Song en build() (línea 1162), no se duplican. El formato es un flujo sin offsets (compás a compás intercalando pistas), así que no es paralelizable por pista; no merece la pena intentarlo.
- general_midi.hpp y gp_directions.hpp son tablas constexpr de string_view con búsqueda lineal sobre 19 elementos solo por MasterBar: sin coste relevante.
Fuera de mi área pero observado: Warnings::add (src/io/src/warnings.hpp:24) hace búsqueda lineal comparando dos strings por aviso; con avisos por nota (p. ej. "tapping dropped") se ejecuta hasta 160k veces, aunque la lista es corta. No he leído song.cpp, así que no puedo afirmar nada sobre si los accesores no const (song.measureHeader(m), song.track(t).staff(s)) llamados desde varios hilos en readStaves son puramente de lectura; conviene que quien audite core lo confirme. La importación en la app parece lanzarse con QtConcurrent::run (src/app/src/score_document.cpp:451), no en el hilo de UI, pero no lo he verificado en detalle.

### IO: MIDI y formato nativo

#### io-midi-nativo/N1 · Guardar .elara se ejecuta entero en el hilo de UI (JSON + deflate nivel 6 + escritura a disco)

- **Dónde:** `src/app/src/score_document.cpp:843`
- **Eje:** concurrencia · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
io::saveNative(document_->song(), std::filesystem::path(local.toStdU16String()));
```

**Por qué importa.** ScoreDocument::save() es síncrono y lo llama QML: songToJson (42,8 ms) + deflate + rename corren en el hilo de UI. En la canción de referencia son 70,4 ms (4-5 frames perdidos) y eso con un JSON que comprime a 41 KB porque repite un patrón; docs/performance.md avisa de que las canciones reales comprimen mucho peor, y deflate nivel 6 (ZipEntry::level = 6 por defecto, archive.hpp:57) sobre 7,9 MB de JSON real puede irse a 100-250 ms, más la E/S de disco (antivirus, disco lento, unidad de red) sin cota. open() y relayout() ya van por QtConcurrent; save es la única operación de E/S pesada que queda bloqueando.

**Propuesta.** Lanzar el guardado con QtConcurrent::run capturando song_ (ya es un std::shared_ptr<const core::Song> inmutable que edited() mantiene al día en la línea 877, así que no hace falta copia extra) y terminar con .then(this, ...) como open(): markSaved() referido al valor de edits_ capturado al lanzar, señales saved/saveFailed en vez del bool de retorno, y un único guardado en curso a la vez (el temporal se llama siempre path + ".saving", archive.cpp:94, así que dos guardados simultáneos al mismo fichero se pisarían). Opcional: bajar song.json a nivel 1-3 de deflate, que en miniz es varias veces más rápido.

**Nota del revisor.** Comprobado: ScoreDocument::save() (score_document.cpp:834-857) es Q_INVOKABLE, sincrono, y QML lo llama directamente (Main.qml:107 y 174, `if (score.save(...)) runPendingAction()`). saveNative -> encodeNative -> songToJson + writeZip (nivel 6 por defecto, archive.hpp:57) + writeFileAtomically, todo en el hilo de UI. En src/app el unico otro uso de io:: con E/S es openFile, que ya va por QtConcurrent (linea 451), asi que es cierto que es la unica E/S pesada que bloquea. Matices: (1) los 70,4 ms son del benchmark de encodeNative (bench_io.cpp:89), sin disco: el guardado real es eso mas la escritura. (2) Impacto inflado: es una accion explicita y poco frecuente, y edited() (lineas 877-880) ya hace en el hilo de UI una copia de la cancion (10,7 ms) y una Sequence (28 ms) en CADA edicion; un guardado equivale a unas dos ediciones. Lo que si justifica 'medio' es la E/S sin cota (red, antivirus) y que el dato real comprime peor. Los 100-250 ms de deflate son una extrapolacion sin medir. Propuesta viable con condiciones: song_ esta sincronizado con document_->song() en cada vuelta al bucle de eventos (lineas 502/509, 668, 877; typeDigit acaba siempre en edited() o redo()), y capturar el shared_ptr<const Song> es seguro entre hilos. Pero Document::markSaved() (document.hpp:70) solo marca la profundidad ACTUAL: si se edita durante el guardado hay que marcar solo si edits_ no ha cambiado, o anadir una API en core que respete el descarte de la rama de rehacer y el limite de deshacer (document.cpp:25-33). Ademas hay que reescribir el flujo QML de pendingAction/onClosing (hoy depende del bool de retorno) y no cerrar la ventana hasta que termine el guardado en curso. Bajar el nivel de deflate no compensa: el propio docs/performance.md (fila .mxl) mide que el nivel 1 gana poco y multiplica el tamano por 10.

#### io-midi-nativo/N2 · El escritor nativo construye un DOM yyjson mutable completo y luego duplica el texto

- **Dónde:** `src/io/src/native_format.cpp:108`
- **Eje:** memoria · **Impacto:** medio (propuesto inicialmente: alto) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
std::string json(text, length);
```

**Por qué importa.** Writer crea un yyjson_mut_val (24 bytes: valor de 16 + puntero next) por cada objeto, clave y valor: unas 7 por nota (objeto + pitch/string/fret) y 5 por beat, es decir ~1,5 M de nodos = 35-40 MB de DOM para un modelo que ocupa ~13 MB (160k notas x 48 B + 80k beats x 64 B), más el pool de cadenas copiadas con strncpy. Con el DOM aún vivo, yyjson_mut_write (línea 104) reserva los 7,9 MB de texto y la línea 108 los copia a un std::string: pico aproximado modelo + 37 MB + 7,9 MB + 7,9 MB. Además recorrer el modelo, construir el DOM y volver a recorrerlo para serializar es doble trabajo: son los 42,8 ms medidos.

**Propuesta.** Emitir el JSON directamente a un std::string (las claves son literales fijos y los valores son enteros, booleanos y cadenas cortas; solo hace falta un escape de cadenas para info/nombres): un buffer por pista con reserve estimado (~50 B por nota), escrito en paralelo con forEachIndex y unido en orden, igual que hace el exportador MusicXML (ADR-0008). Elimina el DOM entero (pico ~8 MB en vez de ~53 MB) y deja la escritura en el orden de 5-10 ms. Si se quiere un paso intermedio barato: liberar el doc antes de copiar y devolver el buffer de yyjson sin copiarlo (unique_ptr<char, free> + string_view hacia writeZip).

**Nota del revisor.** El codigo existe tal cual: yyjson_mut_write en la linea 104 y copia a std::string en la 108 con el documento aun vivo (se libera en el destructor de Writer). La cuenta de nodos es correcta: cada add_* de objeto crea clave + valor, asi que una nota con pitch/string/fret son 7 yyjson_mut_val y un beat 5; en la cancion de referencia unos 1,5 M de nodos x 24 B = ~36 MB, mas 7,9 MB + 7,9 MB de texto. Lo que rebajo: (1) como hallazgo de MEMORIA el impacto es bajo: un pico transitorio de ~50 MB durante ~43 ms en una aplicacion de escritorio y solo para una cancion de 160k notas. (2) El coste real es de tiempo (42,8 ms de los 70,4 del guardado) y solo importa mientras el guardado siga en el hilo de UI; con N1 hecho pasa a ser bajo. (3) Es una decision conocida y aplazada: docs/performance.md linea 51 dice 'fast enough for open/save. If it ever shows, write tracks in parallel or emit JSON directly'. (4) El objetivo de 5-10 ms es una estimacion; es coherente con el exportador MusicXML (124 MB en 155 ms en un hilo), pero no esta medido. Huecos de la propuesta: no todos los valores son enteros, booleanos y cadenas: addNumber escribe reales (bpm no entero, volume, pan, traste de armonico), y el emisor directo tendria que dar numeros JSON validos de ida y vuelta y rechazar no finitos; yyjson ademas valida UTF-8 al escribir y el emisor directo debe conservar esa validacion o escribira ficheros que el lector rechaza. El paso intermedio (no copiar el buffer de yyjson) es correcto y barato, pero songToJson es API publica que devuelve std::string (la usa la CLI), asi que tiene que ser una sobrecarga interna.

#### io-midi-nativo/N3 · Importar MIDI: aritmética de Fraction con gcd repetida en los bucles calientes de rejillas y ritmos

- **Dónde:** `src/io/src/midi_import.cpp:750`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** confirmado

```cpp
static_cast<std::size_t>((from / cell).numerator() / (from / cell).denominator());
```

**Por qué importa.** Cada operator/ de Fraction son 4 std::gcd (recíproco normalizado + 2 de reducción cruzada + normalise) y aquí se hace dos veces la misma división por iteración de spellRhythm. El mismo patrón se repite: longestPlain (línea 691) llama a isMultiple (una división) por cada una de las ~24 duraciones candidatas en los silencios; el bucle de la línea 776 multiplica cell * Fraction(next) por cada celda sondeada y otra vez en la 779; Grids::snap (549-555) construye measure.cell() tres veces y hace 2 productos + 1 suma por nota (320k llamadas); Grids::locate (línea 591) evalúa measures_.back().end().toDouble(), que es una suma de fracciones, en cada una de las ~640k llamadas, y su upper_bound convierte start.toDouble() (una división en coma flotante) en cada sondeo; MeasureInfo::end()/cell()/beat()/cellCount() se recalculan en cada uso dentro de writeVoice (líneas 988-1035). Es justo lo que el documento señala como el resto de los 287 ms en un hilo (40,5 ms solo gracias a 12 núcleos; en un portátil de 4 núcleos serían ~80-100 ms).

**Propuesta.** 1) Guardar en MeasureInfo, calculados una vez en layMeasures, end, cell, beat y cellCount como campos. 2) En Grids, precalcular en el constructor startTicks/cellTicks por compás y el fin de canción en double, y avanzar con cursor en vez de upper_bound (las notas llegan ordenadas por inicio). 3) En voiceSubdivisions/spellRhythm/writeVoice trabajar en enteros: unidades de cell/10080 (10080 = mcm de todas las subdivisiones candidatas 1..32, 24, 14, 10, 9), de modo que isMultiple sea un %, floorTo una división entera y las comparaciones un <; convertir a Fraction/Duration solo al emitir. Objetivo razonable: 287 ms en un hilo a menos de 80 ms.

**Nota del revisor.** Verificado linea a linea: operator/ (fraction.hpp:64-69) son 4 std::gcd (constructor del reciproco + 2 de reduccion cruzada + normalise); la linea 750 escribe `from / cell` dos veces; longestPlain llama a isMultiple (una division) por candidata solo cuando aligned, es decir en silencios, y hay 24 duraciones planas; lineas 776 y 779 multiplican cell * Fraction(next); snap (549-555) construye measure.cell() tres veces; locate (591) evalua measures_.back().end(), que es una suma de fracciones, en cada una de las ~640k llamadas (addPoint + snap, dos por nota cada una) y su upper_bound hace start.toDouble() por sondeo; MeasureInfo::end()/cell()/beat()/cellCount() (279-293) no se guardan y se recalculan en writeVoice. Coincide con docs/performance.md linea 63 ('One thread still takes 287 ms, mostly fraction arithmetic in the grids'). Impacto rebajado: la importacion corre en un hilo de trabajo (openAndLayOut via QtConcurrent, score_document.cpp:451), una vez por apertura de un MIDI, y ya esta repartida por pista: 40,5 ms en la maquina de referencia y unos 80-100 ms en 4 nucleos para una cancion de 160k notas. No bloquea la UI ni el audio. Correcciones a la propuesta: el punto 2 (cursor en vez de upper_bound) solo vale para los inicios; las notas estan ordenadas por inicio (linea 248) pero addPoint(note.end) y snap(raw.end) no son monotonos, asi que los finales siguen necesitando busqueda. El punto 3 es aritmeticamente correcto (mcm de 1..10, 12, 14, 16, 24, 32 = 10080, y beat es siempre multiplo de cell), pero es una reescritura del nucleo de la escritura de ritmos y el resultado debe seguir siendo identico bit a bit; el objetivo de menos de 80 ms no esta medido. Los puntos 1 y la parte de precalculo del 2 son seguros y baratos: empezar por ahi.

#### io-midi-nativo/N5 · Al escribir, cada altura y duración se formatea con std::format/std::to_string a un std::string temporal y se vuelve a copiar

- **Dónde:** `src/io/src/native_format.cpp:310`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
addText(object, "pitch", value.pitch.toString());
```

**Por qué importa.** Pitch::toString() termina en std::format("{}{}{}", ...) (src/core/src/pitch.cpp:84) y Duration::toString() en std::to_string más std::format para los tresillos (src/core/src/duration.cpp:45-51); la línea 294 hace lo mismo por beat. Son 240k llamadas (160k notas + 80k beats), cada una construye un std::string que addText copia otra vez al pool con yyjson_mut_obj_add_strncpy (línea 119). std::format en MSVC cuesta del orden de 100-200 ns por llamada, es decir una parte apreciable (estimado 10-20 ms) de los 42,8 ms de escritura, para un dominio de valores diminuto: 7 notas x 5 alteraciones x ~11 octavas y 9 figuras x 3 puntillos.

**Propuesta.** Tabla estática de cadenas ya formateadas indexada por (step, alter, octave) para las alturas y por (value, dots) para las duraciones sin grupo irregular, construida una vez; pasarlas por puntero con yyjson_mut_obj_add_strn (sin copia: el almacenamiento estático sobrevive al documento) o escribirlas tal cual en el emisor directo de N2. std::format queda solo para duraciones con tuplet, o se sustituye por std::to_chars en un buffer de pila.

**Nota del revisor.** Verificado: linea 310 (pitch) y 294 (duration) pasan por toString(); Pitch::toString termina en std::format("{}{}{}") (pitch.cpp:84) y Duration::toString en std::to_string, con std::format solo para grupos irregulares (duration.cpp:46-52). Son 160k + 80k llamadas, cada una crea un std::string temporal (sin asignacion de heap, por SSO) que addText vuelve a copiar con yyjson_mut_obj_add_strncpy (linea 119). Impacto rebajado: los 10-20 ms son una estimacion sin perfilar (plausible para std::format de MSVC, pero no medida), solo afectan al guardado y dejan de notarse si N1 saca el guardado del hilo de UI. Aun asi es el arreglo mas barato de la ruta de guardado. La propuesta es correcta: yyjson_mut_obj_add_strn no copia y el almacenamiento estatico sobrevive al documento; la tabla inmutable es segura entre hilos y la salida es identica. Detalle a corregir: el rango de octavas valido no son 11 sino 12 (de -2 a 9: B#-2 es MIDI 0 y pasa la validacion de pitch.hpp:40), asi que la tabla debe cubrirlo o caer a la ruta general. Alternativa mas simple: que Pitch::toString escriba con std::to_chars en vez de std::format, lo que beneficia a todos los llamantes sin tabla.

#### io-midi-nativo/N9 · Abrir .elara mantiene dos copias del JSON de 7,9 MB además del DOM y del modelo

- **Dónde:** `src/io/src/native_format.cpp:948`
- **Eje:** memoria · **Impacto:** bajo · **Esfuerzo:** S · **Veredicto:** confirmado

```cpp
yyjson_read_opts(const_cast<char*>(json.data()), json.size(), 0, nullptr, &error);
```

**Por qué importa.** Con flags = 0 (sin YYJSON_READ_INSITU) yyjson reserva y copia internamente toda la entrada para poder terminar las cadenas en su sitio, mientras el std::string que devolvió zip.entry("song.json") (línea 981) sigue vivo hasta el final de decodeNative. Ese string, además, se crea relleno de ceros antes de descomprimir encima ('std::string content(size, '\0')', archive.cpp:40: un memset de 7,9 MB inútil). Pico al abrir la canción de referencia: 7,9 MB (texto) + 7,9 MB (copia de yyjson) + ~25 MB de DOM (~1,5 M de valores x 16 B) + el modelo que va creciendo hasta ~13 MB; la copia y el memset son además 1-2 ms de los 51 ms.

**Propuesta.** Descomprimir en un buffer propio con YYJSON_PADDING_SIZE bytes de relleno al final (std::string::resize_and_overwrite o un unique_ptr<char[]> para evitar el relleno de ceros) y parsear con YYJSON_READ_INSITU a través de una sobrecarga interna de songFromJson que reciba el buffer mutable; la API pública con string_view puede seguir copiando. Ahorra 7,9 MB de pico y una pasada de memcpy más una de memset.

**Nota del revisor.** Verificado: yyjson_read_opts con flags = 0 (lineas 947-948) reserva y copia la entrada entera (longitud + relleno) y esa copia vive con el documento; el std::string de zip.entry("song.json") (linea 981) sigue vivo hasta el final de decodeNative; y ZipReader::entry crea el buffer con `std::string content(size, '\0')` (archive.cpp:40), un relleno de ceros que inflate sobrescribe despues. El impacto 'bajo' ya es el correcto: 7,9 MB de un pico de ~55 MB y del orden de 1 ms de 51 (los 1-2 ms son una estimacion algo generosa: memset y memcpy de 7,9 MB rondan 0,5 ms cada uno), todo en el hilo de trabajo de apertura. La propuesta es viable con cuidado: YYJSON_READ_INSITU exige un buffer mutable con YYJSON_PADDING_SIZE (4) bytes a cero tras los datos, y un std::string solo garantiza un terminador, asi que hay que reservar size + 4 y pasar size como longitud; yyjson modifica el buffer (desescapa cadenas en el sitio) y los string_view que devuelve el Reader apuntan a el, de modo que el buffer debe sobrevivir al documento. Debe hacerse con una sobrecarga interna: songFromJson(std::string_view) es API publica y no puede mutar su entrada, y ZipReader::entry lo comparten otros lectores (.mxl, .gpx), que no deben cambiar de comportamiento.

#### io-midi-nativo/N4 · La lectura nativa procesa las pistas en serie sobre un documento yyjson inmutable

- **Dónde:** `src/io/src/native_format.cpp:451`
- **Eje:** concurrencia · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
forEach(tracks, [&](std::size_t, yyjson_val* track) { readTrack(track, song); });
```

**Por qué importa.** Leer song.json son 35,3 ms de los 51,3 ms de abrir un .elara y, según la propia medición, el parseo de yyjson es la parte pequeña: el grueso es construir el modelo pista a pista. Es el único importador que sigue en un hilo (MusicXML, GPIF y MIDI ya reparten por pista). El yyjson_doc inmutable es seguro para lectura concurrente y cada pista escribe solo en su Track. Además cada objeto se consulta clave a clave con yyjson_obj_get (búsqueda lineal con strlen de la clave): 4 búsquedas por nota (pitch, string, fret, effects), 3 por beat y 14 en readNoteEffects (líneas 890-913) aunque el objeto tenga 1-2 claves: del orden de 1 M de búsquedas en la canción de referencia.

**Propuesta.** Dividir readTrack en dos fases: (1) en serie y en orden, crear cada Track con sus pentagramas (barato) y guardar el puntero yyjson de cada staff; (2) readMeasures de cada pista con forEachIndex/threadsFor, un Reader por hilo (path_ es estado propio) inicializado con el prefijo tracks[i], y un hueco de error por pista para devolver siempre el de la primera pista en orden (forEachIndex relanza el primero por hilo, no por pista). Esperable ~35 ms a 6-10 ms. De paso, recorrer cada objeto nota/beat/effects una sola vez con yyjson_obj_iter y despachar por clave, como ya se hizo con los hijos de <note> en MusicXML.

**Nota del revisor.** La linea 451 existe y readTrack/readMeasures/readBeat/readNote corren en serie; el yyjson_doc inmutable es seguro para lectura concurrente y cada pista solo escribiria en su Track si las pistas se crean antes (song.addTrack realoja el vector). Las busquedas por clave son ciertas: 4 yyjson_obj_get por nota y 3 por beat, unas 880k en la cancion de referencia; las 14 de readNoteEffects solo se pagan en notas con efectos (ninguna en el benchmark). Lo que no se sostiene: (1) 'es el unico importador que sigue en un hilo' es falso: guitar_pro_binary.cpp (.gp3/.gp4/.gp5) tampoco usa forEachIndex. (2) El impacto esta inflado: la apertura corre en un hilo de trabajo (score_document.cpp:451), son 35 ms de una apertura que ademas secuencia (28 ms) y maqueta (hasta 21 ms), y el propio docs/performance.md (linea 51) la da por suficiente. (3) La ganancia de 35 a 6-10 ms es optimista: el parseo de yyjson y su copia de 7,9 MB siguen en serie, mas el arranque de hilos; lo razonable es 10-15 ms. (4) Las busquedas son baratas en objetos de 1-3 claves (yyjson compara longitudes antes de memcmp); recorrer con yyjson_obj_iter aporta poco salvo en los objetos effects. Sobre la correccion: dividir en dos fases cambia que error se informa cuando hay varios (un error de cabecera en la pista 1 saldria antes que uno de compases en la pista 0); para conservar el orden actual hay que cortar la fase 1 en el primer error y devolver el de menor indice de pista entre las dos fases. ReadError no deriva de std::exception, pero forEachIndex captura con catch(...), asi que se propaga bien.

#### io-midi-nativo/N6 · gridSpelling consulta un std::map thread_local con clave tupla en cada candidato, celda e iteración

- **Dónde:** `src/io/src/midi_import.cpp:453`
- **Eje:** velocidad · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** S · **Veredicto:** parcial

```cpp
thread_local std::map<std::tuple<std::int64_t, std::int64_t, int>, std::optional<GridSpelling>>
```

**Por qué importa.** La memoización evita recalcular, pero la consulta sigue en el camino caliente: usable() (línea 605) la llama por cada candidato de exactFor/bestFor (hasta 15 por celda con puntos, ~40k celdas por pista), voiceSubdivisions (línea 723) por candidato y celda, y spellRhythm en cada iteración y en cada celda siguiente que sondea (líneas 746, 778). Cada llamada paga el acceso TLS con guarda de inicialización, construir la tupla y bajar el árbol comparando tuplas de tres campos; con del orden de 1-2 M de llamadas en la canción de referencia son decenas de ms de los 287 ms en un hilo. Además usable() construye Fraction(1, 128) (un gcd) en cada llamada (línea 607) y, como forEachIndex crea hilos nuevos por importación, la caché se reconstruye y destruye en cada hilo.

**Propuesta.** Una canción tiene 1-3 longitudes de celda distintas: construir en layMeasures una tabla por longitud de celda, std::array<std::optional<GridSpelling>, 33> indexada por subdivisiones (con el booleano 'usable por notación', written >= 1/128, ya resuelto), y guardar en MeasureInfo un puntero a su tabla. La consulta pasa a ser un índice de array, sin TLS ni mapa, y la tabla inmutable se comparte entre los hilos.

**Nota del revisor.** El codigo existe (thread_local std::map con clave tupla, lineas 452-461) y se consulta desde usable() (605), voiceSubdivisions (723) y spellRhythm (746, 778). Pero el volumen esta inflado: '~40k celdas por pista' es falso, son 2000 por pista (500 compases x 4 celdas) y 40k en toda la cancion. En la cancion de referencia exactFor se detiene en el segundo candidato, voiceSubdivisions tambien, y spellRhythm hace una consulta por acorde (el bucle de la linea 776 no llega a sondear con corcheas): unas 240k consultas en total, no 1-2 M, sobre un mapa de 2 entradas. A 10-20 ns por consulta son 3-5 ms de los 287 ms en un hilo (menos de 1 ms de reloj en paralelo), no 'decenas de ms'. Fraction(1, 128) con argumentos constantes es constexpr y su gcd es trivial; reconstruir el mapa por hilo son unas pocas entradas. Ademas esta memoizacion ES la mitigacion ya aplicada (docs/performance.md linea 62: 357 a 287 ms). En musica tocada (no cuantizada) se prueban hasta 15 + 6 candidatos por celda y el coste sube, pero sigue siendo proporcional a las celdas. La propuesta (tabla por longitud de celda indexada por subdivisiones, inmutable y compartida) es correcta, determinista y segura entre hilos, pero la ganancia es marginal; tiene sentido solo como parte de N3.

#### io-midi-nativo/N7 · Importar MIDI: vectores temporales por compás, por acorde y por celda (cientos de miles de asignaciones concurrentes)

- **Dónde:** `src/io/src/midi_import.cpp:705`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
std::vector<std::vector<Fraction>> inside(count);
```

**Por qué importa.** Por cada (pista, compás, voz) writeVoice crea std::vector<Fraction> points (línea 991) y voiceSubdivisions crea un vector de vectores (1 + una asignación por celda con puntos) más std::vector<int> result (línea 716); spellRhythm devuelve un std::vector<Duration> nuevo por acorde y por silencio (línea 742); cada Chord lleva su propio std::vector de notas (línea 811, una asignación por acorde en assignVoices); beat.notes.push_back (línea 1027) crece sin reserve aunque chord.notes.size() se conoce, y beats tampoco se reserva (línea 997); Grids::points_ es un vector<vector<Point>> con una asignación por celda ocupada (línea 669). En la canción de referencia (10k compases-voz, 80k acordes) son del orden de 400-500k asignaciones de vida cortísima, hechas a la vez desde hasta 24 hilos contra el mismo heap: es candidato a explicar que el escalado sea 7x (287 a 40,5 ms) con 12 núcleos y 20 trabajos.

**Propuesta.** Un struct de trabajo por writeNotes reutilizado entre compases (points, inside aplanado en un solo vector con offsets por celda, subdivisions, buffer de duraciones con clear() en vez de construir); que spellRhythm añada a un vector del llamante en lugar de devolver uno; beat.notes.reserve(chord.notes.size()) y beats.reserve; notas del acorde en un rango (first, count) sobre un único vector por voz en vez de un vector por Chord; points_ en formato CSR (contar, prefijo, rellenar).

**Nota del revisor.** Las asignaciones existen todas: points (991), inside como vector de vectores (705), result (716), el vector que devuelve spellRhythm por acorde y por silencio (742), Chord::notes (811), beat.notes sin reserve (1027), beats sin reserve (997) y points_ como vector<vector<Point>> (669). El recuento incluso se queda corto: contando el crecimiento de los vectores salen unas 60 asignaciones por compas-voz, mas ~160k de points_, del orden de 800k en la cancion de referencia. Lo que rebajo: (1) Que esto explique el escalado de 7x es una conjetura sin medir; la ley de Amdahl lo explica igual con lo que queda en serie: readSmf (320k MidiEvent), gather con sus stable_sort, layMeasures, el arranque de hilos y la destruccion del resultado en el benchmark. (2) La importacion corre en un hilo de trabajo y tarda 40,5 ms; no afecta a la UI ni al audio. Sobre la propuesta: los reserve (beat.notes.reserve(chord.notes.size()), beats.reserve) y un struct de trabajo reutilizado por writeNotes son seguros (cada pista escribe en su hilo y en su Track) y no cambian el resultado. En cambio 'notas del acorde como rango (first, count) sobre un vector por voz' no encaja con assignVoices: una nota puede unirse a un acorde que no es el ultimo (bucle inverso de las lineas 827-833, por igual inicio y fin) y la rama de voces llenas anade notas a un acorde ya colocado (860-862), asi que los rangos no son contiguos sin reordenar antes por (inicio, fin, tecla) conservando 'la mas aguda primero'.

#### io-midi-nativo/N8 · Exportar MIDI pasa por un MidiEvent de 48 bytes con std::string por cada note-on/off, sin reserve, y calcula los ticks con fracciones

- **Dónde:** `src/io/src/midi_export.cpp:277`
- **Eje:** memoria · **Impacto:** bajo (propuesto inicialmente: medio) · **Esfuerzo:** M · **Veredicto:** parcial

```cpp
track.events.push_back(channel(edge.tick, detail::kNoteOn, channelNumber, edge.key,
```

**Por qué importa.** MidiEvent (smf.hpp:45-50) lleva un std::string payload aunque los mensajes de canal no lo usan: 48 bytes en MSVC x64 por evento que acaba en 3-4 bytes de fichero. Con 320k eventos son ~15 MB intermedios para producir 1,2 MB, además de los edges (24 B x 320k) y de las SoundingNote. track.events crece sin reserve (se conoce edges.size()), y encodeEvents empieza con 'std::string out;' vacío (smf.cpp:40) y lo llena byte a byte con push_back. Timing::ticks (línea 45) construye Fraction(4 * division) y multiplica: 4 gcd por llamada, dos llamadas por nota más la suma note->start + note->length de la línea 262 (unas 1,7 M de gcd), cuando en el caso exacto el denominador ya divide a 4*division. reportDropped (línea 213) llama a warnings.add, que recorre las entradas comparando cadenas, una vez por nota con posición de traste: en un editor de tablaturas, casi todas. El importador tiene el espejo: readSmf materializa los mismos MidiEvent de 48 B y 'const auto file' (midi_import.cpp:1274) sigue vivo durante todo el import aunque solo gather() lo usa.

**Propuesta.** En addNotes, codificar los edges directamente a bytes (delta varlen + running status + 2 datos) en un std::string con reserve(edges.size() * 4) y dejar MidiEvent solo para los pocos meta-eventos de cabecera; ticks exactos como num * (4 * division / den) cuando (4 * division) % den == 0, con la ruta de Fraction solo para el caso no exacto; en reportDropped acumular por tipo un contador y el primer compás en un array local y volcarlo a Warnings una vez por pista. En importación, liberar el MidiFile al terminar gather() (o que readTrack entregue los mensajes de canal en un struct compacto de 16 bytes y los meta aparte).

**Nota del revisor.** Los hechos son ciertos: MidiEvent (smf.hpp:45-50) lleva std::string y ocupa 48 B en MSVC x64; out.events crece sin reserve; encodeEvents empieza con `std::string out;` vacio (smf.cpp:40) y escribe byte a byte; Timing::ticks (linea 45) son 4 gcd por llamada, dos llamadas por nota mas la suma de la linea 262 (2 gcd): 10 gcd por nota, ~1,6 M; reportDropped llama a warnings.add por cada nota con posicion (linea 213) y add recorre las entradas comparando cadenas (warnings.hpp:26-31). El impacto esta inflado: (1) exportMidi solo se llama desde la CLI (src/cli/src/file_commands.cpp:242); la aplicacion no exporta MIDI, asi que no toca la UI. (2) Un MidiEvent con payload vacio no asigna en el heap (SSO): los ~15 MB son memoria transitoria repartida entre pistas, no 320k asignaciones. (3) La exportacion entera son 45 ms (74 en un hilo) y la parte por pista ya va en paralelo; lo que queda, segun el propio documento, son sobre todo las notas sonantes. (4) En una pista con solo 'fret positions dropped' warnings.add acierta en la primera entrada: dos comparaciones de cadena por nota, unos pocos ms en total. La propuesta es correcta: en modo exacto 4*division es multiplo del denominador por construccion (chooseTiming, lineas 56-62), asi que num * (4*division/den) da el mismo tick que la ruta con Fraction, y ticks(start) + ticks(length) equivale a ticks(start + length); hay que conservar la ruta con redondeo cuando exact es false. Codificar los edges directamente debe dar los mismos bytes (running status, que se anula tras los meta) para respetar ADR-0009. En el espejo de importacion, `file` (linea 1274) no se puede soltar sin mas tras gather(): importParsed usa file.division despues (linea 1257); hay que copiar la division antes. Ahorra 15 MB durante 40 ms: irrelevante.

**Observaciones del área (qué está bien resuelto).**

Ficheros leídos enteros: src/io/src/native_format.cpp, midi_import.cpp, midi_export.cpp, smf.cpp, smf.hpp, midi_shared.hpp, archive.cpp, warnings.hpp, src/core/include/elara/core/parallel.hpp y fraction.hpp, y las partes de src/app/src/score_document.cpp que abren, guardan y reaccionan a una edición.
Bien resuelto, no tocar:
- Writer: las claves son literales guardados por puntero (no se copian); los valores por defecto y los compases/voces vacíos finales se omiten, lo que ya reduce el JSON.
- Reader: JsonPath guarda segmentos y solo construye texto al fallar; readMeasures y readBeat hacen reserve con yyjson_arr_size; los límites y errores están localizados sin coste en el camino feliz.
- ScoreDocument::open ya corre en QtConcurrent con contador de generación que descarta resultados obsoletos; song_ es un snapshot inmutable compartido, lo que hace barato mover el guardado a un worker (N1).
- writeSmf(format, division, bodies) reserva el tamaño exacto; addNotes reserva edges; plainDurations() se calcula una vez; chooseTiming evita construir fracciones y corta pronto con 'exact && needed % den != 0'.
- Import y export MIDI ya reparten por pista con avisos fusionados en orden (resultado determinista) y umbral kParallelNotes para no lanzar hilos en canciones pequeñas.
- Sin carreras de datos visibles en el área: cada trabajo escribe solo en su Track / su body / su Warnings, y la caché de gridSpelling es thread_local.
Observaciones fuera del área estricta (no contadas como hallazgo): core::forEachIndex crea y destruye std::thread en cada llamada (core/parallel.hpp:44) en vez de usar un pool; para importar/exportar un fichero es despreciable (1-2 ms), pero conviene que lo valore quien audite core/notation/audio, donde se llama por edición. ScoreDocument::edited() copia la canción entera y la secuencia en el hilo de UI en cada edición (score_document.cpp:877-880); pertenece al área de la app.


