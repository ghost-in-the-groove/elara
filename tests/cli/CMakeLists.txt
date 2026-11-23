# End-to-end tests of elara-cli. They need no audio hardware.

add_test(NAME cli.version COMMAND elara-cli version)
set_tests_properties(cli.version PROPERTIES
  PASS_REGULAR_EXPRESSION "^elara ${PROJECT_VERSION}")

add_test(NAME cli.version_json COMMAND elara-cli --json version)
set_tests_properties(cli.version_json PROPERTIES
  PASS_REGULAR_EXPRESSION "\"version\":\"${PROJECT_VERSION}\"")

set(_tone_wav "${CMAKE_CURRENT_BINARY_DIR}/tone.wav")
add_test(NAME cli.render_tone
  COMMAND elara-cli --json render-tone --out "${_tone_wav}" --seconds 0.5 --sample-rate 48000)
set_tests_properties(cli.render_tone PROPERTIES
  PASS_REGULAR_EXPRESSION "\"frames\":24000,\"sampleRate\":48000,\"channels\":1,\"peak\":0\\.2000"
  FIXTURES_SETUP tone_wav)

# 44-byte header + 24000 mono 16-bit frames.
add_test(NAME cli.render_tone_file
  COMMAND "${CMAKE_COMMAND}" -DFILE=${_tone_wav} -DEXPECTED_SIZE=48044
          -P "${CMAKE_CURRENT_SOURCE_DIR}/check_file_size.cmake")
set_tests_properties(cli.render_tone_file PROPERTIES FIXTURES_REQUIRED tone_wav)

add_test(NAME cli.rejects_bad_arguments
  COMMAND elara-cli render-tone --out "${_tone_wav}" --frequency 5)
set_tests_properties(cli.rejects_bad_arguments PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.pitch_from_text COMMAND elara-cli --json pitch "F#3")
set_tests_properties(cli.pitch_from_text PROPERTIES
  PASS_REGULAR_EXPRESSION "\"pitch\":\"F#3\",\"step\":\"F\",\"alter\":1,\"octave\":3,\"midi\":54,")

add_test(NAME cli.pitch_from_midi_with_flats COMMAND elara-cli --json pitch 61 --flats)
set_tests_properties(cli.pitch_from_midi_with_flats PROPERTIES
  PASS_REGULAR_EXPRESSION "\"pitch\":\"Db4\",\"step\":\"D\",\"alter\":-1,\"octave\":4,\"midi\":61,")

add_test(NAME cli.pitch_frequency COMMAND elara-cli --json pitch A4 --a4 432)
set_tests_properties(cli.pitch_frequency PROPERTIES
  PASS_REGULAR_EXPRESSION "\"midi\":69,\"frequency\":432\\.0000}")

add_test(NAME cli.pitch_rejects_bad_text COMMAND elara-cli pitch H4)
set_tests_properties(cli.pitch_rejects_bad_text PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.pitch_rejects_midi_out_of_range COMMAND elara-cli pitch 128)
set_tests_properties(cli.pitch_rejects_midi_out_of_range PROPERTIES WILL_FAIL TRUE)

# Tuning and fretboard mapping.
add_test(NAME cli.fret_standard_guitar
  COMMAND elara-cli --json fret --tuning guitar --string 6 --fret 3)
set_tests_properties(cli.fret_standard_guitar PROPERTIES
  PASS_REGULAR_EXPRESSION "\"string\":6,\"fret\":3,\"capo\":0,\"midi\":43,\"pitch\":\"G2\"")

add_test(NAME cli.fret_with_capo_and_custom_tuning
  COMMAND elara-cli --json fret --tuning "D2 A2 D3 G3 B3 E4" --capo 2 --string 6 --fret 0)
set_tests_properties(cli.fret_with_capo_and_custom_tuning PROPERTIES
  PASS_REGULAR_EXPRESSION "\"capo\":2,\"midi\":40,\"pitch\":\"E2\"")

add_test(NAME cli.positions_for_pitch
  COMMAND elara-cli --json positions C4 --tuning guitar --max-fret 12)
set_tests_properties(cli.positions_for_pitch PROPERTIES
  PASS_REGULAR_EXPRESSION "\"positions\":\\[{\"string\":2,\"fret\":1},{\"string\":3,\"fret\":5},{\"string\":4,\"fret\":10}\\]")

add_test(NAME cli.tunings_lists_presets COMMAND elara-cli --json tunings)
set_tests_properties(cli.tunings_lists_presets PROPERTIES
  PASS_REGULAR_EXPRESSION "{\"name\":\"bass-5\",\"strings\":\"B0 E1 A1 D2 G2\"}")

add_test(NAME cli.fret_rejects_unknown_tuning
  COMMAND elara-cli fret --tuning theremin --string 1 --fret 0)
set_tests_properties(cli.fret_rejects_unknown_tuning PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.fret_rejects_missing_string
  COMMAND elara-cli fret --tuning bass --string 5 --fret 0)
set_tests_properties(cli.fret_rejects_missing_string PROPERTIES WILL_FAIL TRUE)

# Key signatures, spelling in context and meters.
add_test(NAME cli.key_with_sharps COMMAND elara-cli --json key "D major")
set_tests_properties(cli.key_with_sharps PROPERTIES
  PASS_REGULAR_EXPRESSION "\"key\":\"D major\",\"fifths\":2,\"mode\":\"major\",\"tonic\":\"D\",\"accidentals\":\\[\"F#\",\"C#\"\\]")

add_test(NAME cli.key_with_flats COMMAND elara-cli --json key "bb minor")
set_tests_properties(cli.key_with_flats PROPERTIES
  PASS_REGULAR_EXPRESSION "\"key\":\"Bb minor\",\"fifths\":-5,\"mode\":\"minor\",\"tonic\":\"Bb\",\"accidentals\":\\[\"Bb\",\"Eb\",\"Ab\",\"Db\",\"Gb\"\\]")

add_test(NAME cli.key_rejects_eight_sharps COMMAND elara-cli key "G# major")
set_tests_properties(cli.key_rejects_eight_sharps PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.spell_leading_tone_in_minor COMMAND elara-cli --json spell 61 --key "D minor")
set_tests_properties(cli.spell_leading_tone_in_minor PROPERTIES
  PASS_REGULAR_EXPRESSION "\"midi\":61,\"key\":\"D minor\",\"pitch\":\"C#4\"")

add_test(NAME cli.spell_in_flat_key COMMAND elara-cli --json spell 61 --key "F major")
set_tests_properties(cli.spell_in_flat_key PROPERTIES
  PASS_REGULAR_EXPRESSION "\"pitch\":\"Db4\"")

add_test(NAME cli.meter_compound_time
  COMMAND elara-cli --json meter 6/8 --bpm 60 --beat-unit dotted-quarter)
set_tests_properties(cli.meter_compound_time PROPERTIES
  PASS_REGULAR_EXPRESSION "\"time\":\"6/8\",\"measureLength\":\"3/4\",\"beatLength\":\"1/8\",\"tempo\":{\"bpm\":60,\"beatUnit\":\"dotted-quarter\"},\"secondsPerMeasure\":2\\.0000}")

add_test(NAME cli.meter_rejects_bad_meter COMMAND elara-cli meter 5/3)
set_tests_properties(cli.meter_rejects_bad_meter PROPERTIES WILL_FAIL TRUE)

# Rests that complete a measure.
add_test(NAME cli.rests_compound_meter COMMAND elara-cli --json rests 6/8 8)
set_tests_properties(cli.rests_compound_meter PROPERTIES
  PASS_REGULAR_EXPRESSION "\"time\":\"6/8\",\"written\":\\[\"8\"\\],\"fill\":\"incomplete\",\"rests\":\\[\"8\",\"8\",\"4\\.\"\\]")

add_test(NAME cli.rests_after_triplet COMMAND elara-cli --json rests 4/4 "8*3:2")
set_tests_properties(cli.rests_after_triplet PROPERTIES
  PASS_REGULAR_EXPRESSION "\"rests\":\\[\"8\\*3:2\",\"8\\*3:2\",\"4\",\"2\"\\]")

add_test(NAME cli.rests_complete_measure COMMAND elara-cli --json rests 3/4 2.)
set_tests_properties(cli.rests_complete_measure PROPERTIES
  PASS_REGULAR_EXPRESSION "\"fill\":\"complete\",\"rests\":\\[\\]")

add_test(NAME cli.rests_rejects_overfull COMMAND elara-cli rests 4/4 1 4)
set_tests_properties(cli.rests_rejects_overfull PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.rests_rejects_bad_duration COMMAND elara-cli rests 4/4 3)
set_tests_properties(cli.rests_rejects_bad_duration PROPERTIES WILL_FAIL TRUE)

# Undoable edits on a new song (ADR-0005).
add_test(NAME cli.edit_undo_and_redo_history
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 4" "time 2 6/8" undo)
set_tests_properties(cli.edit_undo_and_redo_history PROPERTIES
  PASS_REGULAR_EXPRESSION "\"measureCount\":4,.*\"undo\":{\"count\":2,\"next\":\"Insert measures\"},\"redo\":{\"count\":1,\"next\":\"Change time signature\"},\"modified\":true")

add_test(NAME cli.edit_reports_incomplete_measure
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 1" "note 0 0 6 3 4")
set_tests_properties(cli.edit_reports_incomplete_measure PROPERTIES
  PASS_REGULAR_EXPRESSION "\"notes\":1}.*\"issues\":\\[{\"kind\":\"voice-incomplete\",\"track\":0,\"staff\":0,\"measure\":0,\"voice\":0,\"beat\":null,\"note\":null}\\]")

add_test(NAME cli.edit_fill_completes_measure
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 1" "note 0 0 6 3 4" "fill 0 0")
set_tests_properties(cli.edit_fill_completes_measure PROPERTIES
  PASS_REGULAR_EXPRESSION "\"beats\":3,\"notes\":1}.*\"issues\":\\[\\]")

add_test(NAME cli.edit_save_point_survives_undo_redo
  COMMAND elara-cli --json edit "title Etude" "append-measures 2" save undo redo)
set_tests_properties(cli.edit_save_point_survives_undo_redo PROPERTIES
  PASS_REGULAR_EXPRESSION "\"title\":\"Etude\".*\"modified\":false}")

add_test(NAME cli.edit_rejects_unknown_step COMMAND elara-cli edit "transpose 2")
set_tests_properties(cli.edit_rejects_unknown_step PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.edit_rejects_undo_without_history COMMAND elara-cli edit undo)
set_tests_properties(cli.edit_rejects_undo_without_history PROPERTIES WILL_FAIL TRUE)

# Song files: create, edit, dump (ADR-0006). The cases share files through fixtures.
set(ELARA_CLI_SONG "${CMAKE_CURRENT_BINARY_DIR}/cli-song.elara")
set(ELARA_CLI_EDITED "${CMAKE_CURRENT_BINARY_DIR}/cli-edited.elara")

add_test(NAME cli.new_creates_song
  COMMAND elara-cli --json new "${ELARA_CLI_SONG}" --force --title Etude
    --track electric-guitar --track electric-bass --measures 8 --time 6/8 --key "D major" --tempo 90)
set_tests_properties(cli.new_creates_song PROPERTIES
  FIXTURES_SETUP cli_song
  PASS_REGULAR_EXPRESSION "\"title\":\"Etude\",\"tracks\":2,\"measures\":8}")

add_test(NAME cli.dump_reads_song COMMAND elara-cli --json dump "${ELARA_CLI_SONG}")
set_tests_properties(cli.dump_reads_song PROPERTIES
  FIXTURES_REQUIRED cli_song
  PASS_REGULAR_EXPRESSION "\"info\":{\"title\":\"Etude\"},\"tempo\":{\"bpm\":90},\"measures\":\\[{\"timeSignature\":\"6/8\",\"keySignature\":\"D major\"}.*\"durationSeconds\":16\\.000,\"issues\":\\[\\]}")

add_test(NAME cli.new_refuses_to_overwrite COMMAND elara-cli new "${ELARA_CLI_SONG}")
set_tests_properties(cli.new_refuses_to_overwrite PROPERTIES
  FIXTURES_REQUIRED cli_song
  WILL_FAIL TRUE)

add_test(NAME cli.edit_file_and_save
  COMMAND elara-cli --json edit --in "${ELARA_CLI_SONG}" --out "${ELARA_CLI_EDITED}"
    "note 0 0 6 3 4." "fill 0 0" undo redo)
set_tests_properties(cli.edit_file_and_save PROPERTIES
  FIXTURES_REQUIRED cli_song
  FIXTURES_SETUP cli_edited
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":2,\"next\":\"Edit measure\"},\"redo\":{\"count\":0,\"next\":null},\"modified\":false}")

add_test(NAME cli.dump_reads_edited_song COMMAND elara-cli --json dump "${ELARA_CLI_EDITED}")
set_tests_properties(cli.dump_reads_edited_song PROPERTIES
  FIXTURES_REQUIRED cli_edited
  PASS_REGULAR_EXPRESSION "\"measures\":\\[\\[\\[{\"duration\":\"4\\.\",\"notes\":\\[{\"pitch\":\"G2\",\"string\":6,\"fret\":3}\\]},{\"duration\":\"4\\.\"}\\]\\]\\]")

add_test(NAME cli.dump_rejects_missing_file
  COMMAND elara-cli dump "${CMAKE_CURRENT_BINARY_DIR}/no-such-song.elara")
set_tests_properties(cli.dump_rejects_missing_file PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.dump_rejects_other_files
  COMMAND elara-cli dump "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
set_tests_properties(cli.dump_rejects_other_files PROPERTIES WILL_FAIL TRUE)

# Import (ADR-0007) from the self-made corpus.
set(ELARA_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/../corpus")
set(ELARA_CLI_IMPORTED "${CMAKE_CURRENT_BINARY_DIR}/cli-imported.elara")

add_test(NAME cli.import_musicxml
  COMMAND elara-cli --json import "${ELARA_CORPUS}/musicxml/melody.musicxml")
set_tests_properties(cli.import_musicxml PROPERTIES
  PASS_REGULAR_EXPRESSION "\"format\":\"musicxml\",\"out\":null,\"warnings\":\\[\\],\"song\":{.*\"title\":\"Corpus Melody\".*\"issues\":\\[\\]}")

add_test(NAME cli.import_musicxml_reports_warnings
  COMMAND elara-cli --json import "${ELARA_CORPUS}/musicxml/piano-voices.musicxml")
set_tests_properties(cli.import_musicxml_reports_warnings PROPERTIES
  PASS_REGULAR_EXPRESSION "\"warnings\":\\[[^]]*\"Piano: grace notes dropped \\(measure 2\\)\"")

add_test(NAME cli.import_musicxml_saves_native
  COMMAND elara-cli import "${ELARA_CORPUS}/musicxml/guitar-tab.musicxml"
    --out "${ELARA_CLI_IMPORTED}" --force)
set_tests_properties(cli.import_musicxml_saves_native PROPERTIES FIXTURES_SETUP cli_imported)

add_test(NAME cli.dump_reads_imported_song COMMAND elara-cli --json dump "${ELARA_CLI_IMPORTED}")
set_tests_properties(cli.dump_reads_imported_song PROPERTIES
  FIXTURES_REQUIRED cli_imported
  PASS_REGULAR_EXPRESSION "\"clef\":\"treble-8vb\",\"tuning\":\"D2 A2 D3 G3 B3 E4\",\"capo\":2")

add_test(NAME cli.import_rejects_unknown_types
  COMMAND elara-cli import "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
set_tests_properties(cli.import_rejects_unknown_types PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.import_rejects_missing_file
  COMMAND elara-cli import "${CMAKE_CURRENT_BINARY_DIR}/no-such-score.musicxml")
set_tests_properties(cli.import_rejects_missing_file PROPERTIES WILL_FAIL TRUE)

# Export (ADR-0007): the imported guitar song goes back to MusicXML and in again.
set(ELARA_CLI_EXPORTED "${CMAKE_CURRENT_BINARY_DIR}/cli-exported.musicxml")

add_test(NAME cli.export_musicxml
  COMMAND elara-cli --json export "${ELARA_CLI_IMPORTED}" "${ELARA_CLI_EXPORTED}" --force)
set_tests_properties(cli.export_musicxml PROPERTIES
  FIXTURES_REQUIRED cli_imported
  FIXTURES_SETUP cli_exported
  PASS_REGULAR_EXPRESSION "\"format\":\"musicxml\",\"compressed\":false,\"bytes\":[1-9][0-9]*,\"warnings\":\\[\\]}")

add_test(NAME cli.import_reads_exported_musicxml
  COMMAND elara-cli --json import "${ELARA_CLI_EXPORTED}")
set_tests_properties(cli.import_reads_exported_musicxml PROPERTIES
  FIXTURES_REQUIRED cli_exported
  PASS_REGULAR_EXPRESSION "\"warnings\":\\[\\],.*\"clef\":\"treble-8vb\",\"tuning\":\"D2 A2 D3 G3 B3 E4\",\"capo\":2")

add_test(NAME cli.export_mxl
  COMMAND elara-cli --json export "${ELARA_CLI_IMPORTED}"
    "${CMAKE_CURRENT_BINARY_DIR}/cli-exported.mxl" --force)
set_tests_properties(cli.export_mxl PROPERTIES
  FIXTURES_REQUIRED cli_imported
  PASS_REGULAR_EXPRESSION "\"compressed\":true")

add_test(NAME cli.export_refuses_to_replace
  COMMAND elara-cli export "${ELARA_CLI_IMPORTED}" "${ELARA_CLI_EXPORTED}")
set_tests_properties(cli.export_refuses_to_replace PROPERTIES
  FIXTURES_REQUIRED cli_exported
  WILL_FAIL TRUE)

add_test(NAME cli.export_rejects_unknown_types
  COMMAND elara-cli export "${ELARA_CLI_IMPORTED}" "${CMAKE_CURRENT_BINARY_DIR}/song.txt")
set_tests_properties(cli.export_rejects_unknown_types PROPERTIES
  FIXTURES_REQUIRED cli_imported
  WILL_FAIL TRUE)

# MIDI (ADR-0009): the imported guitar song goes out as MIDI and in again.
set(ELARA_CLI_MIDI "${CMAKE_CURRENT_BINARY_DIR}/cli-exported.mid")

add_test(NAME cli.export_midi
  COMMAND elara-cli --json export "${ELARA_CLI_IMPORTED}" "${ELARA_CLI_MIDI}" --force)
set_tests_properties(cli.export_midi PROPERTIES
  FIXTURES_REQUIRED cli_imported
  FIXTURES_SETUP cli_midi
  PASS_REGULAR_EXPRESSION "\"format\":\"midi\",\"compressed\":false")

add_test(NAME cli.import_reads_exported_midi
  COMMAND elara-cli --json import "${ELARA_CLI_MIDI}")
set_tests_properties(cli.import_reads_exported_midi PROPERTIES
  FIXTURES_REQUIRED cli_midi
  PASS_REGULAR_EXPRESSION "\"format\":\"midi\".*\"issues\":\\[\\]")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/damaged.mid" "MThd, but not a MIDI file")
add_test(NAME cli.import_rejects_damaged_midi
  COMMAND elara-cli import "${CMAKE_CURRENT_BINARY_DIR}/damaged.mid")
set_tests_properties(cli.import_rejects_damaged_midi PROPERTIES WILL_FAIL TRUE)

# Guitar Pro (ADR-0010): read only.
add_test(NAME cli.import_guitar_pro
  COMMAND elara-cli --json import "${ELARA_CORPUS}/gp/guitar-techniques.gp")
set_tests_properties(cli.import_guitar_pro PROPERTIES
  PASS_REGULAR_EXPRESSION "\"format\":\"guitarpro\".*\"title\":\"Corpus Guitar Techniques\".*\"capo\":2")

add_test(NAME cli.export_rejects_guitar_pro
  COMMAND elara-cli export "${ELARA_CLI_IMPORTED}" "${CMAKE_CURRENT_BINARY_DIR}/out.gp")
set_tests_properties(cli.export_rejects_guitar_pro PROPERTIES
  FIXTURES_REQUIRED cli_imported WILL_FAIL TRUE)

# Paths are UTF-8 on every platform (on Windows through the elara-cli manifest).
set(_unicode_score "${CMAKE_CURRENT_BINARY_DIR}/canción-曲.musicxml")
file(COPY_FILE "${ELARA_CORPUS}/musicxml/melody.musicxml" "${_unicode_score}" ONLY_IF_DIFFERENT)
add_test(NAME cli.import_unicode_path COMMAND elara-cli --json import "${_unicode_score}")
set_tests_properties(cli.import_unicode_path PROPERTIES
  PASS_REGULAR_EXPRESSION "\"file\":\"[^\"]*canción-曲\\.musicxml\",\"format\":\"musicxml\"")

# Snapshot tests: every corpus file imported with --json must give what
# tests/snapshots/import/<folder>/<name>.json holds. To refresh them after an
# intended change: ELARA_UPDATE_SNAPSHOTS=1 python scripts/elara.py test -R snapshot
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB _corpus_files CONFIGURE_DEPENDS
  "${ELARA_CORPUS}/musicxml/*.musicxml"
  "${ELARA_CORPUS}/musicxml/*.mxl"
  "${ELARA_CORPUS}/midi/*.mid"
  "${ELARA_CORPUS}/gp/*.gp"
  "${ELARA_CORPUS}/gp/*.gp[345]"
  "${ELARA_CORPUS}/gp/*.gpx")
foreach(_file IN LISTS _corpus_files)
  file(RELATIVE_PATH _relative "${ELARA_CORPUS}" "${_file}")
  string(REGEX REPLACE "\.[^./]+$" "" _name "${_relative}")
  string(REGEX REPLACE "[/.-]" "_" _test "${_name}")
  add_test(NAME cli.snapshot_${_test}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_snapshot.py"
            "$<TARGET_FILE:elara-cli>" "${_file}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../snapshots/import/${_name}.json")
endforeach()
