# elara_io: file formats. The native .elara format (ADR-0006) and MusicXML import
# (ADR-0007); Guitar Pro and MIDI later. No Qt, no JUCE (see README.md).
find_package(miniz CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(yyjson CONFIG REQUIRED)

add_library(elara_io STATIC
  src/archive.cpp
  src/musicxml.cpp
  src/native_format.cpp)
add_library(elara::io ALIAS elara_io)

target_include_directories(elara_io PUBLIC include)
target_link_libraries(elara_io
  PUBLIC elara::core
  PRIVATE miniz::miniz pugixml::pugixml yyjson::yyjson Threads::Threads)
elara_target_warnings(elara_io)
