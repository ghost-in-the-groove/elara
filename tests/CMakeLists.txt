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
    core/test_jumps.cpp
    core/test_document.cpp
    core/test_effects.cpp
    core/test_entry.cpp
    core/test_drum_entry.cpp
    core/test_entry_effects.cpp
    core/test_entry_dynamics.cpp
    core/test_validation.cpp
    core/test_performance.cpp
    core/test_retune.cpp
    core/test_transpose.cpp
    core/test_parallel.cpp
  LIBRARIES elara::core)
target_compile_definitions(elara_core_tests PRIVATE ELARA_EXPECTED_VERSION="${PROJECT_VERSION}")

elara_add_unit_tests(audio
  SOURCES audio/test_offline_renderer.cpp audio/test_player.cpp audio/test_sequence.cpp
          audio/test_synth.cpp audio/test_tone_generator.cpp
          audio/test_wav_reader.cpp audio/test_wav_writer.cpp audio/test_pitch_detector.cpp
          audio/test_tuner.cpp audio/test_sound_bank.cpp audio/test_sound_font.cpp
          audio/test_sfz.cpp audio/test_sfz_bank.cpp
  LIBRARIES elara::audio elara::audio_fluid)

find_package(miniz CONFIG REQUIRED) # the .mxl tests build archives
# Writes the test SoundFont for the elara-cli tests that play one (ADR-0043).
add_executable(elara_make_test_soundfont audio/make_test_soundfont.cpp)
elara_target_warnings(elara_make_test_soundfont)
# Writes the test SFZ instrument for the elara-cli tests that play one (ADR-0066).
add_executable(elara_make_test_sfz audio/make_test_sfz.cpp)
target_link_libraries(elara_make_test_sfz PRIVATE elara::audio)
elara_target_warnings(elara_make_test_sfz)

elara_add_unit_tests(io
  SOURCES io/test_native_format.cpp io/test_musicxml.cpp io/test_musicxml_export.cpp io/test_midi.cpp io/test_guitar_pro.cpp
          io/test_guitar_pro_binary.cpp io/test_guitar_pro_gpx.cpp io/test_open.cpp
  LIBRARIES elara::io miniz::miniz)
target_compile_definitions(elara_io_tests PRIVATE ELARA_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")

elara_add_unit_tests(notation
  SOURCES notation/test_cursor.cpp notation/test_smufl.cpp notation/test_staff_layout.cpp notation/test_caret.cpp
  notation/test_page_layout.cpp
  notation/test_tab_layout.cpp notation/test_repeat_layout.cpp notation/test_tempo_layout.cpp
  notation/test_dynamics_layout.cpp notation/test_navigation_layout.cpp
  notation/test_incremental_layout.cpp
  LIBRARIES elara::notation elara::io)
# The incremental layout is checked against a full one on the corpus files.
target_compile_definitions(elara_notation_tests PRIVATE ELARA_FONT_DIR="${ELARA_FONT_DIR}"
  ELARA_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")

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
  add_executable(elara_app_tests app/main.cpp app/test_score_painter.cpp app/test_track_list.cpp
    app/test_when_done.cpp app/test_app_icon.cpp app/bench_score_painter.cpp)
  target_link_libraries(elara_app_tests PRIVATE elara_app_view elara_app_icon elara::io
    Catch2::Catch2)
  target_compile_definitions(elara_app_tests PRIVATE
    ELARA_APP_ICON_SVG="${PROJECT_SOURCE_DIR}/src/app/icons/elara.svg"
    ELARA_FONT_DIR="${ELARA_FONT_DIR}"
    ELARA_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
  elara_target_warnings(elara_app_tests)
  catch_discover_tests(elara_app_tests
    TEST_PREFIX "app."
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "$<TARGET_FILE_DIR:Qt6::Core>")

  # The window without a screen: it loads cleanly and, given a file, lays it
  # out, paints its first page and shows it in every view and with all tracks.
  # Then it edits the song (a new one without a file): adds a track, changes
  # the time signature, undoes, redoes, and checks the song ends as it began.
  # The bars floating over the bottom of the score must not cover each other.
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
  # The song panel open, with the first track's tuning, while the song is edited.
  add_test(NAME app.smoke_song_pane
    COMMAND elara --smoke-test --tuning "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/quartet.gp4")
  # The measure popup open at the caret while the measures are edited.
  add_test(NAME app.smoke_measure
    COMMAND elara --smoke-test --measure "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/guitar-techniques.gp")
  # The note effects panel open at the caret while effects are set.
  add_test(NAME app.smoke_effects
    COMMAND elara --smoke-test --effects "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/guitar-techniques.gp")
  set_tests_properties(app.smoke app.smoke_open_tracks app.smoke_open_views app.smoke_open_missing
    app.smoke_tuner app.smoke_song_pane app.smoke_measure app.smoke_effects
    PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
      TIMEOUT 60)
  set_tests_properties(app.smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(1 pages, [0-9]+ painted, 51 layouts\\)")
  set_tests_properties(app.smoke_open_tracks app.smoke_song_pane PROPERTIES
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(1 pages, [0-9]+ painted, 61 layouts\\)")
  set_tests_properties(app.smoke_open_views app.smoke_measure app.smoke_effects PROPERTIES
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(1 pages, [0-9]+ painted, 57 layouts\\)")
  set_tests_properties(app.smoke_open_missing PROPERTIES WILL_FAIL TRUE)

  # A SoundFont from the command line (ADR-0043), the test font of the CLI
  # tests: the window plays through it, keeps it when another file fails to
  # open, and goes back to the built-in synthesizer.
  add_test(NAME app.smoke_soundfont
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/band.gp"
      --soundfont "${CMAKE_CURRENT_BINARY_DIR}/cli/test-soundfont.sf2")
  set_tests_properties(app.smoke_soundfont PROPERTIES
    FIXTURES_REQUIRED cli_soundfont
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60
    PASS_REGULAR_EXPRESSION "sound: test-soundfont[\r\n]+pages kept: [0-9]+[\r\n]+smoke-test: ok")

  # SFZ instruments from the command line (ADR-0066), the test guitar of the
  # CLI tests for the basses of band.gp: its bass track plays the instrument
  # and the others keep the built-in synthesizer; a file that fails keeps it,
  # and the basses can go back to the built-in synthesizer.
  add_test(NAME app.smoke_sfz
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/band.gp"
      --bass-sfz "${CMAKE_CURRENT_BINARY_DIR}/cli/test-sfz/test-guitar.sfz")
  set_tests_properties(app.smoke_sfz PROPERTIES
    FIXTURES_REQUIRED cli_sfz
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60
    PASS_REGULAR_EXPRESSION "sfz: guitars none, basses test-guitar[\r\n]+sounds: test-guitar, piano, drums, wind[\r\n]+(.*[\r\n]+)?smoke-test: ok")

  # A song of several pages: the notes typed on the first page leave the others
  # as they were, so those keep their textures instead of being painted again.
  set(ELARA_APP_LONG_SONG "${CMAKE_CURRENT_BINARY_DIR}/app-long-song.elara")
  add_test(NAME app.make_long_song
    COMMAND elara-cli edit --out "${ELARA_APP_LONG_SONG}" "add-track electric-guitar"
      "append-measures 160")
  set_tests_properties(app.make_long_song PROPERTIES FIXTURES_SETUP app_long_song)
  add_test(NAME app.smoke_keeps_pages COMMAND elara --smoke-test "${ELARA_APP_LONG_SONG}")
  set_tests_properties(app.smoke_keeps_pages PROPERTIES
    FIXTURES_REQUIRED app_long_song
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60
    PASS_REGULAR_EXPRESSION "pages kept: [1-9][0-9]*[\r\n]+smoke-test: ok \\([0-9]+ pages")

  # The UI in Spanish: the catalog compiled into the executable loads and the
  # window loads with it. Every string has a translation in every language.
  add_test(NAME app.smoke_spanish COMMAND elara --smoke-test --language es)
  add_test(NAME app.language_unknown COMMAND elara --smoke-test --language xx)
  set_tests_properties(app.smoke_spanish app.language_unknown PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60)
  set_tests_properties(app.smoke_spanish PROPERTIES
    PASS_REGULAR_EXPRESSION "ui language: es[\r\n]+pages kept: [0-9]+[\r\n]+smoke-test: ok")
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
