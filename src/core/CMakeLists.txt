# elara_core: the musical model. Pure C++23 - no Qt, no JUCE (see README.md).
add_library(elara_core STATIC
  src/chord_name.cpp
  src/clipboard.cpp
  src/commands.cpp
  src/document.cpp
  src/duration.cpp
  src/edit_session.cpp
  src/effects.cpp
  src/entry.cpp
  src/fraction.cpp
  src/fret_placement.cpp
  src/instrument.cpp
  src/lyrics.cpp
  src/measure.cpp
  src/parallel.cpp
  src/performance.cpp
  src/pitch.cpp
  src/retune.cpp
  src/signature.cpp
  src/song.cpp
  src/tempo.cpp
  src/transpose.cpp
  src/tuning.cpp
  src/validation.cpp
  src/version.cpp)
add_library(elara::core ALIAS elara_core)

target_include_directories(elara_core PUBLIC include)
target_compile_definitions(elara_core PRIVATE ELARA_VERSION_STRING="${PROJECT_VERSION}")
elara_target_warnings(elara_core)
