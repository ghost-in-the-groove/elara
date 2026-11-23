# Third-party dependencies.
#
# - vcpkg (manifest mode, vcpkg.json): CLI11, Catch2.
# - FetchContent with a pinned, hashed archive: JUCE (audio only, ADR-0002).
# - Pinned, hashed files: the Bravura music font and its SMuFL metadata (ADR-0011).
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

# Bravura (SMuFL music font, SIL OFL 1.1, ADR-0011): three files of a tagged
# release, each checked against its SHA-256, copied to bin/fonts/ next to the
# executables. ELARA_FONT_DIR is where they end up.
set(ELARA_BRAVURA_VERSION 1.482)
set(ELARA_FONT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fonts")
set(_bravura_url
  "https://raw.githubusercontent.com/steinbergmedia/bravura/bravura-${ELARA_BRAVURA_VERSION}/redist")
set(_bravura_cache "${ELARA_DOWNLOAD_CACHE}/bravura-${ELARA_BRAVURA_VERSION}")
foreach(_entry IN ITEMS
    "Bravura.json|489fb4d734fc0b72a3cc601b50ebdf0bf8c57802ab9893e52ce030b86bf2ce11"
    "otf/Bravura.otf|cdf0f893ee1fdb64b7f6713d71ee0dcfc349c0ac01429a8e451b01a9e79f5f3b"
    "OFL.txt|c24929be7028026a65ee8894da1e3c36d2a4ccce0548d9ba8ba64509f46319ee")
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _path)
  list(GET _parts 1 _hash)
  get_filename_component(_name "${_path}" NAME)
  set(_cached "${_bravura_cache}/${_name}")
  set(_have "")
  if(EXISTS "${_cached}")
    file(SHA256 "${_cached}" _have)
  endif()
  if(NOT _have STREQUAL _hash)
    message(STATUS "Downloading Bravura ${ELARA_BRAVURA_VERSION}: ${_name}")
    file(DOWNLOAD "${_bravura_url}/${_path}" "${_cached}"
      EXPECTED_HASH SHA256=${_hash} TLS_VERIFY ON STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
      list(GET _status 1 _reason)
      message(FATAL_ERROR "Could not download Bravura's ${_name}: ${_reason}")
    endif()
  endif()
  file(COPY "${_cached}" DESTINATION "${ELARA_FONT_DIR}")
endforeach()

# Arimo (sans-serif text font, SIL OFL 1.1, ADR-0015): metric-compatible with
# Arial and Helvetica, the widths layout assumes for text. Two files of a pinned
# commit of the Google Fonts repository and their licence, each checked against
# its SHA-256, copied to bin/fonts/ under plain names.
set(_arimo_commit 903d46673260c1f4c7f7ef67f5190fb03eab5042)
set(_arimo_url "https://raw.githubusercontent.com/google/fonts/${_arimo_commit}/ofl/arimo")
set(_arimo_cache "${ELARA_DOWNLOAD_CACHE}/arimo-${_arimo_commit}")
foreach(_entry IN ITEMS
    "Arimo%5Bwght%5D.ttf|Arimo.ttf|e43898b143ec826ac8cb4034816458a7047fbe0836558de2a1f8c6223ae3e0ca"
    "Arimo-Italic%5Bwght%5D.ttf|Arimo-Italic.ttf|a80fc54fd0233c1dfe298577c4d00f5ae81d5bb83510975e473c47e699b7f4ed"
    "OFL.txt|Arimo-OFL.txt|11cce536cd2f3864d767003af5dcd739e2e15818cf2279b6175edeadd3960992")
  string(REPLACE "|" ";" _parts "${_entry}")
  list(GET _parts 0 _path)
  list(GET _parts 1 _name)
  list(GET _parts 2 _hash)
  set(_cached "${_arimo_cache}/${_name}")
  set(_have "")
  if(EXISTS "${_cached}")
    file(SHA256 "${_cached}" _have)
  endif()
  if(NOT _have STREQUAL _hash)
    message(STATUS "Downloading Arimo: ${_name}")
    file(DOWNLOAD "${_arimo_url}/${_path}" "${_cached}"
      EXPECTED_HASH SHA256=${_hash} TLS_VERIFY ON STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
      list(GET _status 1 _reason)
      message(FATAL_ERROR "Could not download Arimo's ${_name}: ${_reason}")
    endif()
  endif()
  file(COPY "${_cached}" DESTINATION "${ELARA_FONT_DIR}")
endforeach()

find_package(CLI11 CONFIG REQUIRED)

if(ELARA_BUILD_TESTS)
  find_package(Catch2 3 CONFIG REQUIRED)
endif()

if(ELARA_BUILD_APP)
  find_package(Qt6 6.12 REQUIRED COMPONENTS Concurrent Quick QuickControls2 LinguistTools)
endif()
