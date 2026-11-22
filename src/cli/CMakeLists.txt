# elara-cli: headless entry point for tests, scripts and power users (ADR-0003).
# Every user-visible capability should be reachable from here with --json output.
add_executable(elara-cli
  src/main.cpp)

target_link_libraries(elara-cli PRIVATE
  elara::core
  elara::audio
  elara::audio_juce
  CLI11::CLI11)
elara_target_warnings(elara-cli)
