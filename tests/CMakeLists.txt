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
    core/test_lyrics.cpp
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
          audio/test_flac_reader.cpp
  LIBRARIES elara::audio elara::audio_fluid)

# The FLAC decoder against libFLAC, which encodes the test files through
# libsndfile (ADR-0067); it leaves a440.flac for the CLI tests.
find_package(SndFile CONFIG REQUIRED)
add_executable(elara_check_flac audio/check_flac.cpp)
target_link_libraries(elara_check_flac PRIVATE elara::audio SndFile::sndfile)
elara_target_warnings(elara_check_flac)
add_test(NAME audio.flac_against_libflac
  COMMAND elara_check_flac "${CMAKE_CURRENT_BINARY_DIR}/flac")
set_tests_properties(audio.flac_against_libflac PROPERTIES
  FIXTURES_SETUP flac_files
  PASS_REGULAR_EXPRESSION "flac check: ok")

find_package(miniz CONFIG REQUIRED) # the .mxl tests build archives
# Writes the test SoundFont for the elara-cli tests that play one (ADR-0043).
add_executable(elara_make_test_soundfont audio/make_test_soundfont.cpp)
elara_target_warnings(elara_make_test_soundfont)
# Writes the test SFZ instrument for the elara-cli tests that play one (ADR-0066).
add_executable(elara_make_test_sfz audio/make_test_sfz.cpp)
target_link_libraries(elara_make_test_sfz PRIVATE elara::audio SndFile::sndfile)
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
  notation/test_incremental_layout.cpp notation/test_pdf.cpp notation/test_chord_layout.cpp notation/test_beat_text_layout.cpp
  notation/test_lyrics_layout.cpp notation/test_fermata_layout.cpp
  notation/test_slur_layout.cpp notation/test_slide_layout.cpp notation/test_ghost_layout.cpp
  notation/test_clef_layout.cpp notation/test_octave_layout.cpp
  notation/test_measure_repeat_layout.cpp
  notation/test_ornament_layout.cpp notation/test_pedal_layout.cpp
  notation/test_fingering_layout.cpp
  LIBRARIES elara::notation elara::io)
# The incremental layout is checked against a full one on the corpus files.
target_compile_definitions(elara_notation_tests PRIVATE ELARA_FONT_DIR="${ELARA_FONT_DIR}"
  ELARA_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")

add_subdirectory(cli)

# Benchmarks: built on demand (`python scripts/elara.py bench`, release preset),
# never part of CTest or the gate.
add_executable(elara_benchmarks EXCLUDE_FROM_ALL bench/bench_audio.cpp bench/bench_core.cpp
  bench/bench_io.cpp bench/bench_notation.cpp)
# libsndfile encodes the sampler's and the FLAC decoder's input, as in the tests.
target_link_libraries(elara_benchmarks PRIVATE elara::audio elara::core elara::io elara::notation
  SndFile::sndfile Catch2::Catch2WithMain)
target_compile_definitions(elara_benchmarks PRIVATE ELARA_FONT_DIR="${ELARA_FONT_DIR}")
elara_target_warnings(elara_benchmarks)

if(TARGET elara)
  # The app's painting, on the offscreen platform (main.cpp sets it).
  add_executable(elara_app_tests app/main.cpp app/test_score_painter.cpp app/test_track_list.cpp
    app/test_when_done.cpp app/test_app_icon.cpp app/test_included_sound.cpp
    app/test_recent_scores.cpp app/test_recovery_store.cpp app/test_job_lane.cpp
    app/bench_score_painter.cpp)
  target_link_libraries(elara_app_tests PRIVATE elara_app_view elara_app_icon elara::io
    Qt6::Concurrent Catch2::Catch2)
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
  # The groups of the command bar and of the tool row must each fit in the
  # window beside the others of their bar.
  add_test(NAME app.smoke COMMAND elara --smoke-test)
  # The same in the narrowest window, where the command bar and the tool row
  # give up what the menus also offer.
  add_test(NAME app.smoke_narrow
    COMMAND elara --smoke-test --size 760x480 "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/band.gp")
  add_test(NAME app.smoke_open_tracks
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/quartet.gp4")
  add_test(NAME app.smoke_open_views
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/guitar-techniques.gp")
  add_test(NAME app.smoke_open_missing
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/missing.gp")
  # The tuner opened at start. Without an audio input (as in CI) it says so
  # instead of listening; either way its QML must load without warnings.
  add_test(NAME app.smoke_tuner COMMAND elara --smoke-test --tuner)
  # The tuner open over a bass: it tunes to the bass's strings, to a tuning of
  # the track's own when the bass is tuned a whole step down, and back on undo.
  add_test(NAME app.smoke_tuner_track
    COMMAND elara --smoke-test --tuner "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/band.gp")
  # The song panel open, with the first track's tuning, while the song is edited.
  add_test(NAME app.smoke_song_pane
    COMMAND elara --smoke-test --tuning "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/quartet.gp4")
  # The measure popup open at the caret while the measures are edited.
  add_test(NAME app.smoke_measure
    COMMAND elara --smoke-test --measure "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/guitar-techniques.gp")
  # The lyrics popup open over a staff that sings while a verse is typed.
  add_test(NAME app.smoke_lyrics
    COMMAND elara --smoke-test --lyrics "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/quartet.gp4")
  # The note effects panel open at the caret while effects are set.
  add_test(NAME app.smoke_effects
    COMMAND elara --smoke-test --effects "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/guitar-techniques.gp")
  set_tests_properties(app.smoke app.smoke_narrow app.smoke_open_tracks app.smoke_open_views
    app.smoke_open_missing
    app.smoke_tuner app.smoke_tuner_track app.smoke_song_pane app.smoke_measure app.smoke_lyrics
    app.smoke_effects
    PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
      TIMEOUT 60)
  set_tests_properties(app.smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(1 pages, [0-9]+ painted, 59 layouts\\)")
  set_tests_properties(app.smoke_song_pane PROPERTIES
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(1 pages, [0-9]+ painted, 61 layouts\\)")
  # The file opened is the first of the recent scores (ADR-0071) with the two
  # things its import dropped, and the
  # score exports as a PDF document of its one page (ADR-0074), as MusicXML
  # with its four tracks and as MIDI, each telling what it left out.
  set_tests_properties(app.smoke_open_tracks PROPERTIES
    PASS_REGULAR_EXPRESSION "recent: quartet \\(GP4\\)[\r\n]+import: 2 warnings[\r\n]+.*pdf: 1 page, [0-9]+ bytes[\r\n]+musicxml: 4 tracks, 4 warnings[\r\n]+midi: [0-9]+ bytes, [1-9][0-9]* warnings[\r\n]+.*smoke-test: ok \\(1 pages, [0-9]+ painted, 61 layouts\\)")
  set_tests_properties(app.smoke_open_views app.smoke_measure app.smoke_effects PROPERTIES
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(1 pages, [0-9]+ painted, 57 layouts\\)")
  set_tests_properties(app.smoke_narrow PROPERTIES PASS_REGULAR_EXPRESSION "smoke-test: ok")
  set_tests_properties(app.smoke_open_missing PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(app.smoke_tuner_track PROPERTIES
    PASS_REGULAR_EXPRESSION "tuner: E1 A1 D2 G2[\r\n]+tuner: D1 G1 C2 F2[\r\n]+tuner: E1 A1 D2 G2[\r\n]+.*smoke-test: ok")

  # Recovery copies (ADR-0106): a run edits a new song and, once the song's
  # copy is written, quits at once as a crash would; the next start offers the
  # copy back and recovers it as an unsaved song.
  set(_recovery_folder "${CMAKE_CURRENT_BINARY_DIR}/recovery-check")
  add_test(NAME app.smoke_recovery_clean
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_recovery_folder}")
  add_test(NAME app.smoke_recovery_crash
    COMMAND elara --smoke-test --recovery-folder "${_recovery_folder}" --crash-after-recovery-copy)
  add_test(NAME app.smoke_recovery
    COMMAND elara --smoke-test --recovery-folder "${_recovery_folder}")
  set_tests_properties(app.smoke_recovery_clean PROPERTIES FIXTURES_SETUP recovery_clean)
  set_tests_properties(app.smoke_recovery_crash app.smoke_recovery PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60)
  set_tests_properties(app.smoke_recovery_crash PROPERTIES
    FIXTURES_REQUIRED recovery_clean
    FIXTURES_SETUP recovery_left
    PASS_REGULAR_EXPRESSION "smoke-test: ok \\(left a recovery copy\\)")
  set_tests_properties(app.smoke_recovery PROPERTIES
    FIXTURES_REQUIRED recovery_left
    PASS_REGULAR_EXPRESSION "recovery: Left by a crash[\r\n]+.*smoke-test: ok")

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

  # The SoundFont that comes with the packages (ADR-0069), where they put it
  # around the app: the test SoundFont, there for this test alone.
  if(APPLE)
    set(_included_soundfonts "$<TARGET_BUNDLE_CONTENT_DIR:elara>/Resources/soundfonts")
  else()
    set(_included_soundfonts "$<TARGET_FILE_DIR:elara>/../share/elara/soundfonts")
  endif()
  add_test(NAME app.included_soundfont_put
    COMMAND "${CMAKE_COMMAND}" "-DFROM=${CMAKE_CURRENT_BINARY_DIR}/cli/test-soundfont.sf2"
      "-DTO=${_included_soundfonts}" -P "${CMAKE_CURRENT_SOURCE_DIR}/app/included_soundfont.cmake")
  add_test(NAME app.included_soundfont_remove
    COMMAND "${CMAKE_COMMAND}" "-DTO=${_included_soundfonts}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/app/included_soundfont.cmake")
  # Before the SoundFont is put there, the option fails with a message.
  set_tests_properties(app.included_soundfont_put PROPERTIES
    FIXTURES_REQUIRED cli_soundfont FIXTURES_SETUP app_included_soundfont
    DEPENDS app.included_soundfont_none)
  set_tests_properties(app.included_soundfont_remove PROPERTIES
    FIXTURES_CLEANUP app_included_soundfont)
  add_test(NAME app.smoke_included_soundfont
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/band.gp" --included-soundfont)
  set_tests_properties(app.smoke_included_soundfont PROPERTIES
    FIXTURES_REQUIRED app_included_soundfont
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60
    PASS_REGULAR_EXPRESSION "sound: test-soundfont[\r\n]+pages kept: [0-9]+[\r\n]+smoke-test: ok")
  add_test(NAME app.included_soundfont_none COMMAND elara --included-soundfont)
  set_tests_properties(app.included_soundfont_none PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    PASS_REGULAR_EXPRESSION "no SoundFont comes with this build of Elara")

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

  # The guitar SFZ instrument that comes with the packages (ADR-0072), where
  # they put it around the app: the CLI tests' SFZ folder, there for this test
  # alone, whose first instrument by name is test-flac.
  if(APPLE)
    set(_included_sfz "$<TARGET_BUNDLE_CONTENT_DIR:elara>/Resources/sfz")
  else()
    set(_included_sfz "$<TARGET_FILE_DIR:elara>/../share/elara/sfz")
  endif()
  add_test(NAME app.included_guitar_sfz_put
    COMMAND "${CMAKE_COMMAND}" "-DFROM=${CMAKE_CURRENT_BINARY_DIR}/cli/test-sfz"
      "-DTO=${_included_sfz}" -P "${CMAKE_CURRENT_SOURCE_DIR}/app/included_soundfont.cmake")
  add_test(NAME app.included_guitar_sfz_remove
    COMMAND "${CMAKE_COMMAND}" "-DTO=${_included_sfz}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/app/included_soundfont.cmake")
  # Before the instrument is put there, the option fails with a message.
  set_tests_properties(app.included_guitar_sfz_put PROPERTIES
    FIXTURES_REQUIRED cli_sfz FIXTURES_SETUP app_included_guitar_sfz
    DEPENDS app.included_guitar_sfz_none)
  set_tests_properties(app.included_guitar_sfz_remove PROPERTIES
    FIXTURES_CLEANUP app_included_guitar_sfz)
  add_test(NAME app.smoke_included_guitar_sfz
    COMMAND elara --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/corpus/gp/guitar-techniques.gp"
      --included-guitar-sfz)
  set_tests_properties(app.smoke_included_guitar_sfz PROPERTIES
    FIXTURES_REQUIRED app_included_guitar_sfz
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    TIMEOUT 60
    PASS_REGULAR_EXPRESSION "sfz: guitars test-flac, basses none[\r\n]+sounds: test-flac[\r\n]+(.*[\r\n]+)?smoke-test: ok")
  add_test(NAME app.included_guitar_sfz_none COMMAND elara --included-guitar-sfz)
  set_tests_properties(app.included_guitar_sfz_none PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    PASS_REGULAR_EXPRESSION "no guitar SFZ instrument comes with this build of Elara")

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
