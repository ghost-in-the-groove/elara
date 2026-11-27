# elara-cli: headless entry point for tests, scripts and power users (ADR-0003).
# Every user-visible capability should be reachable from here with --json output.

# The steps of `elara-cli edit`, parsed and run against a document, in a library
# of their own so that tests run them without the executable.
add_library(elara_cli_steps STATIC src/edit_steps.cpp)
target_include_directories(elara_cli_steps PUBLIC src)
target_link_libraries(elara_cli_steps PUBLIC elara::core)
elara_target_warnings(elara_cli_steps)

# The --json output's writer (escaping, commas, numbers), tested on its own.
add_library(elara_cli_json STATIC src/json_writer.cpp)
target_include_directories(elara_cli_json PUBLIC src)
elara_target_warnings(elara_cli_json)

add_executable(elara-cli
  src/main.cpp
  src/common.cpp
  src/audio_commands.cpp
  src/edit_commands.cpp
  src/file_commands.cpp
  src/font_commands.cpp
  src/render_commands.cpp
  src/song_report.cpp
  src/theory_commands.cpp)

target_link_libraries(elara-cli PRIVATE
  elara_cli_json
  elara_cli_steps
  elara::core
  elara::io
  elara::notation
  elara::audio
  elara::audio_juce
  elara::audio_fluid
  CLI11::CLI11)
elara_target_warnings(elara-cli)
if(WIN32)
  # UTF-8 arguments and paths (see the manifest).
  target_sources(elara-cli PRIVATE elara-cli.manifest)
endif()

install(TARGETS elara-cli RUNTIME DESTINATION "${ELARA_INSTALL_TOOLS_DIR}")
if(WIN32)
  # The DLLs of the libraries it links (vcpkg builds them as DLLs on Windows).
  install(FILES $<TARGET_RUNTIME_DLLS:elara-cli> DESTINATION "${ELARA_INSTALL_TOOLS_DIR}")
endif()
