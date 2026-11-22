# Third-party dependencies.
#
# - vcpkg (manifest mode, vcpkg.json): CLI11, Catch2.
# - FetchContent with a pinned, hashed archive: JUCE (audio only, ADR-0002).
# - Qt is found from QT_ROOT_DIR (CMAKE_PREFIX_PATH in CMakePresets.json).
#
# Adding a dependency: check its licence against AGPL-3.0 first, record it in
# THIRD_PARTY_NOTICES.md and, if it is a design decision, in docs/adr/.

include(FetchContent)

set(ELARA_DOWNLOAD_CACHE "${PROJECT_SOURCE_DIR}/build/_downloads"
  CACHE PATH "Where third-party source archives are cached between build trees")

FetchContent_Declare(juce
  URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.6.tar.gz
  URL_HASH SHA256=103e6c9d5f17f19bc7b5485a890573609d326513aad4db6b045181f4936ef424
  DOWNLOAD_DIR "${ELARA_DOWNLOAD_CACHE}"
  DOWNLOAD_NAME juce-9.0.6.tar.gz
  SYSTEM
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(juce)

find_package(CLI11 CONFIG REQUIRED)

if(ELARA_BUILD_TESTS)
  find_package(Catch2 3 CONFIG REQUIRED)
endif()

if(ELARA_BUILD_APP)
  find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)
endif()
