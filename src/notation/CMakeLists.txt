# elara_notation: the notation engine (ADR-0011). It reads SMuFL font metadata
# and lays out standard notation into a display list (ADR-0012). No Qt, no JUCE
# (see README.md).
find_package(yyjson CONFIG REQUIRED)

add_library(elara_notation STATIC
  src/display_list.cpp
  src/drum_map.cpp
  src/smufl.cpp
  src/staff_layout.cpp
  src/svg.cpp)
add_library(elara::notation ALIAS elara_notation)

target_include_directories(elara_notation PUBLIC include)
target_link_libraries(elara_notation
  PUBLIC elara::core
  PRIVATE yyjson::yyjson)
elara_target_warnings(elara_notation)
