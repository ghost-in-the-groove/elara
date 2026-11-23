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
    core/test_clipboard.cpp
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
    core/test_entry.cpp
    core/test_drum_entry.cpp
    core/test_validation.cpp
    core/test_performance.cpp
    core/test_transpose.cpp
  LIBRARIES elara::core)
target_compile_definitions(elara_core_tests PRIVATE ELARA_EXPECTED_VERSION="${PROJECT_VERSION}")

elara_add_unit_tests(audio
  SOURCES audio/test_offline_renderer.cpp audio/test_player.cpp audio/test_sequence.cpp
          audio/test_synth.cpp audio/test_tone_generator.cpp
          audio/test_wav_reader.cpp audio/test_wav_writer.cpp audio/test_pitch_detector.cpp
          audio/test_tuner.cpp
  LIBRARIES elara::audio)

find_package(miniz CONFIG REQUIRED) # the .mxl tests build archives
elara_add_unit_tests(io
  SOURCES io/test_native_format.cpp io/test_musicxml.cpp io/test_musicxml_export.cpp io/test_midi.cpp io/test_guitar_pro.cpp
          io/test_guitar_pro_binary.cpp io/test_guitar_pro_gpx.cpp io/test_open.cpp
  LIBRARIES elara::io miniz::miniz)
target_compile_definitions(elara_io_tests PRIVATE ELARA_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")

elara_add_unit_tests(notation
  SOURCES notation/test_cursor.cpp notation/test_smufl.cpp notation/test_staff_layout.cpp
  notation/test_page_layout.cpp
  notation/test_tab_layout.cpp notation/test_repeat_layout.cpp
  LIBRARIES elara::notation)
target_compile_definitions(elara_notation_tests PRIVATE ELARA_FONT_DIR="${ELARA_FONT_DIR}")

add_subdirectory(cli)

# Benchmarks: built on demand (`python scripts/elara.py bench`, release preset),
# never part of CTest or the gate.
add_executable(elara_benchmarks EXCLUDE_FROM_ALL bench/bench_audio.cpp bench/bench_core.cpp
  bench/bench_io.cpp bench/bench_notation.cpp)
target_link_libraries(elara_benchmarks PRIVATE elara::audio elara::core elara::io elara::notation
  Catch2::Catch2WithMain)
target_compile_definitions(elara_benchmarks PRIVATE ELARA_FONT_DIR="${ELARA_FONT_DIR}")
elara_target_warnings(elara_benchmarks)

if(TARGET elara)
  # The app's painting, on the offscreen platform (main.cpp sets it).
  add_executable(elara_app_tests app/main.cpp app/test_score_painter.cpp app/bench_score_painter.cpp)
  target_link_libraries(elara_app_tests PRIVATE elara_app_view elara::io Catch2::Catch2)
  target_compile_definitions(elara_app_tests PRIVATE
    ELARA_FONT_DIR="${ELARA_FONT_DIR}"
    ELARA_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
  elara_target_warnings(elara_app_tests)
  catch_discover_tests(elara_app_tests
    TEST_PREFIX "app."
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "$<TARGET_FILE_DIR:Qt6::Core>")

  # The window without a screen: it loads cleanly and, given a file, lays it
  # out, paints its first page and shows it in every view and with all tracks.
  add_test(NAME app.smoke COMMAND elara --smoke-test)
  add_test(NAME app.smoke_open_tracks
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/quartet.gp4")
  add_test(NAME app.smoke_open_views
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/guitar-techniques.gp")
  add_test(NAME app.smoke_open_missing
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/missing.gp")
  # The tuner opened at start. Without an audio input (as in CI) it says so
  # instead of listening; either way its QML must load without warnings.
  add_test(NAME app.smoke_tuner COMMAND elara --smoke-test --tuner)
  set_tests_properties(app.smoke app.smoke_open_tracks app.smoke_open_views app.smoke_open_missing
    app.smoke_tuner
    PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
      TIMEOUT 60)
  set_tests_properties(app.smoke_open_tracks PROPERTIES
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(1 pages, [0-9]+ painted, 6 layouts\\)")
  set_tests_properties(app.smoke_open_views PROPERTIES
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(1 pages, [0-9]+ painted, 4 layouts\\)")
  set_tests_properties(app.smoke_open_missing PROPERTIES WILL_FAIL TRUE)

  # The UI in Spanish: the catalog compiled into the executable loads and the
  # window loads with it. Every string has a translation in every language.
  add_test(NAME app.smoke_spanish COMMAND elara --smoke-test --language es)
  add_test(NAME app.language_unknown COMMAND elara --smoke-test --language xx)
  set_tests_properties(app.smoke_spanish app.language_unknown PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60)
  set_tests_properties(app.smoke_spanish PROPERTIES
    PASS_REGULAR_EXPRESSION "ui language: es[\r\n]+smoke-test: ok")
  set_tests_properties(app.language_unknown PROPERTIES
    PASS_REGULAR_EXPRESSION "no translation for language \"xx\"")
  add_test(NAME app.translations_complete
    COMMAND ${CMAKE_COMMAND}
      -DLUPDATE=$<TARGET_FILE:Qt6::lupdate>
      -DLCONVERT=$<TARGET_FILE:Qt6::lconvert>
      -DAPP_DIR=${PROJECT_SOURCE_DIR}/src/app
      -DSOURCE_LANGUAGE=en
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/translations
      -P ${CMAKE_CURRENT_SOURCE_DIR}/app/check_translations.cmake)
endif()
