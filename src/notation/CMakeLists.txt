# elara_notation: the notation engine (ADR-0011). It reads SMuFL font metadata
# and will lay out standard notation and tablature into a display list. No Qt,
# no JUCE (see README.md).
find_package(yyjson CONFIG REQUIRED)

add_library(elara_notation STATIC
  src/smufl.cpp)
add_library(elara::notation ALIAS elara_notation)

target_include_directories(elara_notation PUBLIC include)
target_link_libraries(elara_notation
  PUBLIC elara::core
  PRIVATE yyjson::yyjson)
elara_target_warnings(elara_notation)
