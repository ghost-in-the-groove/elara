cmake_minimum_required(VERSION 3.28)

project(elara
  VERSION 0.1.0
  DESCRIPTION "Free and open-source tablature editor, score editor and music studio"
  # JUCE refuses to configure unless the top-level project enables C.
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20/23 modules (ADR-0001): skip dependency scanning.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(ELARA_BUILD_APP "Build the Qt desktop application" ON)
option(ELARA_BUILD_TESTS "Build unit and integration tests" ON)
option(ELARA_WARNINGS_AS_ERRORS "Treat compiler warnings in Elara sources as errors" OFF)

# A compiler cache such as ccache (CI sets one) can only reuse MSVC objects that
# carry their debug information (/Z7), not the ones that share a .pdb (/Zi).
if(MSVC AND (CMAKE_CXX_COMPILER_LAUNCHER OR DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER}))
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

# Every executable (app, CLI, tests) lands in build/<preset>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(ElaraWarnings)
include(ElaraDependencies)

# Where elara-cli and its fonts install (ADR-0048): inside the app bundle on
# macOS, so the disk image holds one thing to drag; next to the app elsewhere.
if(APPLE AND ELARA_BUILD_APP)
  set(ELARA_INSTALL_TOOLS_DIR "elara.app/Contents/MacOS")
else()
  set(ELARA_INSTALL_TOOLS_DIR "${CMAKE_INSTALL_BINDIR}")
endif()

add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

add_subdirectory(src/core)
add_subdirectory(src/io)
add_subdirectory(src/notation)
add_subdirectory(src/audio)
add_subdirectory(src/cli)
if(ELARA_BUILD_APP)
  add_subdirectory(src/app)
endif()

if(ELARA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(ElaraPackaging)
