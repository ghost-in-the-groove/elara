# Tests. One Catch2 executable per module plus end-to-end CTest cases that drive
# elara-cli and the app headlessly (ADR-0003). Test names are prefixed with the
# module ("core.", "io.", "notation.", "audio.", "cli.", "app.") so `ctest -R <module>` selects them.

include(Catch)

# elara_add_unit_tests(<module> SOURCES <files...> LIBRARIES <targets...>)
function(elara_add_unit_tests module)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
  set(target elara_${module}_tests)
  add_executable(${target} ${ARG_SOURCES})
  target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES} Catch2::Catch2WithMain)
  elara_target_warnings(${target})
  catch_discover_tests(${target}
    TEST_PREFIX "${module}."
    DISCOVERY_MODE PRE_TEST)
endfunction()

elara_add_unit_tests(core
  SOURCES
    core/test_version.cpp
    core/test_fraction.cpp
    core/test_duration.cpp
    core/test_pitch.cpp
    core/test_tuning.cpp
    core/test_measure.cpp
    core/test_song.cpp
    core/test_signature.cpp
    core/test_timeline.cpp
    core/test_document.cpp
    core/test_effects.cpp
    core/test_validation.cpp
    core/test_performance.cpp
  LIBRARIES elara::core)
target_compile_definitions(elara_core_tests PRIVATE ELARA_EXPECTED_VERSION="${PROJECT_VERSION}")

elara_add_unit_tests(audio
  SOURCES audio/test_tone_generator.cpp audio/test_wav_writer.cpp
  LIBRARIES elara::audio)

find_package(miniz CONFIG REQUIRED) # the .mxl tests build archives
elara_add_unit_tests(io
  SOURCES io/test_native_format.cpp io/test_musicxml.cpp io/test_musicxml_export.cpp io/test_midi.cpp io/test_guitar_pro.cpp
          io/test_guitar_pro_binary.cpp io/test_guitar_pro_gpx.cpp
  LIBRARIES elara::io miniz::miniz)
target_compile_definitions(elara_io_tests PRIVATE ELARA_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")

elara_add_unit_tests(notation
  SOURCES notation/test_smufl.cpp notation/test_staff_layout.cpp
  LIBRARIES elara::notation)
target_compile_definitions(elara_notation_tests PRIVATE ELARA_FONT_DIR="${ELARA_FONT_DIR}")

add_subdirectory(cli)

# Benchmarks: built on demand (`python scripts/elara.py bench`, release preset),
# never part of CTest or the gate.
add_executable(elara_benchmarks EXCLUDE_FROM_ALL bench/bench_core.cpp bench/bench_io.cpp
  bench/bench_notation.cpp)
target_link_libraries(elara_benchmarks PRIVATE elara::core elara::io elara::notation
  Catch2::Catch2WithMain)
target_compile_definitions(elara_benchmarks PRIVATE ELARA_FONT_DIR="${ELARA_FONT_DIR}")
elara_target_warnings(elara_benchmarks)

if(TARGET elara)
  add_test(NAME app.smoke COMMAND elara --smoke-test)
  set_tests_properties(app.smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60)
endif()
