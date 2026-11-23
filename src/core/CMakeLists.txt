# elara_core: the musical model. Pure C++23 - no Qt, no JUCE (see README.md).
add_library(elara_core STATIC
  src/commands.cpp
  src/document.cpp
  src/duration.cpp
  src/effects.cpp
  src/fraction.cpp
  src/instrument.cpp
  src/measure.cpp
  src/pitch.cpp
  src/signature.cpp
  src/song.cpp
  src/tuning.cpp
  src/validation.cpp
  src/version.cpp)
add_library(elara::core ALIAS elara_core)

target_include_directories(elara_core PUBLIC include)
target_compile_definitions(elara_core PRIVATE ELARA_VERSION_STRING="${PROJECT_VERSION}")
elara_target_warnings(elara_core)
