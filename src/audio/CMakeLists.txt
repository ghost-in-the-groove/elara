# elara_audio: the sequencer, the built-in synthesizer, offline rendering,
# real-time-safe DSP and audio file helpers. Pure C++23.
add_library(elara_audio STATIC
  src/offline_renderer.cpp
  src/pitch_detector.cpp
  src/player.cpp
  src/sample_ring.cpp
  src/sequence.cpp
  src/sfz.cpp
  src/sfz_bank.cpp
  src/sound_bank.cpp
  src/synth.cpp
  src/tone_generator.cpp
  src/tuner.cpp
  src/wav_reader.cpp
  src/wav_writer.cpp)
add_library(elara::audio ALIAS elara_audio)

target_include_directories(elara_audio PUBLIC include)
target_link_libraries(elara_audio PUBLIC elara::core)
elara_target_warnings(elara_audio)

# elara_audio_juce: the only target that sees JUCE (ADR-0002). Its public
# header is plain C++; JUCE stays an implementation detail behind a pimpl.
add_library(elara_audio_juce STATIC
  backend_juce/src/microphone_input.cpp
  backend_juce/src/system_audio.cpp)
add_library(elara::audio_juce ALIAS elara_audio_juce)

target_include_directories(elara_audio_juce PUBLIC backend_juce/include)
target_link_libraries(elara_audio_juce
  PUBLIC
    elara::audio
  PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_events
    juce::juce_core
    juce::juce_recommended_config_flags)
target_compile_definitions(elara_audio_juce PRIVATE
  JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  JUCE_STANDALONE_APPLICATION=1
  JUCE_USE_CURL=0
  JUCE_WEB_BROWSER=0
  JUCE_MODAL_LOOPS_PERMITTED=0
  # Debug builds would otherwise print "JUCE vX.Y.Z" to stderr at start-up on
  # macOS and Linux, polluting elara-cli output.
  JUCE_DISABLE_JUCE_VERSION_PRINTING=1)
target_compile_options(elara_audio_juce PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
# Strict warnings for our file only; JUCE's module sources compile in this target too.
elara_source_warnings(backend_juce/src/system_audio.cpp)

# elara_audio_fluid: SoundFonts as sound banks, through FluidSynth (ADR-0043).
# Its public header is plain C++; FluidSynth stays in its source.
add_library(elara_audio_fluid STATIC backend_fluid/src/sound_font.cpp)
add_library(elara::audio_fluid ALIAS elara_audio_fluid)

target_include_directories(elara_audio_fluid PUBLIC backend_fluid/include)
target_link_libraries(elara_audio_fluid
  PUBLIC
    elara::audio
  PRIVATE
    FluidSynth::libfluidsynth)
elara_target_warnings(elara_audio_fluid)
