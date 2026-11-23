# elara_io: file formats. The native .elara format (ADR-0006), MusicXML import and
# export (ADR-0007, ADR-0008), MIDI import and export (ADR-0009) and Guitar Pro
# import (ADR-0010). No Qt, no JUCE (see README.md).
find_package(miniz CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(yyjson CONFIG REQUIRED)

add_library(elara_io STATIC
  src/archive.cpp
  src/musicxml.cpp
  src/musicxml_export.cpp
  src/guitar_pro.cpp
  src/guitar_pro_binary.cpp
  src/midi_export.cpp
  src/midi_import.cpp
  src/smf.cpp
  src/native_format.cpp)
add_library(elara::io ALIAS elara_io)

target_include_directories(elara_io PUBLIC include)
target_link_libraries(elara_io
  PUBLIC elara::core
  PRIVATE miniz::miniz pugixml::pugixml yyjson::yyjson Threads::Threads)
elara_target_warnings(elara_io)
