# elara_notation: the notation engine (ADR-0011). It reads SMuFL font metadata
# and lays out standard notation and tablature into a display list (ADR-0012). No Qt, no JUCE
# (see README.md).
find_package(yyjson CONFIG REQUIRED)
find_package(Threads REQUIRED) # systems are laid out on several threads

add_library(elara_notation STATIC
  src/caret.cpp
  src/cursor.cpp
  src/display_list.cpp
  src/drum_map.cpp
  src/font_outlines.cpp
  src/layout_common.cpp
  src/page_layout.cpp
  src/pdf.cpp
  src/smufl.cpp
  src/standard_staff.cpp
  src/svg.cpp
  src/symbol.cpp
  src/system_layout.cpp
  src/tab_staff.cpp)
add_library(elara::notation ALIAS elara_notation)

target_include_directories(elara_notation PUBLIC include)
target_link_libraries(elara_notation
  PUBLIC elara::core
  PRIVATE yyjson::yyjson Threads::Threads)
elara_target_warnings(elara_notation)
