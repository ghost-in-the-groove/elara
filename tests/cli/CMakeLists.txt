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

# The tuner hears the test tone: 440 Hz is A4, and 31.8 cents sharp of an A4 at 432 Hz.
add_test(NAME cli.tune_tone COMMAND elara-cli --json tune "${_tone_wav}")
set_tests_properties(cli.tune_tone PROPERTIES
  FIXTURES_REQUIRED tone_wav
  PASS_REGULAR_EXPRESSION "\"sampleRate\":48000,\"windows\":13,\"pitched\":13,\"a4\":440,\"frequency\":(439\\.99[0-9]*|440(\\.00[0-9]*)?),\"note\":\"A4\",\"cents\":-?0(\\.0[0-9]*)?,\"clarity\":(0\\.99[0-9]*|1)}")

# A FLAC recording, encoded by libFLAC (audio.flac_against_libflac), tunes the same.
add_test(NAME cli.tune_flac COMMAND elara-cli --json tune "${CMAKE_BINARY_DIR}/tests/flac/a440.flac")
set_tests_properties(cli.tune_flac PROPERTIES
  FIXTURES_REQUIRED flac_files
  PASS_REGULAR_EXPRESSION "\"sampleRate\":44100,.*\"note\":\"A4\",\"cents\":-?0(\\.[0-9]*)?,")

add_test(NAME cli.tune_reference COMMAND elara-cli tune "${_tone_wav}" --a4 432)
set_tests_properties(cli.tune_reference PROPERTIES
  FIXTURES_REQUIRED tone_wav
  PASS_REGULAR_EXPRESSION "^A4 \\+31\\.8 cents \\(440\\.00 Hz, clarity [0-9.]+, 13 of 13 windows\\)")

set(_low_a_wav "${CMAKE_CURRENT_BINARY_DIR}/low_a.wav")
add_test(NAME cli.tune_render_low_a
  COMMAND elara-cli render-tone --out "${_low_a_wav}" --frequency 110 --seconds 0.5)
set_tests_properties(cli.tune_render_low_a PROPERTIES FIXTURES_SETUP low_a_wav)

add_test(NAME cli.tune_string COMMAND elara-cli --json tune "${_low_a_wav}" --tuning guitar)
set_tests_properties(cli.tune_string PROPERTIES
  FIXTURES_REQUIRED low_a_wav
  PASS_REGULAR_EXPRESSION "\"note\":\"A2\",\"cents\":-?0(\\.0[0-9]*)?,\"clarity\":[0-9.]+,\"tuning\":\"[^\"]+\",\"string\":5,\"stringPitch\":\"A2\",\"stringCents\":-?0(\\.0[0-9]*)?}")

add_test(NAME cli.tune_string_text COMMAND elara-cli tune "${_low_a_wav}" --tuning "D2 A2 D3 G3 B3 E4")
set_tests_properties(cli.tune_string_text PROPERTIES
  FIXTURES_REQUIRED low_a_wav
  PASS_REGULAR_EXPRESSION "\nstring 5 \\(A2\\) \\+0\\.0 cents")

add_test(NAME cli.tune_rejects_missing_file
  COMMAND elara-cli tune "${CMAKE_CURRENT_BINARY_DIR}/missing.wav")
set_tests_properties(cli.tune_rejects_missing_file PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.tune_rejects_other_files COMMAND elara-cli tune "${ELARA_CORPUS}/gp/band.gp")
set_tests_properties(cli.tune_rejects_other_files PROPERTIES WILL_FAIL TRUE)

# Listening needs an input device, which CI does not have; bad arguments fail
# before the device is opened.
add_test(NAME cli.listen_rejects_bad_duration COMMAND elara-cli listen --seconds 0)
set_tests_properties(cli.listen_rejects_bad_duration PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli.listen_rejects_unknown_tuning COMMAND elara-cli listen --tuning banjo-9)
set_tests_properties(cli.listen_rejects_unknown_tuning PROPERTIES
  PASS_REGULAR_EXPRESSION "unknown tuning: banjo-9")

add_test(NAME cli.tune_rejects_unknown_tuning
  COMMAND elara-cli tune "${_tone_wav}" --tuning banjo-9)
set_tests_properties(cli.tune_rejects_unknown_tuning PROPERTIES
  FIXTURES_REQUIRED tone_wav
  WILL_FAIL TRUE)

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
  PASS_REGULAR_EXPRESSION "\"measureCount\":4,.*\"undo\":{\"count\":2,\"next\":\"Insert measures\"},\"redo\":{\"count\":1,\"next\":\"Change time signature\"},\"historyBytes\":[0-9]+,\"modified\":true")

# The history says how much it holds: a removed track is kept to undo (ADR-0121).
add_test(NAME cli.edit_history_bytes
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 4" "remove-track 0")
set_tests_properties(cli.edit_history_bytes PROPERTIES
  PASS_REGULAR_EXPRESSION "\"historyBytes\":[1-9][0-9]+,")
add_test(NAME cli.edit_history_bytes_none
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 4")
set_tests_properties(cli.edit_history_bytes_none PROPERTIES
  PASS_REGULAR_EXPRESSION "\"historyBytes\":0,")

# A signature change runs until the next change, as in the app.
add_test(NAME cli.edit_signature_runs_to_next_change
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 4" "time 2 6/8"
    "time 1 3/4" "key 0 D major")
set_tests_properties(cli.edit_signature_runs_to_next_change PROPERTIES
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"D major\"},{\"time\":\"3/4\",\"key\":\"D major\"},{\"time\":\"6/8\",\"key\":\"D major\"},{\"time\":\"6/8\",\"key\":\"D major\"}\\].*\"undo\":{\"count\":5,")

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

# Song credits (ADR-0059): each line on its own, as one undo step; a credit
# with no text clears the line, and one that reads the text already is no edit.
set(ELARA_CLI_CREDITS "${CMAKE_CURRENT_BINARY_DIR}/cli-credits.elara")
add_test(NAME cli.edit_sets_credits
  COMMAND elara-cli --json edit --out "${ELARA_CLI_CREDITS}" "title Etude"
    "credit subtitle for two guitars" "credit album Studies" "credit words A. Poet"
    "credit copyright CC0" "credit transcriber Someone" "credit artist The Band"
    "credit artist" "credit album Studies")
set_tests_properties(cli.edit_sets_credits PROPERTIES
  FIXTURES_SETUP cli_credits
  PASS_REGULAR_EXPRESSION "\"title\":\"Etude\".*\"undo\":{\"count\":8,\"next\":\"Edit song info\"}")
add_test(NAME cli.dump_credits COMMAND elara-cli --json dump "${ELARA_CLI_CREDITS}")
set_tests_properties(cli.dump_credits PROPERTIES
  FIXTURES_REQUIRED cli_credits
  PASS_REGULAR_EXPRESSION "\"info\":{\"title\":\"Etude\",\"subtitle\":\"for two guitars\",\"album\":\"Studies\",\"words\":\"A. Poet\",\"copyright\":\"CC0\",\"transcriber\":\"Someone\"}")
add_test(NAME cli.edit_rejects_unknown_credit COMMAND elara-cli edit "credit composer Someone")
set_tests_properties(cli.edit_rejects_unknown_credit PROPERTIES
  PASS_REGULAR_EXPRESSION "credit: title, subtitle, artist, album, words, music, copyright, transcriber or notes, not composer")

# Sections (ADR-0062): named where they start; no name takes one off.
set(ELARA_CLI_SECTIONS "${CMAKE_CURRENT_BINARY_DIR}/cli-sections.elara")
add_test(NAME cli.edit_names_sections
  COMMAND elara-cli --json edit --out "${ELARA_CLI_SECTIONS}" "add-track electric-guitar"
    "append-measures 4" "section 0 Intro" "section 2 First chorus" "section 3 Bridge"
    "section 3")
set_tests_properties(cli.edit_names_sections PROPERTIES
  FIXTURES_SETUP cli_sections
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":6,\"next\":\"Change section\"}")
add_test(NAME cli.dump_sections COMMAND elara-cli --json dump "${ELARA_CLI_SECTIONS}")
set_tests_properties(cli.dump_sections PROPERTIES
  FIXTURES_REQUIRED cli_sections
  PASS_REGULAR_EXPRESSION "\"section\":\"Intro\"},{[^}]*},{[^}]*\"section\":\"First chorus\"},{[^}]*}\\]")
add_test(NAME cli.edit_rejects_section_past_the_end
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 2" "section 5 Outro")
set_tests_properties(cli.edit_rejects_section_past_the_end PROPERTIES
  PASS_REGULAR_EXPRESSION "section: no measure 5")

# A track's name, to the end of the step; undoable as "Rename track".
add_test(NAME cli.edit_renames_track
  COMMAND elara-cli --json edit "add-track electric-bass" "rename-track 0 Bass, fingers")
set_tests_properties(cli.edit_renames_track PROPERTIES
  PASS_REGULAR_EXPRESSION "\"name\":\"Bass, fingers\".*\"next\":\"Rename track\"")

add_test(NAME cli.edit_rejects_rename_of_a_missing_track
  COMMAND elara-cli edit "add-track electric-bass" "rename-track 3 Nobody")
set_tests_properties(cli.edit_rejects_rename_of_a_missing_track PROPERTIES
  PASS_REGULAR_EXPRESSION "step 2 \\(rename-track 3 Nobody\\)")

add_test(NAME cli.edit_sets_track_mix
  COMMAND elara-cli --json edit "add-track electric-bass" "mix 0 0.5 -0.25 solo")
set_tests_properties(cli.edit_sets_track_mix PROPERTIES
  PASS_REGULAR_EXPRESSION "\"mix\":{\"volume\":0.5,\"pan\":-0.25,\"mute\":false,\"solo\":true}.*\"next\":\"Change mix\"")

add_test(NAME cli.edit_rejects_mix_out_of_range
  COMMAND elara-cli edit "add-track electric-bass" "mix 0 1.5 0")
set_tests_properties(cli.edit_rejects_mix_out_of_range PROPERTIES
  PASS_REGULAR_EXPRESSION "Track: volume must be from 0 to 1")

# Keyboard entry at a caret (ADR-0021).
add_test(NAME cli.edit_types_frets_at_the_caret
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 2" "caret 0 0 0 6"
    "duration 8" "fret 3" right up "fret 2" right "duration 4" "fret 0")
set_tests_properties(cli.edit_types_frets_at_the_caret PROPERTIES
  PASS_REGULAR_EXPRESSION "\"beats\":3,\"notes\":3}.*\"caret\":{\"track\":0,\"staff\":0,\"measure\":0,\"voice\":0,\"beat\":2,\"string\":5,\"freeSlot\":false},\"input\":\"4\",\"undo\":{\"count\":5,\"next\":\"Enter note\"}")

add_test(NAME cli.edit_deletes_at_the_caret
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 1" "caret 0 0 0 1"
    "fret 0" "duration 2" delete delete)
set_tests_properties(cli.edit_deletes_at_the_caret PROPERTIES
  PASS_REGULAR_EXPRESSION "\"beats\":0,\"notes\":0}.*\"caret\":{\"track\":0,\"staff\":0,\"measure\":0,\"voice\":0,\"beat\":0,\"string\":1,\"freeSlot\":true},\"input\":\"2\",\"undo\":{\"count\":6,\"next\":\"Delete\"}")

# Note names (ADR-0034): each in the octave nearest the previous note, a chord
# note above the highest, then a semitone down; on tablature the caret moves to
# the string that reaches the note.
set(ELARA_CLI_NAMES "${CMAKE_CURRENT_BINARY_DIR}/cli-note-names.elara")
add_test(NAME cli.edit_types_note_names
  COMMAND elara-cli --json edit --out "${ELARA_CLI_NAMES}" "add-track flute" "append-measures 1"
    "caret 0 0 0" "pitch C" right "pitch E" "pitch G add" right "pitch A" "shift -1")
set_tests_properties(cli.edit_types_note_names PROPERTIES
  FIXTURES_SETUP cli_note_names
  PASS_REGULAR_EXPRESSION "\"caret\":{\"track\":0,\"staff\":0,\"measure\":0,\"voice\":0,\"beat\":2,\"string\":1,\"freeSlot\":false},\"input\":\"4\",\"undo\":{\"count\":7,\"next\":\"Change pitch\"}")
add_test(NAME cli.dump_note_names COMMAND elara-cli --json dump "${ELARA_CLI_NAMES}")
set_tests_properties(cli.dump_note_names PROPERTIES
  FIXTURES_REQUIRED cli_note_names
  PASS_REGULAR_EXPRESSION "\"measures\":\\[\\[\\[{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"C5\"}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"G5\"},{\"pitch\":\"E5\"}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"G#5\"}\\]}\\]\\]\\]")
# Voices: the caret's voice picks where typing goes, the first voice keeps its
# notes; voices are 1 to 4.
set(ELARA_CLI_VOICES "${CMAKE_CURRENT_BINARY_DIR}/cli-voices.elara")
add_test(NAME cli.edit_types_in_voice_two
  COMMAND elara-cli --json edit --out "${ELARA_CLI_VOICES}" "add-track flute" "append-measures 1"
    "caret 0 0 0" "pitch C" "voice 2" "pitch E")
set_tests_properties(cli.edit_types_in_voice_two PROPERTIES
  FIXTURES_SETUP cli_voices
  PASS_REGULAR_EXPRESSION "\"caret\":{\"track\":0,\"staff\":0,\"measure\":0,\"voice\":1,\"beat\":0,\"string\":1,\"freeSlot\":false}")
add_test(NAME cli.dump_voice_two COMMAND elara-cli --json dump "${ELARA_CLI_VOICES}")
set_tests_properties(cli.dump_voice_two PROPERTIES
  FIXTURES_REQUIRED cli_voices
  PASS_REGULAR_EXPRESSION "\"measures\":\\[\\[\\[{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"C5\"}\\]}\\],\\[{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"E5\"}\\]}\\]\\]\\]")
# The input duration follows the beat the caret moves to, as the app's keys
# have it (ADR-0127): back on the eighth, typing uses eighths again.
add_test(NAME cli.edit_input_follows_the_caret
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "duration 8" "fret 3"
    right "duration 2" "fret 5" left)
set_tests_properties(cli.edit_input_follows_the_caret PROPERTIES
  PASS_REGULAR_EXPRESSION "beat 0, string 1; input 8")
add_test(NAME cli.edit_rejects_voice_five
  COMMAND elara-cli edit "add-track flute" "append-measures 1" "voice 5")
set_tests_properties(cli.edit_rejects_voice_five PROPERTIES
  PASS_REGULAR_EXPRESSION "voice: 1 to 4, not 5")
add_test(NAME cli.edit_note_name_on_tablature
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 1" "caret 0 0 0 1"
    "pitch A")
set_tests_properties(cli.edit_note_name_on_tablature PROPERTIES
  PASS_REGULAR_EXPRESSION "\"caret\":{\"track\":0,\"staff\":0,\"measure\":0,\"voice\":0,\"beat\":0,\"string\":3,\"freeSlot\":false}")
add_test(NAME cli.edit_rejects_note_name_on_drums
  COMMAND elara-cli edit "add-track drum-kit" "append-measures 1" "pitch C")
add_test(NAME cli.edit_rejects_unknown_note_name
  COMMAND elara-cli edit "add-track flute" "append-measures 1" "pitch H")
set_tests_properties(cli.edit_rejects_note_name_on_drums PROPERTIES
  PASS_REGULAR_EXPRESSION "enterPitch: a percussion staff has no note names")
set_tests_properties(cli.edit_rejects_unknown_note_name PROPERTIES
  PASS_REGULAR_EXPRESSION "pitch: not a note name \\(C to B\\): H")

# Drum entry (ADR-0035): a bass drum, a closed hi-hat and a snare on the first
# beat, the hi-hat toggled off from its lane, then a snare on the second beat.
set(ELARA_CLI_DRUMS "${CMAKE_CURRENT_BINARY_DIR}/cli-drums.elara")
add_test(NAME cli.edit_types_drums
  COMMAND elara-cli --json edit --out "${ELARA_CLI_DRUMS}" "add-track drum-kit" "append-measures 1"
    "caret 0 0 0" "drum 36" "drum 42" "drum 38" "caret 0 0 0 2" drum right "drum 38")
set_tests_properties(cli.edit_types_drums PROPERTIES
  FIXTURES_SETUP cli_drums
  PASS_REGULAR_EXPRESSION "\"caret\":{\"track\":0,\"staff\":0,\"measure\":0,\"voice\":0,\"beat\":1,\"string\":8,\"freeSlot\":false}")
add_test(NAME cli.dump_drums COMMAND elara-cli --json dump "${ELARA_CLI_DRUMS}")
set_tests_properties(cli.dump_drums PROPERTIES
  FIXTURES_REQUIRED cli_drums
  PASS_REGULAR_EXPRESSION "\"measures\":\\[\\[\\[{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"D2\"},{\"pitch\":\"C2\"}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"D2\"}\\]}\\]\\]\\]")
add_test(NAME cli.edit_rejects_drum_on_guitar
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" drum)
add_test(NAME cli.edit_rejects_drum_without_lane
  COMMAND elara-cli edit "add-track drum-kit" "append-measures 1" "drum 56")
set_tests_properties(cli.edit_rejects_drum_on_guitar PROPERTIES
  PASS_REGULAR_EXPRESSION "toggleDrum: the staff is not a percussion staff")
set_tests_properties(cli.edit_rejects_drum_without_lane PROPERTIES
  PASS_REGULAR_EXPRESSION "drum: no lane for key 56")

# Note effects (ADR-0037): palm mute and let ring on fret 5, let ring off again;
# a tie on fret 7 after it takes fret 5; a natural harmonic on fret 12.
set(ELARA_CLI_EFFECTS "${CMAKE_CURRENT_BINARY_DIR}/cli-effects.elara")
add_test(NAME cli.edit_toggles_effects
  COMMAND elara-cli --json edit --out "${ELARA_CLI_EFFECTS}" "add-track electric-guitar" "append-measures 1"
    "caret 0 0 0 3" "fret 5" "effect palm-mute" "effect let-ring" "effect let-ring"
    right "fret 7" "effect tie" right "fret 12" "effect harmonic")
set_tests_properties(cli.edit_toggles_effects PROPERTIES
  FIXTURES_SETUP cli_effects
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":10,\"next\":\"Change effect\"}")
add_test(NAME cli.dump_effects COMMAND elara-cli --json dump "${ELARA_CLI_EFFECTS}")
set_tests_properties(cli.dump_effects PROPERTIES
  FIXTURES_REQUIRED cli_effects
  PASS_REGULAR_EXPRESSION "\"notes\":\\[{\"pitch\":\"C4\",\"string\":3,\"fret\":5,\"effects\":{\"palmMute\":true}}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"C4\",\"string\":3,\"fret\":5,\"effects\":{\"tiedFromPrevious\":true}}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"G4\",\"string\":3,\"fret\":12,\"effects\":{\"harmonic\":{\"kind\":\"natural\"")
add_test(NAME cli.edit_rejects_unknown_effect
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "fret 5" "effect wah")
add_test(NAME cli.edit_rejects_tie_without_note
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "fret 5" "effect tie")
set_tests_properties(cli.edit_rejects_unknown_effect PROPERTIES
  PASS_REGULAR_EXPRESSION "not an effect: wah")
set_tests_properties(cli.edit_rejects_tie_without_note PROPERTIES
  PASS_REGULAR_EXPRESSION "toggleEffect: no note to tie to")

# Bends of any form (ADR-0055): a half-step bend and release on fret 7, then a
# pre-bend of a step and a half on fret 9, taken off and put back by undo.
set(ELARA_CLI_BENDS "${CMAKE_CURRENT_BINARY_DIR}/cli-bends.elara")
add_test(NAME cli.edit_sets_bend
  COMMAND elara-cli --json edit --out "${ELARA_CLI_BENDS}" "add-track electric-guitar" "append-measures 1"
    "caret 0 0 0 3" "fret 7" "bend release 100" right "fret 9" "bend pre-bend 300" "bend none" undo)
set_tests_properties(cli.edit_sets_bend PROPERTIES
  FIXTURES_SETUP cli_bends
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":6,\"next\":\"Change bend\"}")
add_test(NAME cli.dump_bends COMMAND elara-cli --json dump "${ELARA_CLI_BENDS}")
set_tests_properties(cli.dump_bends PROPERTIES
  FIXTURES_REQUIRED cli_bends
  PASS_REGULAR_EXPRESSION "\"fret\":7,\"effects\":{\"bend\":\\[\\[0,0\\],\\[33,100\\],\\[66,100\\],\\[100,0\\]\\]}}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"[A-G][#b]?[0-9]\",\"string\":3,\"fret\":9,\"effects\":{\"bend\":\\[\\[0,300\\],\\[100,300\\]\\]}}")
add_test(NAME cli.edit_rejects_unknown_bend
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "fret 5" "bend wobble")
add_test(NAME cli.edit_rejects_flat_bend
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "fret 5" "bend bend 0")
set_tests_properties(cli.edit_rejects_unknown_bend PROPERTIES
  PASS_REGULAR_EXPRESSION "not a bend form: wobble")
set_tests_properties(cli.edit_rejects_flat_bend PROPERTIES
  PASS_REGULAR_EXPRESSION "bendCurve: the bend must go up by 1 to 2400 cents")

# Beat effects (ADR-0056): a chord strummed down, then fret 9 tremolo-picked
# at sixteenths with an up stroke; the strum taken off and put back by undo.
set(ELARA_CLI_BEAT_EFFECTS "${CMAKE_CURRENT_BINARY_DIR}/cli-beat-effects.elara")
add_test(NAME cli.edit_sets_beat_effects
  COMMAND elara-cli --json edit --out "${ELARA_CLI_BEAT_EFFECTS}" "add-track electric-guitar"
    "append-measures 1" "caret 0 0 0 2" "fret 5" "caret 0 0 0 3" "fret 7" "strum down"
    right "fret 9" "tremolo 16" "pick up" "caret 0 0 0 2" "strum none" undo)
set_tests_properties(cli.edit_sets_beat_effects PROPERTIES
  FIXTURES_SETUP cli_beat_effects
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":8,\"next\":\"Change beat effects\"}")
add_test(NAME cli.dump_beat_effects COMMAND elara-cli --json dump "${ELARA_CLI_BEAT_EFFECTS}")
set_tests_properties(cli.dump_beat_effects PROPERTIES
  FIXTURES_REQUIRED cli_beat_effects
  PASS_REGULAR_EXPRESSION "\"effects\":{\"strum\":\"down\"}},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"[A-G][#b]?[0-9]\",\"string\":3,\"fret\":9}\\],\"effects\":{\"pickStroke\":\"up\",\"tremoloPicking\":\"16\"}}")
add_test(NAME cli.edit_rejects_unknown_strum
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "fret 5" "strum sideways")
add_test(NAME cli.edit_rejects_unknown_tremolo
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "fret 5" "tremolo 12")
set_tests_properties(cli.edit_rejects_unknown_strum PROPERTIES
  PASS_REGULAR_EXPRESSION "strum: down, up or none, not sideways")
set_tests_properties(cli.edit_rejects_unknown_tremolo PROPERTIES
  PASS_REGULAR_EXPRESSION "tremolo: 8, 16, 32 or none, not 12")

# Whammy bar and bass techniques (ADR-0060): fret 5 dips 300 cents and is
# slapped; fret 7 pulls up 100 cents and returns, and is popped, and taking the
# pop off is undone.
set(ELARA_CLI_WHAMMY "${CMAKE_CURRENT_BINARY_DIR}/cli-whammy.elara")
add_test(NAME cli.edit_sets_whammy_and_bass
  COMMAND elara-cli --json edit --out "${ELARA_CLI_WHAMMY}" "add-track electric-bass"
    "append-measures 1" "fret 5" "whammy dip -300" "bass slap" right "fret 7" "whammy dive"
    "bass pop" "whammy return 100" "bass none" undo)
set_tests_properties(cli.edit_sets_whammy_and_bass PROPERTIES
  FIXTURES_SETUP cli_whammy
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":9,\"next\":\"Change beat effects\"}")
add_test(NAME cli.dump_whammy_and_bass COMMAND elara-cli --json dump "${ELARA_CLI_WHAMMY}")
set_tests_properties(cli.dump_whammy_and_bass PROPERTIES
  FIXTURES_REQUIRED cli_whammy
  PASS_REGULAR_EXPRESSION "\"fret\":5}\\],\"effects\":{\"whammyBar\":\\[\\[0,0\\],\\[33,-300\\],\\[66,-300\\],\\[100,0\\]\\],\"bassTechnique\":\"slap\"}}.*\"fret\":7}\\],\"effects\":{\"whammyBar\":\\[\\[0,100\\],\\[50,100\\],\\[100,0\\]\\],\"bassTechnique\":\"pop\"}}")
add_test(NAME cli.edit_rejects_unknown_whammy
  COMMAND elara-cli edit "add-track electric-bass" "append-measures 1" "fret 5" "whammy wobble")
add_test(NAME cli.edit_rejects_whammy_of_nothing
  COMMAND elara-cli edit "add-track electric-bass" "append-measures 1" "fret 5" "whammy dive 0")
add_test(NAME cli.edit_rejects_unknown_bass_technique
  COMMAND elara-cli edit "add-track electric-bass" "append-measures 1" "fret 5" "bass thumb")
set_tests_properties(cli.edit_rejects_unknown_whammy PROPERTIES
  PASS_REGULAR_EXPRESSION "not a whammy-bar form: wobble")
set_tests_properties(cli.edit_rejects_whammy_of_nothing PROPERTIES
  PASS_REGULAR_EXPRESSION "whammyCurve: the bar must go 1 to 2400 cents down or up")
set_tests_properties(cli.edit_rejects_unknown_bass_technique PROPERTIES
  PASS_REGULAR_EXPRESSION "bass: none, tap, slap, pop, not thumb")

# Hairpins at the caret (ADR-0078): the first two beats get a crescendo, the
# third a diminuendo that undo takes back.
set(ELARA_CLI_HAIRPINS "${CMAKE_CURRENT_BINARY_DIR}/cli-hairpins.elara")
add_test(NAME cli.edit_sets_hairpins
  COMMAND elara-cli --json edit --out "${ELARA_CLI_HAIRPINS}" "add-track piano"
    "append-measures 1" "pitch C" "hairpin crescendo" right "pitch D" "hairpin crescendo"
    right "pitch E" "hairpin diminuendo" "hairpin none" undo)
set_tests_properties(cli.edit_sets_hairpins PROPERTIES
  FIXTURES_SETUP cli_hairpins
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change beat effects\"}")
add_test(NAME cli.dump_hairpins COMMAND elara-cli --json dump "${ELARA_CLI_HAIRPINS}")
set_tests_properties(cli.dump_hairpins PROPERTIES
  FIXTURES_REQUIRED cli_hairpins
  PASS_REGULAR_EXPRESSION "\"C5\"}\],\"effects\":{\"hairpin\":\"crescendo\"}}.*\"D5\"}\],\"effects\":{\"hairpin\":\"crescendo\"}}.*\"E5\"}\],\"effects\":{\"hairpin\":\"diminuendo\"}}")
# Chord symbols typed at the caret (ADR-0080): two frets make the diagram of
# Em; G7 over a single note has none, and goes again.
set(ELARA_CLI_CHORDS "${CMAKE_CURRENT_BINARY_DIR}/cli-chords.elara")
add_test(NAME cli.edit_names_chords
  COMMAND elara-cli --json edit --out "${ELARA_CLI_CHORDS}" "add-track electric-guitar"
    "append-measures 1" "caret 0 0 0 5" "fret 2" "caret 0 0 0 4" "fret 2" "chord Em" right
    "fret 3" "chord G7" "chord none")
set_tests_properties(cli.edit_names_chords PROPERTIES
  FIXTURES_SETUP cli_chords
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Remove chord symbol\"}")
add_test(NAME cli.dump_chords COMMAND elara-cli --json dump "${ELARA_CLI_CHORDS}")
set_tests_properties(cli.dump_chords PROPERTIES
  FIXTURES_REQUIRED cli_chords
  PASS_REGULAR_EXPRESSION "\"chord\":{\"name\":\"Em\",\"diagram\":{\"firstFret\":1,\"frets\":\\[-1,-1,-1,2,2,-1\\]}}"
  FAIL_REGULAR_EXPRESSION "G7")
# Transposing moves chord symbols with the notes (ADR-0080): Em/B up a whole
# tone in C major is F#m/C#, its diagram two frets higher.
set(ELARA_CLI_CHORDS_UP "${CMAKE_CURRENT_BINARY_DIR}/cli-chords-up.elara")
add_test(NAME cli.edit_transposes_chords
  COMMAND elara-cli --json edit --out "${ELARA_CLI_CHORDS_UP}" "add-track electric-guitar"
    "append-measures 1" "caret 0 0 0 5" "fret 2" "caret 0 0 0 4" "fret 2" "chord Em/B"
    "transpose 2")
set_tests_properties(cli.edit_transposes_chords PROPERTIES
  FIXTURES_SETUP cli_chords_up
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Transpose\"}")
add_test(NAME cli.dump_transposed_chords COMMAND elara-cli --json dump "${ELARA_CLI_CHORDS_UP}")
set_tests_properties(cli.dump_transposed_chords PROPERTIES
  FIXTURES_REQUIRED cli_chords_up
  PASS_REGULAR_EXPRESSION "\"chord\":{\"name\":\"F#m/C#\",\"diagram\":{\"firstFret\":1,\"frets\":\\[-1,-1,-1,4,4,-1\\]}}")
# Beat text typed at the caret (ADR-0081): words with spaces over a note; a
# second text over the next beat goes again.
set(ELARA_CLI_TEXT "${CMAKE_CURRENT_BINARY_DIR}/cli-text.elara")
add_test(NAME cli.edit_writes_text
  COMMAND elara-cli --json edit --out "${ELARA_CLI_TEXT}" "add-track electric-guitar"
    "append-measures 1" "caret 0 0 0 5" "fret 2" "text let it ring" right "fret 3"
    "text solo" "text none")
set_tests_properties(cli.edit_writes_text PROPERTIES
  FIXTURES_SETUP cli_text
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Remove beat text\"}")
add_test(NAME cli.dump_text COMMAND elara-cli --json dump "${ELARA_CLI_TEXT}")
set_tests_properties(cli.dump_text PROPERTIES
  FIXTURES_REQUIRED cli_text
  PASS_REGULAR_EXPRESSION "\"text\":\"let it ring\""
  FAIL_REGULAR_EXPRESSION "solo")
# Lyrics typed as text (ADR-0084): a word over two notes with a blank note
# before it and the last syllable held, a second verse one syllable too long;
# then both verses taken away.
set(ELARA_CLI_LYRICS "${CMAKE_CURRENT_BINARY_DIR}/cli-lyrics.elara")
add_test(NAME cli.edit_types_lyrics
  COMMAND elara-cli --json edit --out "${ELARA_CLI_LYRICS}" "add-track electric-guitar"
    "append-measures 1" "caret 0 0 0 5" "fret 2" right "fret 3" right "fret 5" right "fret 7"
    "lyrics 0 0 Sing  a-long_" "lyrics 1 0 oh oh oh oh oh")
set_tests_properties(cli.edit_types_lyrics PROPERTIES
  FIXTURES_SETUP cli_lyrics
  PASS_REGULAR_EXPRESSION "lyrics: 1 syllable.s. after the last note dropped")
add_test(NAME cli.dump_lyrics COMMAND elara-cli --json dump "${ELARA_CLI_LYRICS}")
set_tests_properties(cli.dump_lyrics PROPERTIES
  FIXTURES_REQUIRED cli_lyrics
  PASS_REGULAR_EXPRESSION "\"lyrics\":\\[\"Sing\",\"oh\"\\].*\"lyrics\":\\[\"\",\"oh\"\\].*\"lyrics\":\\[\"a-\",\"oh\"\\].*\"lyrics\":\\[\"long_\",\"oh\"\\]")
add_test(NAME cli.edit_removes_lyrics
  COMMAND elara-cli --json edit --in "${ELARA_CLI_LYRICS}" "lyrics 0 none" "lyrics 1 none")
set_tests_properties(cli.edit_removes_lyrics PROPERTIES
  FIXTURES_REQUIRED cli_lyrics
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":2,\"next\":\"Remove lyrics\"}")
# Grace notes typed at the caret (ADR-0079): two on the 7th fret of the G
# string, then one on the next note taken off again.
set(ELARA_CLI_GRACES "${CMAKE_CURRENT_BINARY_DIR}/cli-graces.elara")
add_test(NAME cli.edit_adds_grace_notes
  COMMAND elara-cli --json edit --out "${ELARA_CLI_GRACES}" "add-track electric-guitar"
    "append-measures 1" "caret 0 0 0 3" "fret 7" "grace -2" "grace -1 16 on" right "fret 5"
    "grace 2" "grace none")
set_tests_properties(cli.edit_adds_grace_notes PROPERTIES
  FIXTURES_SETUP cli_graces
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Remove grace notes\"}")
add_test(NAME cli.dump_grace_notes COMMAND elara-cli --json dump "${ELARA_CLI_GRACES}")
set_tests_properties(cli.dump_grace_notes PROPERTIES
  FIXTURES_REQUIRED cli_graces
  PASS_REGULAR_EXPRESSION "\"graces\":\\[{\"duration\":\"8\",\"notes\":\\[{[^}]*\"string\":3,\"fret\":5}\\]},{\"duration\":\"16\",\"notes\":\\[{[^}]*\"string\":3,\"fret\":6}\\],\"onBeat\":true}\\]")
add_test(NAME cli.edit_rejects_grace_off_the_neck
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "caret 0 0 0 3"
    "fret 1" "grace -2")
set_tests_properties(cli.edit_rejects_grace_off_the_neck PROPERTIES
  PASS_REGULAR_EXPRESSION "addGraceNote: the grace note leaves the neck")
add_test(NAME cli.edit_rejects_unknown_hairpin
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "hairpin swell")
set_tests_properties(cli.edit_rejects_unknown_hairpin PROPERTIES
  PASS_REGULAR_EXPRESSION "hairpin: none, crescendo, diminuendo, not swell")

# Fermatas at the caret (ADR-0085): a long one on the C, a short one on a rest,
# and a normal one on the E that undo takes back. The measure lasts 1.25 s
# longer: the C three times as long, the rest half as long again.
set(ELARA_CLI_FERMATAS "${CMAKE_CURRENT_BINARY_DIR}/cli-fermatas.elara")
add_test(NAME cli.edit_sets_fermatas
  COMMAND elara-cli --json edit --out "${ELARA_CLI_FERMATAS}" "add-track piano"
    "append-measures 1" "pitch C" "fermata long" right insert-rest "fermata short" right
    "pitch E" "fermata normal" undo)
set_tests_properties(cli.edit_sets_fermatas PROPERTIES
  FIXTURES_SETUP cli_fermatas
  PASS_REGULAR_EXPRESSION "\"redo\":{\"count\":1,\"next\":\"Change beat effects\"}")
add_test(NAME cli.dump_fermatas COMMAND elara-cli --json dump "${ELARA_CLI_FERMATAS}")
set_tests_properties(cli.dump_fermatas PROPERTIES
  FIXTURES_REQUIRED cli_fermatas
  PASS_REGULAR_EXPRESSION "\"C5\"}\],\"effects\":{\"fermata\":\"long\"}}.*{\"duration\":\"4\",\"effects\":{\"fermata\":\"short\"}}.*\"E5\"}\]}.*\"durationSeconds\":3\\.250")
# Octave lines at the caret (ADR-0089): 8va on the C, 15mb on the D, 8vb on a
# rest, and 15ma on the E that undo takes back. Each is drawn as a mark, its
# dashes and a hook.
set(ELARA_CLI_OTTAVAS "${CMAKE_CURRENT_BINARY_DIR}/cli-ottavas.elara")
add_test(NAME cli.edit_sets_ottavas
  COMMAND elara-cli --json edit --out "${ELARA_CLI_OTTAVAS}" "add-track piano"
    "append-measures 1" "pitch C" "ottava 8va" right "pitch D" "ottava 15mb" right insert-rest
    "ottava 8vb" right "pitch E" "ottava 15ma" undo)
set_tests_properties(cli.edit_sets_ottavas PROPERTIES
  FIXTURES_SETUP cli_ottavas
  PASS_REGULAR_EXPRESSION "\"redo\":{\"count\":1,\"next\":\"Change beat effects\"}")
add_test(NAME cli.dump_ottavas COMMAND elara-cli --json dump "${ELARA_CLI_OTTAVAS}")
set_tests_properties(cli.dump_ottavas PROPERTIES
  FIXTURES_REQUIRED cli_ottavas
  PASS_REGULAR_EXPRESSION "\"C5\"}\],\"effects\":{\"ottava\":\"8va\"}}.*\"D5\"}\],\"effects\":{\"ottava\":\"15mb\"}}.*{\"duration\":\"4\",\"effects\":{\"ottava\":\"8vb\"}}.*\"E5\"}\]}")
add_test(NAME cli.render_ottavas COMMAND elara-cli --json render "${ELARA_CLI_OTTAVAS}")
set_tests_properties(cli.render_ottavas PROPERTIES
  FIXTURES_REQUIRED cli_ottavas
  PASS_REGULAR_EXPRESSION "\"octaveLine\":9[,}]")
add_test(NAME cli.edit_rejects_unknown_ottava
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "ottava 22ma")
set_tests_properties(cli.edit_rejects_unknown_ottava PROPERTIES
  PASS_REGULAR_EXPRESSION "ottava: none, 8va, 8vb, 15ma, 15mb, not 22ma")
# Fingerings at the caret (ADR-0093): a piano's C-E-G fingered 1 3 5, drawn
# over the notes; a guitar's note fretted with the ring finger and plucked
# with i, the fingering small left of the notehead and i over it.
set(ELARA_CLI_FINGERINGS "${CMAKE_CURRENT_BINARY_DIR}/cli-fingerings.elara")
add_test(NAME cli.edit_sets_fingerings
  COMMAND elara-cli --json edit --out "${ELARA_CLI_FINGERINGS}" "add-track piano"
    "append-measures 1" "pitch C" "pitch E add" "pitch G add" "fingering 1 3 5")
set_tests_properties(cli.edit_sets_fingerings PROPERTIES
  FIXTURES_SETUP cli_fingerings
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change fingering\"}")
add_test(NAME cli.dump_fingerings COMMAND elara-cli --json dump "${ELARA_CLI_FINGERINGS}")
set_tests_properties(cli.dump_fingerings PROPERTIES
  FIXTURES_REQUIRED cli_fingerings
  PASS_REGULAR_EXPRESSION "\"fingering\":\"little\".*\"fingering\":\"middle\".*\"fingering\":\"thumb\"")
add_test(NAME cli.render_fingerings COMMAND elara-cli --json render "${ELARA_CLI_FINGERINGS}")
set_tests_properties(cli.render_fingerings PROPERTIES
  FIXTURES_REQUIRED cli_fingerings
  PASS_REGULAR_EXPRESSION "\"fingering\":3[,}]")
set(ELARA_CLI_PLUCKS "${CMAKE_CURRENT_BINARY_DIR}/cli-plucks.elara")
add_test(NAME cli.edit_sets_plucks
  COMMAND elara-cli --json edit --out "${ELARA_CLI_PLUCKS}" "add-track electric-guitar"
    "append-measures 1" "caret 0 0 0 5" "fret 3" "fingering 3" "pluck i")
set_tests_properties(cli.edit_sets_plucks PROPERTIES
  FIXTURES_SETUP cli_plucks
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change plucking finger\"}")
add_test(NAME cli.render_plucks COMMAND elara-cli --json render "${ELARA_CLI_PLUCKS}")
set_tests_properties(cli.render_plucks PROPERTIES
  FIXTURES_REQUIRED cli_plucks
  PASS_REGULAR_EXPRESSION "\"fingering\":2[,}]")
add_test(NAME cli.edit_rejects_a_sixth_finger
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "fingering 6")
set_tests_properties(cli.edit_rejects_a_sixth_finger PROPERTIES
  PASS_REGULAR_EXPRESSION "not a finger: 6")
add_test(NAME cli.edit_rejects_pluck_without_strings
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "pluck i")
set_tests_properties(cli.edit_rejects_pluck_without_strings PROPERTIES
  PASS_REGULAR_EXPRESSION "setPluck: a plucking finger needs strings")
# The sustain pedal at the caret (ADR-0092): down on the C, the D and a rest,
# changed on the E. Drawn as "Ped.", its line broken by the notch, and the hook.
set(ELARA_CLI_PEDALS "${CMAKE_CURRENT_BINARY_DIR}/cli-pedals.elara")
add_test(NAME cli.edit_sets_pedals
  COMMAND elara-cli --json edit --out "${ELARA_CLI_PEDALS}" "add-track piano"
    "append-measures 1" "pitch C" "pedal down" right "pitch D" "pedal down" right insert-rest
    "pedal down" right "pitch E" "pedal change")
set_tests_properties(cli.edit_sets_pedals PROPERTIES
  FIXTURES_SETUP cli_pedals
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change beat effects\"}")
add_test(NAME cli.dump_pedals COMMAND elara-cli --json dump "${ELARA_CLI_PEDALS}")
set_tests_properties(cli.dump_pedals PROPERTIES
  FIXTURES_REQUIRED cli_pedals
  PASS_REGULAR_EXPRESSION "\"C5\"}\],\"effects\":{\"pedal\":\"down\"}}.*\"D5\"}\],\"effects\":{\"pedal\":\"down\"}}.*{\"duration\":\"4\",\"effects\":{\"pedal\":\"down\"}}.*\"E5\"}\],\"effects\":{\"pedal\":\"change\"}}")
add_test(NAME cli.render_pedals COMMAND elara-cli --json render "${ELARA_CLI_PEDALS}")
set_tests_properties(cli.render_pedals PROPERTIES
  FIXTURES_REQUIRED cli_pedals
  PASS_REGULAR_EXPRESSION "\"pedal\":6[,}]")
add_test(NAME cli.edit_rejects_unknown_pedal
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "pedal half")
set_tests_properties(cli.edit_rejects_unknown_pedal PROPERTIES
  PASS_REGULAR_EXPRESSION "pedal: none, down, change, not half")
# Slurs at the caret (ADR-0086): the C slurred to the D, the E's slur undone.
set(ELARA_CLI_SLURS "${CMAKE_CURRENT_BINARY_DIR}/cli-slurs.elara")
add_test(NAME cli.edit_sets_slurs
  COMMAND elara-cli --json edit --out "${ELARA_CLI_SLURS}" "add-track piano"
    "append-measures 1" "pitch C" "slur on" right "pitch D" right "pitch E" "slur on" undo)
set_tests_properties(cli.edit_sets_slurs PROPERTIES
  FIXTURES_SETUP cli_slurs
  PASS_REGULAR_EXPRESSION "\"redo\":{\"count\":1,\"next\":\"Change beat effects\"}")
add_test(NAME cli.dump_slurs COMMAND elara-cli --json dump "${ELARA_CLI_SLURS}")
set_tests_properties(cli.dump_slurs PROPERTIES
  FIXTURES_REQUIRED cli_slurs
  PASS_REGULAR_EXPRESSION "\"C5\"}\\],\"effects\":{\"slur\":true}},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"D5\"}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"E5\"}\\]}")
add_test(NAME cli.edit_rejects_unknown_slur
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "slur maybe")
set_tests_properties(cli.edit_rejects_unknown_slur PROPERTIES
  PASS_REGULAR_EXPRESSION "slur: none, on, not maybe")
# Tenuto at the caret (ADR-0088): on the C, and with a staccato (portato) on
# the D, which plays three quarters of its length.
set(ELARA_CLI_TENUTO "${CMAKE_CURRENT_BINARY_DIR}/cli-tenuto.elara")
add_test(NAME cli.edit_toggles_tenuto
  COMMAND elara-cli --json edit --out "${ELARA_CLI_TENUTO}" "add-track piano"
    "append-measures 1" "pitch C" "effect tenuto" right "pitch D" "effect staccato"
    "effect tenuto")
set_tests_properties(cli.edit_toggles_tenuto PROPERTIES
  FIXTURES_SETUP cli_tenuto
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change effect\"}")
add_test(NAME cli.dump_tenuto COMMAND elara-cli --json dump "${ELARA_CLI_TENUTO}")
set_tests_properties(cli.dump_tenuto PROPERTIES
  FIXTURES_REQUIRED cli_tenuto
  PASS_REGULAR_EXPRESSION "\"C5\",\"effects\":{\"tenuto\":true}}.*\"D5\",\"effects\":{\"staccato\":true,\"tenuto\":true}}")
# Breaks after the caret's beat (ADR-0095): a breath mark after the C, a
# caesura after the D, both drawn as articulations.
set(ELARA_CLI_BREATHS "${CMAKE_CURRENT_BINARY_DIR}/cli-breaths.elara")
add_test(NAME cli.edit_sets_breaths
  COMMAND elara-cli --json edit --out "${ELARA_CLI_BREATHS}" "add-track flute"
    "append-measures 1" "pitch C" "breath comma" right "pitch D" "breath caesura")
set_tests_properties(cli.edit_sets_breaths PROPERTIES
  FIXTURES_SETUP cli_breaths
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change beat effects\"}")
add_test(NAME cli.dump_breaths COMMAND elara-cli --json dump "${ELARA_CLI_BREATHS}")
set_tests_properties(cli.dump_breaths PROPERTIES
  FIXTURES_REQUIRED cli_breaths
  PASS_REGULAR_EXPRESSION "\"breath\":\"comma\".*\"breath\":\"caesura\"")
add_test(NAME cli.render_breaths COMMAND elara-cli --json render "${ELARA_CLI_BREATHS}")
set_tests_properties(cli.render_breaths PROPERTIES
  FIXTURES_REQUIRED cli_breaths
  PASS_REGULAR_EXPRESSION "\"articulation\":2[,}]")
add_test(NAME cli.edit_rejects_unknown_breath
  COMMAND elara-cli edit "add-track flute" "append-measures 1" "pitch C" "breath gasp")
set_tests_properties(cli.edit_rejects_unknown_breath PROPERTIES
  PASS_REGULAR_EXPRESSION "breath: none, comma, caesura, not gasp")
# Staccatissimo at the caret (ADR-0094): on the C, and in the place of the
# staccato on the D.
set(ELARA_CLI_STACCATISSIMO "${CMAKE_CURRENT_BINARY_DIR}/cli-staccatissimo.elara")
add_test(NAME cli.edit_toggles_staccatissimo
  COMMAND elara-cli --json edit --out "${ELARA_CLI_STACCATISSIMO}" "add-track piano"
    "append-measures 1" "pitch C" "effect staccatissimo" right "pitch D" "effect staccato"
    "effect staccatissimo")
set_tests_properties(cli.edit_toggles_staccatissimo PROPERTIES
  FIXTURES_SETUP cli_staccatissimo
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change effect\"}")
add_test(NAME cli.dump_staccatissimo COMMAND elara-cli --json dump "${ELARA_CLI_STACCATISSIMO}")
set_tests_properties(cli.dump_staccatissimo PROPERTIES
  FIXTURES_REQUIRED cli_staccatissimo
  PASS_REGULAR_EXPRESSION "\"C5\",\"effects\":{\"staccatissimo\":true}}.*\"D5\",\"effects\":{\"staccatissimo\":true}}")
# Clef changes at the caret (ADR-0087): a flute down to the bass clef, its
# first note name then near the bass staff's middle line; a guitar keeps its
# octave; the tenor clef undone.
set(ELARA_CLI_CLEFS "${CMAKE_CURRENT_BINARY_DIR}/cli-clefs.elara")
add_test(NAME cli.edit_changes_clefs
  COMMAND elara-cli --json edit --out "${ELARA_CLI_CLEFS}" "add-track flute"
    "add-track electric-guitar" "append-measures 3" "caret 0 1 0" "clef bass" "pitch C"
    "caret 1 1 0" "clef bass" "caret 1 2 0" "clef tenor" undo)
set_tests_properties(cli.edit_changes_clefs PROPERTIES
  FIXTURES_SETUP cli_clefs
  PASS_REGULAR_EXPRESSION "\"redo\":{\"count\":1,\"next\":\"Change clef\"}")
add_test(NAME cli.dump_clefs COMMAND elara-cli --json dump "${ELARA_CLI_CLEFS}")
set_tests_properties(cli.dump_clefs PROPERTIES
  FIXTURES_REQUIRED cli_clefs
  PASS_REGULAR_EXPRESSION "\"clef\":\"treble\",\"clefs\":\\[{\"measure\":1,\"clef\":\"bass\"}\\].*\"C3\".*\"clef\":\"treble-8vb\",.*\"clefs\":\\[{\"measure\":1,\"clef\":\"bass-8vb\"}\\]"
  FAIL_REGULAR_EXPRESSION "tenor")
# Measure repeats at the caret (ADR-0102): measure 1 repeats measure 0,
# measures 3 and 4 repeat 1 and 2; then the two-measure sign is written out
# and undone, and measure 0 refuses a sign.
set(ELARA_CLI_SIMILE_EDIT "${CMAKE_CURRENT_BINARY_DIR}/cli-simile-edit.elara")
add_test(NAME cli.edit_sets_measure_repeats
  COMMAND elara-cli --json edit --out "${ELARA_CLI_SIMILE_EDIT}" "add-track electric-guitar"
    "append-measures 5" "fret 3" "caret 0 1 0" "simile 1" "caret 0 2 0" "fret 5"
    "caret 0 3 0" "simile 2" "simile none" undo "caret 0 0 0" "simile 1")
set_tests_properties(cli.edit_sets_measure_repeats PROPERTIES
  FIXTURES_SETUP cli_simile_edit
  PASS_REGULAR_EXPRESSION "\"redo\":{\"count\":1,\"next\":\"Write out measure repeat\"}")
add_test(NAME cli.dump_measure_repeats COMMAND elara-cli --json dump "${ELARA_CLI_SIMILE_EDIT}")
set_tests_properties(cli.dump_measure_repeats PROPERTIES
  FIXTURES_REQUIRED cli_simile_edit
  PASS_REGULAR_EXPRESSION "\"repeats\":\\[{\"measure\":1,\"repeat\":\"one\"},{\"measure\":3,\"repeat\":\"two-first\"},{\"measure\":4,\"repeat\":\"two-second\"}\\]")
add_test(NAME cli.sequence_measure_repeats COMMAND elara-cli sequence "${ELARA_CLI_SIMILE_EDIT}")
set_tests_properties(cli.sequence_measure_repeats PROPERTIES
  FIXTURES_REQUIRED cli_simile_edit
  PASS_REGULAR_EXPRESSION "5 measures, 5 notes")
add_test(NAME cli.edit_rejects_long_measure_repeats
  COMMAND elara-cli edit "add-track piano" "append-measures 4" "caret 0 3 0" "simile 3")
set_tests_properties(cli.edit_rejects_long_measure_repeats PROPERTIES
  PASS_REGULAR_EXPRESSION "a measure repeat repeats one or two measures")
add_test(NAME cli.edit_rejects_percussion_clef
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "clef percussion")
set_tests_properties(cli.edit_rejects_percussion_clef PROPERTIES
  PASS_REGULAR_EXPRESSION "clef: treble, treble-8vb, bass, bass-8vb, alto, tenor, not percussion")
add_test(NAME cli.edit_rejects_unknown_fermata
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "fermata endless")
set_tests_properties(cli.edit_rejects_unknown_fermata PROPERTIES
  PASS_REGULAR_EXPRESSION "fermata: none, short, normal, long, not endless")

# Kinds of note effects at the caret (ADR-0058): fret 12 gets an artificial
# harmonic, wide vibrato, a marcato and slides in from below and out downwards;
# fret 7 a natural harmonic, then a pinch harmonic that undo takes back.
set(ELARA_CLI_EFFECT_KINDS "${CMAKE_CURRENT_BINARY_DIR}/cli-effect-kinds.elara")
add_test(NAME cli.edit_sets_effect_kinds
  COMMAND elara-cli --json edit --out "${ELARA_CLI_EFFECT_KINDS}" "add-track electric-guitar"
    "append-measures 1" "fret 12" "harmonic artificial" "vibrato wide" "accent marcato"
    "slide-in from-below" "slide-out downwards" right "fret 7" "harmonic natural"
    "harmonic pinch" undo)
set_tests_properties(cli.edit_sets_effect_kinds PROPERTIES
  FIXTURES_SETUP cli_effect_kinds
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":10,\"next\":\"Change harmonic\"}")
add_test(NAME cli.dump_effect_kinds COMMAND elara-cli --json dump "${ELARA_CLI_EFFECT_KINDS}")
set_tests_properties(cli.dump_effect_kinds PROPERTIES
  FIXTURES_REQUIRED cli_effect_kinds
  PASS_REGULAR_EXPRESSION "\"string\":1,\"fret\":12,\"effects\":{\"harmonic\":{\"kind\":\"artificial\",\"fret\":12},\"slideIn\":\"from-below\",\"slideOut\":\"downwards\",\"vibrato\":\"wide\",\"accent\":\"marcato\"}}.*\"string\":1,\"fret\":7,\"effects\":{\"harmonic\":{\"kind\":\"natural\",\"fret\":7}}")
# Trills at the caret (ADR-0090): in D major the E trills with F# by the
# key, the G down two semitones in sixty-fourths, and the A's trill comes off.
set(ELARA_CLI_TRILLS "${CMAKE_CURRENT_BINARY_DIR}/cli-trills.elara")
add_test(NAME cli.edit_sets_trills
  COMMAND elara-cli --json edit --out "${ELARA_CLI_TRILLS}" "add-track piano"
    "append-measures 1" "key 0 D major" "pitch E" "trill on" right "pitch G" "trill -2 64" right
    "pitch A" "trill on 16" "trill none")
set_tests_properties(cli.edit_sets_trills PROPERTIES
  FIXTURES_SETUP cli_trills
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change trill\"}")
add_test(NAME cli.dump_trills COMMAND elara-cli --json dump "${ELARA_CLI_TRILLS}")
set_tests_properties(cli.dump_trills PROPERTIES
  FIXTURES_REQUIRED cli_trills
  PASS_REGULAR_EXPRESSION "\"E5\",\"effects\":{\"trill\":{\"semitones\":2,\"speed\":\"32\"}}}.*\"G5\",\"effects\":{\"trill\":{\"semitones\":-2,\"speed\":\"64\"}}}.*\"A5\"}\\]")
add_test(NAME cli.render_trills COMMAND elara-cli --json render "${ELARA_CLI_TRILLS}")
set_tests_properties(cli.render_trills PROPERTIES
  FIXTURES_REQUIRED cli_trills
  PASS_REGULAR_EXPRESSION "\"ornament\":[1-9]")
# Mordents and turns at the caret (ADR-0091): a mordent on the E, an upper
# mordent on the F, and the G's turn changed to an inverted turn.
set(ELARA_CLI_ORNAMENTS "${CMAKE_CURRENT_BINARY_DIR}/cli-ornaments.elara")
add_test(NAME cli.edit_sets_ornaments
  COMMAND elara-cli --json edit --out "${ELARA_CLI_ORNAMENTS}" "add-track piano"
    "append-measures 1" "pitch E" "ornament mordent" right "pitch F" "ornament upper-mordent"
    right "pitch G" "ornament turn" "ornament inverted-turn")
set_tests_properties(cli.edit_sets_ornaments PROPERTIES
  FIXTURES_SETUP cli_ornaments
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":[0-9]+,\"next\":\"Change ornament\"}")
add_test(NAME cli.dump_ornaments COMMAND elara-cli --json dump "${ELARA_CLI_ORNAMENTS}")
set_tests_properties(cli.dump_ornaments PROPERTIES
  FIXTURES_REQUIRED cli_ornaments
  PASS_REGULAR_EXPRESSION "\"E5\",\"effects\":{\"ornament\":\"mordent\"}}.*\"F5\",\"effects\":{\"ornament\":\"upper-mordent\"}}.*\"G5\",\"effects\":{\"ornament\":\"inverted-turn\"}}")
add_test(NAME cli.render_ornaments COMMAND elara-cli --json render "${ELARA_CLI_ORNAMENTS}")
set_tests_properties(cli.render_ornaments PROPERTIES
  FIXTURES_REQUIRED cli_ornaments
  PASS_REGULAR_EXPRESSION "\"ornament\":3[,}]")
add_test(NAME cli.edit_rejects_unknown_ornament
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "ornament shake")
set_tests_properties(cli.edit_rejects_unknown_ornament PROPERTIES
  PASS_REGULAR_EXPRESSION "ornament: none, mordent, upper-mordent, turn, inverted-turn, not shake")
add_test(NAME cli.edit_rejects_trill_past_an_octave
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "trill 13")
add_test(NAME cli.edit_rejects_unknown_trill_speed
  COMMAND elara-cli edit "add-track piano" "append-measures 1" "pitch C" "trill on 128")
set_tests_properties(cli.edit_rejects_trill_past_an_octave PROPERTIES
  PASS_REGULAR_EXPRESSION "trill: on, none or 1 to 12 semitones up or down, not 13")
set_tests_properties(cli.edit_rejects_unknown_trill_speed PROPERTIES
  PASS_REGULAR_EXPRESSION "trill: 16, 32 or 64, not 128")
add_test(NAME cli.edit_rejects_unknown_vibrato
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "fret 5" "vibrato huge")
add_test(NAME cli.edit_rejects_natural_harmonic_off_node
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "fret 6"
    "harmonic natural")
set_tests_properties(cli.edit_rejects_unknown_vibrato PROPERTIES
  PASS_REGULAR_EXPRESSION "vibrato: none, slight, wide, not huge")
set_tests_properties(cli.edit_rejects_natural_harmonic_off_node PROPERTIES
  PASS_REGULAR_EXPRESSION "setHarmonic: no natural harmonic at that fret")

# Dynamics at the caret (ADR-0047): f from the second note holds to the end,
# and a note typed after it is f too.
set(ELARA_CLI_DYNAMICS "${CMAKE_CURRENT_BINARY_DIR}/cli-dynamics.elara")
add_test(NAME cli.edit_sets_dynamic
  COMMAND elara-cli --json edit --out "${ELARA_CLI_DYNAMICS}" "add-track flute" "append-measures 1"
    "pitch C" right "pitch D" right "pitch E" "caret 0 0 1" "dynamic f" "caret 0 0 3" "pitch F")
set_tests_properties(cli.edit_sets_dynamic PROPERTIES
  FIXTURES_SETUP cli_dynamics
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":7,\"next\":\"Enter note\"}")
add_test(NAME cli.dump_dynamics COMMAND elara-cli --json dump "${ELARA_CLI_DYNAMICS}")
set_tests_properties(cli.dump_dynamics PROPERTIES
  FIXTURES_REQUIRED cli_dynamics
  PASS_REGULAR_EXPRESSION "{\"pitch\":\"C5\"}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"D5\",\"effects\":{\"dynamic\":\"f\"}}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"E5\",\"effects\":{\"dynamic\":\"f\"}}\\]},{\"duration\":\"4\",\"notes\":\\[{\"pitch\":\"F5\",\"effects\":{\"dynamic\":\"f\"}}\\]}")
add_test(NAME cli.edit_rejects_unknown_dynamic
  COMMAND elara-cli edit "add-track flute" "append-measures 1" "pitch C" "dynamic loud")
set_tests_properties(cli.edit_rejects_unknown_dynamic PROPERTIES
  PASS_REGULAR_EXPRESSION "not a dynamic: loud")

add_test(NAME cli.edit_rejects_caret_outside_song
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "caret 0 3 0")
set_tests_properties(cli.edit_rejects_caret_outside_song PROPERTIES
  PASS_REGULAR_EXPRESSION "the song has no such caret position")

add_test(NAME cli.edit_rejects_fret_without_track COMMAND elara-cli edit "fret 3")
set_tests_properties(cli.edit_rejects_fret_without_track PROPERTIES
  PASS_REGULAR_EXPRESSION "clampCaret: a song without tracks or measures has no caret")

add_test(NAME cli.edit_rejects_unknown_step COMMAND elara-cli edit "explode 2")
set_tests_properties(cli.edit_rejects_unknown_step PROPERTIES
  PASS_REGULAR_EXPRESSION "unknown step: explode")

add_test(NAME cli.edit_rejects_undo_without_history COMMAND elara-cli edit undo)
set_tests_properties(cli.edit_rejects_undo_without_history PROPERTIES
  PASS_REGULAR_EXPRESSION "Document: nothing to undo")

# Transposition (ADR-0026): the open high E down a semitone is D#4 on the B
# string, and the keys follow to B major.
set(ELARA_CLI_TRANSPOSED "${CMAKE_CURRENT_BINARY_DIR}/cli-transposed.elara")
add_test(NAME cli.edit_transpose
  COMMAND elara-cli --json edit --out "${ELARA_CLI_TRANSPOSED}" "add-track electric-guitar"
    "add-track flute" "add-track drum-kit" "append-measures 2" "note 0 0 1 0 1" "transpose -1")
set_tests_properties(cli.edit_transpose PROPERTIES
  FIXTURES_SETUP cli_transposed
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"B major\"},{\"time\":\"4/4\",\"key\":\"B major\"}\\].*\"undo\":{\"count\":6,\"next\":\"Transpose\"}")
add_test(NAME cli.dump_transposed COMMAND elara-cli --json dump "${ELARA_CLI_TRANSPOSED}")
set_tests_properties(cli.dump_transposed PROPERTIES
  FIXTURES_REQUIRED cli_transposed
  PASS_REGULAR_EXPRESSION "\"notes\":\\[{\"pitch\":\"D#4\",\"string\":2,\"fret\":4}\\]")
add_test(NAME cli.edit_transpose_measures_and_tracks
  COMMAND elara-cli --json edit "add-track electric-guitar" "add-track flute" "append-measures 3"
    "transpose +2 measures 1 1 tracks 0")
set_tests_properties(cli.edit_transpose_measures_and_tracks PROPERTIES
  PASS_REGULAR_EXPRESSION "\"key\":\"C major\"},{\"time\":\"4/4\",\"key\":\"C major\"},{\"time\":\"4/4\",\"key\":\"C major\"}\\].*\"undo\":{\"count\":4,\"next\":\"Transpose\"}")
add_test(NAME cli.edit_rejects_unplayable_transpose
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "note 0 0 6 0 1" "transpose -1")
set_tests_properties(cli.edit_rejects_unplayable_transpose PROPERTIES
  PASS_REGULAR_EXPRESSION "measure 1: no free string can play D#2")
# Retuning (ADR-0046): A2 at the fifth fret of the low string moves to the
# seventh in drop D; with a capo and the frets kept, G2 at the third fret
# sounds A2.
set(ELARA_CLI_RETUNED "${CMAKE_CURRENT_BINARY_DIR}/cli-retuned.elara")
add_test(NAME cli.edit_tuning
  COMMAND elara-cli --json edit --out "${ELARA_CLI_RETUNED}" "add-track electric-guitar"
    "append-measures 1" "note 0 0 6 5 1" "tuning 0 guitar-drop-d")
set_tests_properties(cli.edit_tuning PROPERTIES
  FIXTURES_SETUP cli_retuned
  PASS_REGULAR_EXPRESSION "\"tuning\":\"D2 A2 D3 G3 B3 E4\",\"capo\":0,.*\"undo\":{\"count\":4,\"next\":\"Change tuning\"}")
add_test(NAME cli.dump_retuned COMMAND elara-cli --json dump "${ELARA_CLI_RETUNED}")
set_tests_properties(cli.dump_retuned PROPERTIES
  FIXTURES_REQUIRED cli_retuned
  PASS_REGULAR_EXPRESSION "\"notes\":\\[{\"pitch\":\"A2\",\"string\":6,\"fret\":7}\\]")
set(ELARA_CLI_CAPO "${CMAKE_CURRENT_BINARY_DIR}/cli-capo.elara")
add_test(NAME cli.edit_capo_keeping_frets
  COMMAND elara-cli --json edit --out "${ELARA_CLI_CAPO}" "add-track electric-guitar"
    "append-measures 1" "note 0 0 6 3 1" "tuning 0 capo 2 frets")
set_tests_properties(cli.edit_capo_keeping_frets PROPERTIES
  FIXTURES_SETUP cli_capo
  PASS_REGULAR_EXPRESSION "\"tuning\":\"E2 A2 D3 G3 B3 E4\",\"capo\":2,")
add_test(NAME cli.dump_capo COMMAND elara-cli --json dump "${ELARA_CLI_CAPO}")
set_tests_properties(cli.dump_capo PROPERTIES
  FIXTURES_REQUIRED cli_capo
  PASS_REGULAR_EXPRESSION "\"notes\":\\[{\"pitch\":\"A2\",\"string\":6,\"fret\":3}\\]")
add_test(NAME cli.edit_rejects_unknown_tuning
  COMMAND elara-cli edit "add-track electric-guitar" "tuning 0 guitar-banjo-x")
set_tests_properties(cli.edit_rejects_unknown_tuning PROPERTIES
  PASS_REGULAR_EXPRESSION "not a tuning: guitar-banjo-x")
add_test(NAME cli.edit_rejects_unplayable_tuning
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "note 0 0 6 0 1"
    "tuning 0 capo 2")
set_tests_properties(cli.edit_rejects_unplayable_tuning PROPERTIES
  PASS_REGULAR_EXPRESSION "measure 1: no free string can play E2")
# Signs and jumps (ADR-0049): 0 | 1 segno | 2 to coda | 3 D.S. al Coda | 4 coda | 5,
# saved, read back and played in order.
set(ELARA_CLI_JUMPS "${CMAKE_CURRENT_BINARY_DIR}/cli-jumps.elara")
add_test(NAME cli.edit_jumps
  COMMAND elara-cli --json edit --out "${ELARA_CLI_JUMPS}" "add-track electric-guitar"
    "append-measures 6" "sign 1 segno" "sign 2 to-coda" "jump 3 dal-segno al-coda" "sign 4 coda")
set_tests_properties(cli.edit_jumps PROPERTIES
  FIXTURES_SETUP cli_jumps
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"C major\"},{[^}]*\"signs\":\\[\"segno\"\\]},{[^}]*\"signs\":\\[\"to-coda\"\\]},{[^}]*\"jump\":\"dal-segno\",\"jumpThen\":\"al-coda\"},{[^}]*\"signs\":\\[\"coda\"\\]},.*\"undo\":{\"count\":6,\"next\":\"Change navigation marks\"}")
add_test(NAME cli.dump_jumps COMMAND elara-cli --json dump "${ELARA_CLI_JUMPS}")
set_tests_properties(cli.dump_jumps PROPERTIES
  FIXTURES_REQUIRED cli_jumps
  PASS_REGULAR_EXPRESSION "\"jump\":\"dal-segno\",\"jumpThen\":\"al-coda\"")
add_test(NAME cli.sequence_jumps COMMAND elara-cli sequence "${ELARA_CLI_JUMPS}")
set_tests_properties(cli.sequence_jumps PROPERTIES
  FIXTURES_REQUIRED cli_jumps
  PASS_REGULAR_EXPRESSION "played measures: 1 2 3 4 2 3 5 6\n")
add_test(NAME cli.edit_clears_jumps
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 2"
    "sign 0 segno fine" "jump 1 da-capo" "sign 0 none" "jump 1 none")
set_tests_properties(cli.edit_clears_jumps PROPERTIES
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"C major\"},{\"time\":\"4/4\",\"key\":\"C major\"}\\],.*\"undo\":{\"count\":6,")
add_test(NAME cli.edit_rejects_unknown_sign
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "sign 0 dolce")
add_test(NAME cli.edit_rejects_unknown_jump
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "jump 0 da-capo al-segno")
set_tests_properties(cli.edit_rejects_unknown_sign PROPERTIES
  PASS_REGULAR_EXPRESSION "not a sign: dolce")
set_tests_properties(cli.edit_rejects_unknown_jump PROPERTIES
  PASS_REGULAR_EXPRESSION "jump: not al-fine, al-coda or al-double-coda: al-segno")
# Tempo changes (ADR-0042): 0 at the song's 120 | 1 from 60 | 2 from 240. Each
# 4/4 measure of rests lasts 2 s, then 4 s, then 1 s.
set(ELARA_CLI_TEMPO "${CMAKE_CURRENT_BINARY_DIR}/cli-tempo.elara")
add_test(NAME cli.edit_tempo
  COMMAND elara-cli --json edit --out "${ELARA_CLI_TEMPO}" "add-track electric-guitar"
    "append-measures 3" "tempo-from 1 60" "tempo-from 2 240")
set_tests_properties(cli.edit_tempo PROPERTIES
  FIXTURES_SETUP cli_tempo
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"C major\"},{[^}]*\"tempo\":60},{[^}]*\"tempo\":240}\\],.*\"undo\":{\"count\":4,\"next\":\"Change tempo\"}")
add_test(NAME cli.sequence_tempo COMMAND elara-cli sequence "${ELARA_CLI_TEMPO}")
set_tests_properties(cli.sequence_tempo PROPERTIES
  FIXTURES_REQUIRED cli_tempo
  PASS_REGULAR_EXPRESSION "3 measures, 0 notes, [0-9]+ events in 7\\.000 s")
# Drawn above the first staff: a mark (a note and its number) at the start and
# one at each change, in measures 1 and 2.
add_test(NAME cli.render_tempo COMMAND elara-cli --json render "${ELARA_CLI_TEMPO}")
set_tests_properties(cli.render_tempo PROPERTIES
  FIXTURES_REQUIRED cli_tempo
  PASS_REGULAR_EXPRESSION "\"tempo\":6,")
# Gradual tempo changes (ADR-0082): measure 1 speeds up from 60 to measure 2's
# 120, so it lasts 4 ln 2 s instead of 4 (2 s + 2.773 s + 2 s); drawn as the
# marks of tempo-from with "accel." and a dashed line after the second.
set(ELARA_CLI_GRADUAL "${CMAKE_CURRENT_BINARY_DIR}/cli-gradual.elara")
add_test(NAME cli.edit_gradual_tempo
  COMMAND elara-cli --json edit --out "${ELARA_CLI_GRADUAL}" "add-track electric-guitar"
    "append-measures 4" "tempo-from 1 60" "tempo-gradual 1 on" "tempo-from 3 120")
set_tests_properties(cli.edit_gradual_tempo PROPERTIES
  FIXTURES_SETUP cli_gradual
  PASS_REGULAR_EXPRESSION "{[^}]*\"tempo\":60,\"gradual\":true},{[^}]*},{[^}]*\"tempo\":120}\\],.*\"undo\":{\"count\":5,\"next\":\"Change tempo\"}")
add_test(NAME cli.sequence_gradual_tempo COMMAND elara-cli sequence "${ELARA_CLI_GRADUAL}")
set_tests_properties(cli.sequence_gradual_tempo PROPERTIES
  FIXTURES_REQUIRED cli_gradual
  PASS_REGULAR_EXPRESSION "4 measures, 0 notes, [0-9]+ events in 9\\.545 s")
add_test(NAME cli.render_gradual_tempo COMMAND elara-cli --json render "${ELARA_CLI_GRADUAL}")
set_tests_properties(cli.render_gradual_tempo PROPERTIES
  FIXTURES_REQUIRED cli_gradual
  PASS_REGULAR_EXPRESSION "\"tempo\":8,")
# Each text says its face (ADR-0133): the tempo in bold, "accel." in italics.
add_test(NAME cli.render_text_faces COMMAND elara-cli --json render "${ELARA_CLI_GRADUAL}")
set_tests_properties(cli.render_text_faces PROPERTIES
  FIXTURES_REQUIRED cli_gradual
  PASS_REGULAR_EXPRESSION "{\"text\":\"= 60\",\"role\":\"tempo\",[^}]*\"face\":\"bold\",[^}]*},{\"text\":\"accel\\.\",\"role\":\"tempo\",[^}]*\"face\":\"italic\",")
add_test(NAME cli.edit_rejects_gradual_word
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 2" "tempo-gradual 1 maybe")
set_tests_properties(cli.edit_rejects_gradual_word PROPERTIES
  PASS_REGULAR_EXPRESSION "tempo-gradual: on or off, not maybe")
add_test(NAME cli.edit_clears_tempo
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 2" "tempo-from 1 60"
    "tempo-from 1 none")
set_tests_properties(cli.edit_clears_tempo PROPERTIES
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"C major\"},{\"time\":\"4/4\",\"key\":\"C major\"}\\],.*\"undo\":{\"count\":4,")
add_test(NAME cli.edit_rejects_tempo_out_of_range
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 2" "tempo-from 1 5")
add_test(NAME cli.edit_rejects_tempo_from_first_measure
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 2" "tempo-from 0 90")
set_tests_properties(cli.edit_rejects_tempo_out_of_range PROPERTIES
  PASS_REGULAR_EXPRESSION "tempo-from: 10 to 999 beats per minute, not 5")
set_tests_properties(cli.edit_rejects_tempo_from_first_measure PROPERTIES
  PASS_REGULAR_EXPRESSION "tempo-from: the first measure plays at the song's tempo \\(\"tempo <bpm>\"\\)")
# Triplet feel (ADR-0104): swung eighths from measure 1, straight again in
# measure 2; at quarter = 120 the second eighth starts after a third of a
# second, saved, read back and played.
set(ELARA_CLI_FEEL "${CMAKE_CURRENT_BINARY_DIR}/cli-feel.elara")
add_test(NAME cli.edit_sets_triplet_feel
  COMMAND elara-cli --json edit --out "${ELARA_CLI_FEEL}" "add-track electric-guitar"
    "append-measures 2" "note 0 0 1 3 8" "note 0 0 1 5 8" "feel 0 eighth" "feel 1 none")
set_tests_properties(cli.edit_sets_triplet_feel PROPERTIES
  FIXTURES_SETUP cli_feel
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"C major\",\"tripletFeel\":\"eighth\"},{\"time\":\"4/4\",\"key\":\"C major\"}\\],.*\"undo\":{\"count\":6,\"next\":\"Change triplet feel\"}")
add_test(NAME cli.sequence_triplet_feel COMMAND elara-cli sequence "${ELARA_CLI_FEEL}" --events)
set_tests_properties(cli.sequence_triplet_feel PROPERTIES
  FIXTURES_REQUIRED cli_feel
  PASS_REGULAR_EXPRESSION " +0\\.000 on   track 0  key  [0-9]+ velocity +[0-9]+\n +0\\.333 off  track 0  key  [0-9]+ velocity   0\n +0\\.333 on   track 0  key ")
add_test(NAME cli.edit_rejects_unknown_triplet_feel
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "feel 0 shuffle")
set_tests_properties(cli.edit_rejects_unknown_triplet_feel PROPERTIES
  PASS_REGULAR_EXPRESSION "not a triplet feel: shuffle")
# Repeats and alternate endings (ADR-0016): |: 1 | 2 (1., 2.) :| 3 (3.) | 4, played
# three times through, saved, read back and played in order.
set(ELARA_CLI_REPEATS "${CMAKE_CURRENT_BINARY_DIR}/cli-repeats.elara")
add_test(NAME cli.edit_repeats
  COMMAND elara-cli --json edit --out "${ELARA_CLI_REPEATS}" "add-track electric-guitar"
    "append-measures 4" "repeat 0 start" "repeat 1 end 3" "ending 1 1 2" "ending 2 3")
set_tests_properties(cli.edit_repeats PROPERTIES
  FIXTURES_SETUP cli_repeats
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{[^}]*\"repeatStart\":true},{[^}]*\"repeatTimes\":3,\"endings\":\\[1,2\\]},{[^}]*\"endings\":\\[3\\]},{\"time\":\"4/4\",\"key\":\"C major\"}\\],.*\"undo\":{\"count\":6,\"next\":\"Change repeats\"}")
add_test(NAME cli.dump_repeats COMMAND elara-cli --json dump "${ELARA_CLI_REPEATS}")
set_tests_properties(cli.dump_repeats PROPERTIES
  FIXTURES_REQUIRED cli_repeats
  PASS_REGULAR_EXPRESSION "\"repeatTimes\":3,\"alternateEndings\":\\[1,2\\]")
add_test(NAME cli.sequence_edited_repeats COMMAND elara-cli sequence "${ELARA_CLI_REPEATS}")
set_tests_properties(cli.sequence_edited_repeats PROPERTIES
  FIXTURES_REQUIRED cli_repeats
  PASS_REGULAR_EXPRESSION "played measures: 1 2 1 2 1 3 4\n")
add_test(NAME cli.edit_clears_repeats
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 1"
    "repeat 0 start end 2" "ending 0 1" "repeat 0 none" "ending 0 none")
set_tests_properties(cli.edit_clears_repeats PROPERTIES
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"C major\"}\\],.*\"undo\":{\"count\":6,")
add_test(NAME cli.edit_rejects_single_repeat
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "repeat 0 end 1")
add_test(NAME cli.edit_rejects_unknown_repeat
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "repeat 0 twice")
add_test(NAME cli.edit_rejects_ending_past_eight
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "ending 0 9")
add_test(NAME cli.edit_rejects_ending_zero
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "ending 0 0")
set_tests_properties(cli.edit_rejects_single_repeat PROPERTIES
  PASS_REGULAR_EXPRESSION "repeat: a section plays 2 to 255 times, not 1")
set_tests_properties(cli.edit_rejects_unknown_repeat PROPERTIES
  PASS_REGULAR_EXPRESSION "not a repeat: twice")
set_tests_properties(cli.edit_rejects_ending_past_eight PROPERTIES
  PASS_REGULAR_EXPRESSION "ending: times through are 1 to 8, not 9")
set_tests_properties(cli.edit_rejects_ending_zero PROPERTIES
  PASS_REGULAR_EXPRESSION "ending: times through are 1 to 8, not 0")
# Double barlines (ADR-0103): two set and one taken off again, each one undo
# step, saved and read back.
set(ELARA_CLI_DOUBLE_BARS "${CMAKE_CURRENT_BINARY_DIR}/cli-double-bars.elara")
add_test(NAME cli.edit_sets_double_barlines
  COMMAND elara-cli --json edit --out "${ELARA_CLI_DOUBLE_BARS}" "add-track electric-guitar"
    "append-measures 3" "barline 1 double" "barline 2 double" "barline 2 single")
set_tests_properties(cli.edit_sets_double_barlines PROPERTIES
  FIXTURES_SETUP cli_double_bars
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"time\":\"4/4\",\"key\":\"C major\"},{\"time\":\"4/4\",\"key\":\"C major\",\"doubleBar\":true},{\"time\":\"4/4\",\"key\":\"C major\"}\\],.*\"undo\":{\"count\":5,\"next\":\"Change barline\"}")
add_test(NAME cli.dump_double_barlines COMMAND elara-cli --json dump "${ELARA_CLI_DOUBLE_BARS}")
set_tests_properties(cli.dump_double_barlines PROPERTIES
  FIXTURES_REQUIRED cli_double_bars
  PASS_REGULAR_EXPRESSION "\"doubleBar\":true")
add_test(NAME cli.edit_rejects_unknown_barline
  COMMAND elara-cli edit "add-track electric-guitar" "append-measures 1" "barline 0 triple")
set_tests_properties(cli.edit_rejects_unknown_barline PROPERTIES
  PASS_REGULAR_EXPRESSION "barline: expected double or single, not triple")
# Copy and paste (ADR-0027): a measure pasted over the next, and a 3/4 measure
# pasted into a new measure before the first, bringing its time signature.
add_test(NAME cli.edit_copy_paste
  COMMAND elara-cli --json edit "add-track electric-guitar" "append-measures 2" "note 0 0 6 3 1"
    "copy 0 1" "paste 1")
set_tests_properties(cli.edit_copy_paste PROPERTIES
  PASS_REGULAR_EXPRESSION "\"beats\":2,\"notes\":2}\\],\"measureCount\":2,.*\"undo\":{\"count\":4,\"next\":\"Paste\"}")
add_test(NAME cli.edit_paste_insert
  COMMAND elara-cli --json edit "add-track electric-guitar" "add-track flute" "append-measures 2"
    "time 1 3/4" "note 0 1 6 3 2." "copy 1 1 track 0" "paste 0 insert")
set_tests_properties(cli.edit_paste_insert PROPERTIES
  PASS_REGULAR_EXPRESSION "\"name\":\"Electric Guitar\",.*\"beats\":2,\"notes\":2}.*\"measureCount\":3,\"measures\":\\[{\"time\":\"3/4\",\"key\":\"C major\"},{\"time\":\"4/4\",\"key\":\"C major\"},{\"time\":\"3/4\",\"key\":\"C major\"}\\]")
add_test(NAME cli.edit_rejects_paste_without_copy
  COMMAND elara-cli edit "add-track flute" "append-measures 1" "paste 0")
set_tests_properties(cli.edit_rejects_paste_without_copy PROPERTIES
  PASS_REGULAR_EXPRESSION "Paste: nothing to paste")
add_test(NAME cli.edit_rejects_malformed_copy
  COMMAND elara-cli edit "add-track flute" "append-measures 1" "copy 0 1 tracks 0")
set_tests_properties(cli.edit_rejects_malformed_copy PROPERTIES
  PASS_REGULAR_EXPRESSION "copy: expected track: tracks")
add_test(NAME cli.edit_rejects_malformed_transpose
  COMMAND elara-cli edit "add-track flute" "append-measures 1" "transpose 2 bars 1")
set_tests_properties(cli.edit_rejects_malformed_transpose PROPERTIES
  PASS_REGULAR_EXPRESSION "transpose: expected measures or tracks: bars")

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
  PASS_REGULAR_EXPRESSION "\"undo\":{\"count\":2,\"next\":\"Edit measure\"},\"redo\":{\"count\":0,\"next\":null},\"historyBytes\":[0-9]+,\"modified\":false}")

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

# Chord symbols under held notes split them into tied notes (ADR-0096): the
# whole E under C and Am becomes two tied halves.
add_test(NAME cli.import_splits_held_notes_under_chords
  COMMAND elara-cli --json import "${ELARA_CORPUS}/musicxml/lead-sheet.musicxml")
set_tests_properties(cli.import_splits_held_notes_under_chords PROPERTIES
  PASS_REGULAR_EXPRESSION "{\"duration\":\"2\",\"chord\":{\"name\":\"C\"},\"notes\":\\[{\"pitch\":\"E5\"}\\]},{\"duration\":\"2\",\"chord\":{\"name\":\"Am\"},\"notes\":\\[{\"pitch\":\"E5\",\"effects\":{\"tiedFromPrevious\":true}}")
add_test(NAME cli.import_musicxml_reports_warnings
  COMMAND elara-cli --json import "${ELARA_CORPUS}/musicxml/transposing.musicxml")
set_tests_properties(cli.import_musicxml_reports_warnings PROPERTIES
  PASS_REGULAR_EXPRESSION "\"warnings\":\\[[^]]*\"pickup measure filled with leading rests \\(measure 1\\)\"")

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

# Karaoke files (ADR-0084): read for their words, title and artist.
add_test(NAME cli.import_karaoke
  COMMAND elara-cli --json import "${ELARA_CORPUS}/midi/karaoke.kar")
set_tests_properties(cli.import_karaoke PROPERTIES
  PASS_REGULAR_EXPRESSION "\"title\":\"Corpus Song\",\"artist\":\"Ghost in the Groove\".*\"lyrics\":\\[\"Hel-\"\\]")
add_test(NAME cli.export_rejects_karaoke
  COMMAND elara-cli export "${ELARA_CLI_IMPORTED}" "${CMAKE_CURRENT_BINARY_DIR}/out.kar")
set_tests_properties(cli.export_rejects_karaoke PROPERTIES
  FIXTURES_REQUIRED cli_imported WILL_FAIL TRUE)

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

# Notation font (ADR-0011): the SMuFL metadata in fonts/ next to elara-cli.
add_test(NAME cli.font COMMAND elara-cli --json font)
set_tests_properties(cli.font PROPERTIES
  PASS_REGULAR_EXPRESSION "\"name\":\"Bravura\",\"version\":\"1\\.482\",.*\"described\":150,\"missing\":\\[\\],\"defaults\":{\"staffLineThickness\":0\\.13,\"stemThickness\":0\\.12,")

add_test(NAME cli.font_glyph COMMAND elara-cli --json font --glyph noteheadBlack)
set_tests_properties(cli.font_glyph PROPERTIES
  PASS_REGULAR_EXPRESSION "\"glyph\":\"noteheadBlack\",\"codepoint\":\"U\\+E0A4\",\"bbox\":{\"sw\":\\[0,-0\\.5\\],\"ne\":\\[1\\.18,0\\.5\\]},\"advanceWidth\":1\\.18,\"anchors\":{\"stemUpSE\":\\[1\\.18,0\\.168\\],\"stemDownNW\":\\[0,-0\\.168\\]")

add_test(NAME cli.font_rejects_unknown_glyph COMMAND elara-cli font --glyph noSuchGlyph)
set_tests_properties(cli.font_rejects_unknown_glyph PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.font_rejects_missing_metadata
  COMMAND elara-cli font --metadata "${CMAKE_CURRENT_BINARY_DIR}/no-such-metadata.json")
set_tests_properties(cli.font_rejects_missing_metadata PROPERTIES WILL_FAIL TRUE)

# Notation layout (ADR-0012): one staff as a display list, or as SVG.
add_test(NAME cli.render COMMAND elara-cli --json render "${ELARA_CORPUS}/musicxml/melody.musicxml")
set_tests_properties(cli.render PROPERTIES
  PASS_REGULAR_EXPRESSION "\"track\":0,\"staff\":0,\"first\":0,\"count\":4,\"svg\":null,.*\"counts\":\\{\"staffLine\":5,\"ledgerLine\":1,\"barline\":5,\"clef\":1,\"keySignature\":1,\"timeSignature\":4,\"notehead\":10,\"accidental\":2,")

# Where the notes of each measure start, as an editor finds its beats.
add_test(NAME cli.render_columns
  COMMAND elara-cli --json render "${ELARA_CORPUS}/musicxml/melody.musicxml" --count 1)
set_tests_properties(cli.render_columns PROPERTIES
  PASS_REGULAR_EXPRESSION "\"columns\":\\[\\{\"measure\":0,\"start\":\"0\",\"x\":[0-9.]+\\},\\{\"measure\":0,\"start\":\"1/4\",")

add_test(NAME cli.render_drums COMMAND elara-cli --json render "${ELARA_CORPUS}/musicxml/drums.musicxml")
set_tests_properties(cli.render_drums PROPERTIES
  PASS_REGULAR_EXPRESSION "\"notehead\":5,\"accidental\":0,.*\"glyph\":\"unpitchedPercussionClef1\",\"role\":\"clef\"")

# A piano's two staves are a grand staff: one brace joins them (ADR-0073).
add_test(NAME cli.render_grand_staff
  COMMAND elara-cli --json render "${ELARA_CORPUS}/musicxml/piano-voices.musicxml" --tracks 0)
set_tests_properties(cli.render_grand_staff PROPERTIES
  PASS_REGULAR_EXPRESSION "\"section\":0,\"brace\":1,\"arpeggio\":0,\"hairpin\":0,\"chord\":1,\"beatText\":1,\"lyric\":1,\"fermata\":0,\"octaveLine\":0,\"ornament\":0,\"pedal\":0,\"fingering\":0,\"parenthesis\":0\}.*\"glyph\":\"brace\",\"role\":\"brace\"")

# A strummed piano chord is rolled: an arpeggio line left of it on each staff
# of the grand staff, not an arrow above (ADR-0076).
add_test(NAME cli.render_arpeggio
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/piano.gp5" --tracks 0 --count 1)
set_tests_properties(cli.render_arpeggio PROPERTIES
  PASS_REGULAR_EXPRESSION "\"technique\":0,.*\"brace\":1,\"arpeggio\":[1-9][0-9]*,\"hairpin\":0,\"chord\":0,\"beatText\":0,\"lyric\":0,\"fermata\":0,\"octaveLine\":0,\"ornament\":0,\"pedal\":0,\"fingering\":0,\"parenthesis\":0\}.*\"role\":\"arpeggio\"")

# Hairpins are wedges under the staff (ADR-0078): the crescendo of bar 1 and the
# diminuendo of bar 3 of the guitar techniques, two lines each.
add_test(NAME cli.render_hairpins
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --view both)
set_tests_properties(cli.render_hairpins PROPERTIES
  PASS_REGULAR_EXPRESSION "\"arpeggio\":0,\"hairpin\":4,\"chord\":55,\"beatText\":0,\"lyric\":0,\"fermata\":0,\"octaveLine\":0,\"ornament\":0,\"pedal\":0,\"fingering\":0,\"parenthesis\":0\}.*\"role\":\"hairpin\",\"measure\":0,.*\"role\":\"hairpin\",\"measure\":2,")

add_test(NAME cli.render_svg
  COMMAND elara-cli render "${ELARA_CORPUS}/musicxml/melody.musicxml" --first 2 --count 1
          --svg "${CMAKE_CURRENT_BINARY_DIR}/melody-measure-3.svg")
set_tests_properties(cli.render_svg PROPERTIES
  PASS_REGULAR_EXPRESSION "measures 2 to 2 of 4.*saved .*melody-measure-3\\.svg"
  FIXTURES_SETUP cli_rendered)
add_test(NAME cli.render_svg_content
  COMMAND "${CMAKE_COMMAND}" -E cat "${CMAKE_CURRENT_BINARY_DIR}/melody-measure-3.svg")
set_tests_properties(cli.render_svg_content PROPERTIES
  PASS_REGULAR_EXPRESSION "@font-face\\{font-family:\"Bravura\";src:url\\(\"[^\"]*fonts/Bravura\\.otf\".*<text class=\"keySignature\""
  FIXTURES_REQUIRED cli_rendered)

add_test(NAME cli.render_rejects_missing_track
  COMMAND elara-cli render "${ELARA_CORPUS}/musicxml/melody.musicxml" --track 3)
set_tests_properties(cli.render_rejects_missing_track PROPERTIES WILL_FAIL TRUE)

# Tablature of a fretted staff, alone or under its standard notation.
add_test(NAME cli.render_tab
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --view tab)
set_tests_properties(cli.render_tab PROPERTIES
  PASS_REGULAR_EXPRESSION "\"view\":\"tab\",.*\"notehead\":0,\"accidental\":0,\"dot\":1,\"stem\":11,\"flag\":1,\"beam\":1,\"rest\":1,\"fretNumber\":12,\"tie\":0,\"slur\":1,\"technique\":4,\"bend\":3,\"slide\":1,\"vibrato\":6,.*\"staves\":\\[{\"track\":0,\"staff\":0,\"view\":\"tablature\",\"x\":0,\"y\":0,\"width\":[1-9][0-9.]*,\"height\":7\\.5,\"lines\":6}\\].*\"texts\":\\[{\"text\":\"0\",\"role\":\"fretNumber\",.*{\"text\":\"<12>\",.*{\"text\":\"P\\.M\\.\",.*{\"text\":\"full\",\"role\":\"bend\"")

add_test(NAME cli.render_both
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --view both)
set_tests_properties(cli.render_both PROPERTIES
  PASS_REGULAR_EXPRESSION "\"notehead\":12,.*\"rest\":1,\"fretNumber\":12,\"tie\":1,.*\"articulation\":2,\"measureNumber\":0,\"credit\":0,\"trackName\":0,\"repeat\":9,\"volta\":7,\"tuplet\":1,\"tempo\":4,\"dynamic\":3,\"navigation\":0,\"section\":0,\"brace\":0,\"arpeggio\":0,\"hairpin\":4,\"chord\":55,\"beatText\":0,\"lyric\":0,\"fermata\":0,\"octaveLine\":0,\"ornament\":0,\"pedal\":0,\"fingering\":0,\"parenthesis\":0},\"systems\":\\[{\"firstMeasure\":0,\"measureCount\":3,\"x\":0,\"y\":0,.*\"staves\":\\[{\"track\":0,\"staff\":0,\"view\":\"standard\",\"x\":0,\"y\":0,\"width\":[1-9][0-9.]*,\"height\":4,\"lines\":5},{\"track\":0,\"staff\":0,\"view\":\"tablature\",\"x\":0,\"y\":[1-9][0-9.]*,\"width\":[1-9][0-9.]*,\"height\":7\\.5,\"lines\":6}\\]")

add_test(NAME cli.render_tab_svg
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --view both
          --svg "${CMAKE_CURRENT_BINARY_DIR}/guitar-techniques-tab.svg")
set_tests_properties(cli.render_tab_svg PROPERTIES FIXTURES_SETUP cli_rendered_tab
  PASS_REGULAR_EXPRESSION "\\(both\\), measures 0 to 2 of 3")

add_test(NAME cli.render_tab_svg_content
  COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_BINARY_DIR}/guitar-techniques-tab.svg")
set_tests_properties(cli.render_tab_svg_content PROPERTIES FIXTURES_REQUIRED cli_rendered_tab
  PASS_REGULAR_EXPRESSION "<rect class=\"knockout\"[^>]*fill=\"white\".*<text class=\"t fretNumber\"[^>]*text-anchor=\"middle\"[^>]*>0</text>.*<path class=\"tie\".*<polygon class=\"bend\"")

add_test(NAME cli.render_rejects_tab_without_tuning
  COMMAND elara-cli render "${ELARA_CORPUS}/musicxml/melody.musicxml" --view tab)
set_tests_properties(cli.render_rejects_tab_without_tuning PROPERTIES WILL_FAIL TRUE)

# Systems on pages (ADR-0014): page size from the paper and the staff space,
# whole tracks, one SVG per page.
add_test(NAME cli.render_page
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --view both
          --layout page --paper letter --staff-space 2)
set_tests_properties(cli.render_page PROPERTIES
  PASS_REGULAR_EXPRESSION "\"view\":\"both\",\"layout\":\"page\",\"pages\":\\[{\"width\":107\\.95,\"top\":0,\"bottom\":139\\.7,.*\"credit\":[1-9].*\"systems\":\\[{\"firstMeasure\":0,\"measureCount\":3,\"x\":7\\.5,")

add_test(NAME cli.render_scroll
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --layout scroll
          --width 40 --no-credits)
set_tests_properties(cli.render_scroll PROPERTIES
  PASS_REGULAR_EXPRESSION "\"layout\":\"scroll\",\"pages\":\\[{\"width\":40,\"top\":0,.*\"measureNumber\":[1-9][0-9]*,\"credit\":0,.*\"systems\":\\[{\"firstMeasure\":0,.*{\"firstMeasure\":[1-9]")

# The endless page cut into strips between systems (ADR-0057): the first system,
# tall with its hairpin, fills the first strip, and the second strip holds the
# other two whole; one SVG per strip.
add_test(NAME cli.render_strips
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --layout strips
          --width 40 --strip-height 68 --no-credits)
set_tests_properties(cli.render_strips PROPERTIES
  PASS_REGULAR_EXPRESSION "\"layout\":\"strips\",\"pages\":\\[{\"width\":40,\"top\":0,\"bottom\":[0-9.]+,\"counts\":{[^}]*},\"systems\":\\[{\"firstMeasure\":0,[^{]*}\\],\"staves\".*},{\"width\":40,\"top\":0,\"bottom\":[0-9.]+,\"counts\":{[^}]*},\"systems\":\\[{\"firstMeasure\":1,[^{]*{\"firstMeasure\":2,[^{]*}\\],\"staves\"")
add_test(NAME cli.render_strips_svg
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --layout strips
          --width 40 --strip-height 68 --no-credits
          --svg "${CMAKE_CURRENT_BINARY_DIR}/strips.svg")
set_tests_properties(cli.render_strips_svg PROPERTIES
  PASS_REGULAR_EXPRESSION "measures 0 to 2 of 3: 3 systems on 2 strips.*saved .*strips-1\\.svg.*saved .*strips-2\\.svg")
add_test(NAME cli.render_rejects_flat_strips
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --layout strips
          --strip-height 0)
set_tests_properties(cli.render_rejects_flat_strips PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.render_tracks_pages
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/band.gp" --tracks all --layout page
          --staff-space 6 --svg "${CMAKE_CURRENT_BINARY_DIR}/band-pages.svg")
set_tests_properties(cli.render_tracks_pages PROPERTIES FIXTURES_SETUP cli_rendered_pages
  PASS_REGULAR_EXPRESSION "5 staves \\(standard\\), measures 0 to 1 of 2: 2 systems on 2 pages.*trackName 4.*saved .*band-pages-1\\.svg.*saved .*band-pages-2\\.svg")
add_test(NAME cli.render_tracks_pages_content
  COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_BINARY_DIR}/band-pages-1.svg")
set_tests_properties(cli.render_tracks_pages_content PROPERTIES FIXTURES_REQUIRED cli_rendered_pages
  PASS_REGULAR_EXPRESSION "viewBox=\"0 0 35 49\\.5\".*<text class=\"t credit\"[^>]*>Corpus Band</text>.*<text class=\"t trackName\"")

add_test(NAME cli.render_rejects_tracks_with_track
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/band.gp" --tracks 0 --track 1)
set_tests_properties(cli.render_rejects_tracks_with_track PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli.render_rejects_bad_tracks
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/band.gp" --tracks 9)
set_tests_properties(cli.render_rejects_bad_tracks PROPERTIES WILL_FAIL TRUE)

# PDF (ADR-0074): every page in one document, the glyphs drawn from the font's
# outlines. The text before the first compressed stream is plain.
add_test(NAME cli.render_pdf
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/band.gp" --tracks all --layout page
          --staff-space 6 --pdf "${CMAKE_CURRENT_BINARY_DIR}/band-pages.pdf")
set_tests_properties(cli.render_pdf PROPERTIES FIXTURES_SETUP cli_rendered_pdf
  PASS_REGULAR_EXPRESSION "\"svg\":null,\"pdf\":\\{\"file\":\"[^\"]*band-pages\\.pdf\",\"pages\":2,\"bytes\":[1-9][0-9]+\\},")
add_test(NAME cli.render_pdf_content
  COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_BINARY_DIR}/band-pages.pdf")
set_tests_properties(cli.render_pdf_content PROPERTIES FIXTURES_REQUIRED cli_rendered_pdf
  PASS_REGULAR_EXPRESSION "^%PDF-1\\.7.*/Type /Pages /Kids \\[[0-9]+ 0 R [0-9]+ 0 R\\] /Count 2 >>.*/Title <FEFF0043006F0072007000750073002000420061006E0064>")
add_test(NAME cli.render_pdf_line
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/melody.gp3" --pdf "${CMAKE_CURRENT_BINARY_DIR}/melody.pdf")
set_tests_properties(cli.render_pdf_line PROPERTIES
  PASS_REGULAR_EXPRESSION "saved .*melody\\.pdf \\(1 page, [1-9][0-9]+ bytes\\)")
add_test(NAME cli.render_pdf_rejects_unwritable
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/melody.gp3"
          --pdf "${CMAKE_CURRENT_BINARY_DIR}/no-such-folder/melody.pdf")
set_tests_properties(cli.render_pdf_rejects_unwritable PROPERTIES WILL_FAIL TRUE)
# Metadata without its OpenType font next to it: SVG still works, PDF cannot.
file(COPY "${ELARA_FONT_DIR}/Bravura.json" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/metadata-only")
add_test(NAME cli.render_pdf_needs_the_font
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/melody.gp3"
          --metadata "${CMAKE_CURRENT_BINARY_DIR}/metadata-only/Bravura.json"
          --pdf "${CMAKE_CURRENT_BINARY_DIR}/metadata-only.pdf")
set_tests_properties(cli.render_pdf_needs_the_font PROPERTIES WILL_FAIL TRUE)

# Snapshot tests: every corpus file imported with --json must give what
# tests/snapshots/import/<folder>/<name>.json holds. To refresh them after an
# intended change: ELARA_UPDATE_SNAPSHOTS=1 python scripts/elara.py test -R snapshot
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB _corpus_files CONFIGURE_DEPENDS
  "${ELARA_CORPUS}/musicxml/*.musicxml"
  "${ELARA_CORPUS}/musicxml/*.mxl"
  "${ELARA_CORPUS}/midi/*.mid"
  "${ELARA_CORPUS}/midi/*.kar"
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

# Render snapshots: every corpus file laid out on A4 pages, all tracks as
# notation and tablature, must write the SVG that
# tests/snapshots/render/<folder>/<name>.svg holds (<name>-1.svg, -2.svg... for
# several pages); the guitar techniques also as notation only and as tablature
# only. Refreshed like the import snapshots.
set(_render_snapshots "${CMAKE_CURRENT_SOURCE_DIR}/../snapshots/render")
foreach(_file IN LISTS _corpus_files)
  file(RELATIVE_PATH _relative "${ELARA_CORPUS}" "${_file}")
  string(REGEX REPLACE "\.[^./]+$" "" _name "${_relative}")
  string(REGEX REPLACE "[/.-]" "_" _test "${_name}")
  add_test(NAME cli.render_snapshot_${_test}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_render_snapshot.py"
            "$<TARGET_FILE:elara-cli>" "${_file}" "${_render_snapshots}/${_name}" --view both)
endforeach()
foreach(_view IN ITEMS standard tab)
  add_test(NAME cli.render_snapshot_gp_guitar_techniques_${_view}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_render_snapshot.py"
            "$<TARGET_FILE:elara-cli>" "${ELARA_CORPUS}/gp/guitar-techniques.gp"
            "${_render_snapshots}/gp/guitar-techniques-${_view}" --view ${_view})
endforeach()

# The sequencer (ADR-0017): measures in play order with their times, notes and
# note events.
add_test(NAME cli.sequence_repeats
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/gp/guitar-techniques.gp")
set_tests_properties(cli.sequence_repeats PROPERTIES
  PASS_REGULAR_EXPRESSION "\"measures\":\\[{\"measure\":0,\"start\":0,[^}]*},{\"measure\":1,[^}]*},{\"measure\":0,[^}]*},{\"measure\":1,[^}]*},{\"measure\":0,[^}]*},{\"measure\":2,[^}]*}\\],\"noteCount\":25,\"eventCount\":93}")
add_test(NAME cli.sequence_endings
  COMMAND elara-cli sequence "${ELARA_CORPUS}/gp/techniques.gp5")
set_tests_properties(cli.sequence_endings PROPERTIES
  PASS_REGULAR_EXPRESSION "played measures: 1 2 3 2 4\n5 measures, 23 notes, 69 events in 13\\.950 s")
# Signs and jumps read from Guitar Pro (ADR-0049): D.S. al Coda from a Guitar
# Pro 5 file, D.S.S. al Double Coda from GPIF.
add_test(NAME cli.sequence_dal_segno COMMAND elara-cli sequence "${ELARA_CORPUS}/gp/dal-segno.gp5")
set_tests_properties(cli.sequence_dal_segno PROPERTIES
  PASS_REGULAR_EXPRESSION "played measures: 1 2 3 4 2 3 5 6\n")
add_test(NAME cli.sequence_double_coda
  COMMAND elara-cli sequence "${ELARA_CORPUS}/gp/double-coda.gp")
set_tests_properties(cli.sequence_double_coda PROPERTIES
  PASS_REGULAR_EXPRESSION "played measures: 1 2 3 4 5 3 4 7\n")
# Measure repeats (ADR-0102): Guitar Pro's simile marks are kept, the repeated
# measures sound (4 + 4 + 4 + 4 + 4 + 4 + 1 notes) and show their signs on the
# notation and the tablature.
add_test(NAME cli.sequence_simile COMMAND elara-cli sequence "${ELARA_CORPUS}/gp/simile.gp")
set_tests_properties(cli.sequence_simile PROPERTIES
  PASS_REGULAR_EXPRESSION "7 measures, 25 notes, 50 events in 14\\.000 s")
add_test(NAME cli.import_simile COMMAND elara-cli --json import "${ELARA_CORPUS}/gp/simile.gp")
set_tests_properties(cli.import_simile PROPERTIES
  PASS_REGULAR_EXPRESSION "\"warnings\":\\[\\].*\"repeats\":\\[{\"measure\":1,\"repeat\":\"one\"},{\"measure\":4,\"repeat\":\"two-first\"},{\"measure\":5,\"repeat\":\"two-second\"}\\]")
add_test(NAME cli.render_simile
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/simile.gp" --view both)
set_tests_properties(cli.render_simile PROPERTIES PASS_REGULAR_EXPRESSION "\"repeat\":4,")
# To MusicXML and back: the signs come back, with no warnings.
set(ELARA_CLI_SIMILE "${CMAKE_CURRENT_BINARY_DIR}/cli-simile")
add_test(NAME cli.import_simile_saves_native
  COMMAND elara-cli import "${ELARA_CORPUS}/gp/simile.gp" --out "${ELARA_CLI_SIMILE}.elara" --force)
set_tests_properties(cli.import_simile_saves_native PROPERTIES FIXTURES_SETUP cli_simile_native)
add_test(NAME cli.export_simile_musicxml
  COMMAND elara-cli --json export "${ELARA_CLI_SIMILE}.elara" "${ELARA_CLI_SIMILE}.musicxml" --force)
set_tests_properties(cli.export_simile_musicxml PROPERTIES
  FIXTURES_REQUIRED cli_simile_native
  FIXTURES_SETUP cli_simile_musicxml
  PASS_REGULAR_EXPRESSION "\"warnings\":\\[\\]}")
add_test(NAME cli.import_simile_musicxml
  COMMAND elara-cli --json import "${ELARA_CLI_SIMILE}.musicxml")
set_tests_properties(cli.import_simile_musicxml PROPERTIES
  FIXTURES_REQUIRED cli_simile_musicxml
  PASS_REGULAR_EXPRESSION "\"warnings\":\\[\\].*\"repeats\":\\[{\"measure\":1,\"repeat\":\"one\"},{\"measure\":4,\"repeat\":\"two-first\"},{\"measure\":5,\"repeat\":\"two-second\"}\\]")
# Drawn above the staff: the segno and coda signs, "To Coda" and "D.S. al Coda".
add_test(NAME cli.render_navigation
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/dal-segno.gp5")
set_tests_properties(cli.render_navigation PROPERTIES
  PASS_REGULAR_EXPRESSION "\"navigation\":4,\"section\":0,\"brace\":0,\"arpeggio\":0,\"hairpin\":0,\"chord\":0,\"beatText\":0,\"lyric\":0,\"fermata\":0,\"octaveLine\":0,\"ornament\":0,\"pedal\":0,\"fingering\":0,\"parenthesis\":0}")
# Sections drawn boxed above everything (ADR-0063): two names, four sides each.
add_test(NAME cli.render_sections
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/double-coda.gp")
set_tests_properties(cli.render_sections PROPERTIES
  PASS_REGULAR_EXPRESSION "\"section\":10,\"brace\":0,\"arpeggio\":0,\"hairpin\":0,\"chord\":0,\"beatText\":0,\"lyric\":0,\"fermata\":0,\"octaveLine\":0,\"ornament\":0,\"pedal\":0,\"fingering\":0,\"parenthesis\":0}")
# ... and kept on the way to MusicXML: saved, exported, read back.
set(ELARA_CLI_DOUBLE_CODA "${CMAKE_CURRENT_BINARY_DIR}/cli-double-coda")
add_test(NAME cli.import_double_coda COMMAND elara-cli import "${ELARA_CORPUS}/gp/double-coda.gp"
  --out "${ELARA_CLI_DOUBLE_CODA}.elara" --force)
set_tests_properties(cli.import_double_coda PROPERTIES FIXTURES_SETUP cli_double_coda)
add_test(NAME cli.export_double_coda COMMAND elara-cli export "${ELARA_CLI_DOUBLE_CODA}.elara"
  "${ELARA_CLI_DOUBLE_CODA}.musicxml" --force)
set_tests_properties(cli.export_double_coda PROPERTIES
  FIXTURES_REQUIRED cli_double_coda FIXTURES_SETUP cli_double_coda_exported)
add_test(NAME cli.sequence_exported_double_coda
  COMMAND elara-cli sequence "${ELARA_CLI_DOUBLE_CODA}.musicxml")
set_tests_properties(cli.sequence_exported_double_coda PROPERTIES
  FIXTURES_REQUIRED cli_double_coda_exported
  PASS_REGULAR_EXPRESSION "played measures: 1 2 3 4 5 3 4 7\n")
add_test(NAME cli.sequence_events
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/musicxml/melody.musicxml" --notes --events)
set_tests_properties(cli.sequence_events PROPERTIES
  PASS_REGULAR_EXPRESSION "\"duration\":8\\.4,.*\"notes\":\\[{\"track\":0,\"staff\":0,\"voice\":0,\"pass\":0,\"start\":0,\"end\":0\\.6,\"key\":60,\"velocity\":80},.*\"events\":\\[{\"time\":0,\"kind\":\"on\",\"track\":0,\"key\":60,\"velocity\":80},{\"time\":0\\.6,\"kind\":\"off\",\"track\":0,\"key\":60,\"velocity\":0},{\"time\":0\\.6,\"kind\":\"on\",\"track\":0,\"key\":62,")
# Articulations (ADR-0022): the palm mute, the natural harmonic that rings
# until its string is struck again in the next pass, the accented dead note
# (ff 112 + 16, at most 127) and the staccato note (half of the 0.75 s its
# grace note leaves it, ADR-0079), softer under its diminuendo (ADR-0078).
add_test(NAME cli.sequence_articulations
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/gp/guitar-techniques.gp" --notes --events)
set_tests_properties(cli.sequence_articulations PROPERTIES
  PASS_REGULAR_EXPRESSION "\"start\":0,\"end\":0\\.6,\"key\":40,\"velocity\":80,\"touch\":\"palm-mute\"}.*\"start\":1\\.8,\"end\":6\\.9,\"key\":73,\"velocity\":88,\"touch\":\"harmonic\"}.*\"start\":4\\.35,\"end\":5\\.1,\"key\":40,\"velocity\":127,\"touch\":\"dead\"}.*\"start\":12\\.975,\"end\":13\\.35,\"key\":57,\"velocity\":76}.*\"events\":\\[{\"time\":0,\"kind\":\"on\",\"track\":0,\"key\":40,\"velocity\":80,\"touch\":\"palm-mute\"}")
# Pitch articulations (ADR-0023): the hammer-on in the triplet is not struck
# but joins the voice of the note before it a whole tone up (ADR-0070), and
# the shift slide goes up to the next note over the last half of the staccato
# note its grace note shortened (ADR-0079).
add_test(NAME cli.sequence_pitch
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/gp/guitar-techniques.gp" --notes --events)
set_tests_properties(cli.sequence_pitch PROPERTIES
  PASS_REGULAR_EXPRESSION "\"start\":0\\.8,\"end\":1,\"key\":49,\"velocity\":80,\"struck\":false}.*{\"time\":0\\.8,\"kind\":\"legato\",\"track\":0,\"key\":47,\"cents\":200,\"velocity\":80},{\"time\":1,\"kind\":\"off\",\"track\":0,\"key\":47,\"velocity\":0}.*{\"time\":13\\.1625,\"kind\":\"pitch\",\"track\":0,\"key\":57,\"cents\":200,\"glide\":0\\.1875}")
# Strums and tremolo picking (ADR-0025): the upstroke strikes the chord from
# its highest string, 0.01 s apart; the tremolo-picked quarter is struck every
# sixteenth, and its last attack picks up its slide out where the attack
# before it got to.
add_test(NAME cli.sequence_strums
  COMMAND elara-cli sequence "${ELARA_CORPUS}/gp/techniques.gp5" --events)
set_tests_properties(cli.sequence_strums PROPERTIES
  PASS_REGULAR_EXPRESSION "\n +10\\.950 on   track 0  key  68 velocity  96\n +10\\.960 on   track 0  key  64 velocity  96\n +10\\.970 on   track 0  key  59 velocity  96\n.*\n +12\\.637 off  track 0  key  71 velocity   0\n +12\\.637 on   track 0  key  71 velocity  96\n.*\n +13\\.012 pitch  track 0  key  71 cents   -31 glide 0\\.000\n")
add_test(NAME cli.render_audio_touches
  COMMAND elara-cli render-audio "${ELARA_CORPUS}/gp/guitar-techniques.gp" --tail 0 --events)
set_tests_properties(cli.render_audio_touches PROPERTIES
  PASS_REGULAR_EXPRESSION "\n +0 on   track 0  key  40 velocity  80  palm-mute\n.* on   track 0  key  73 velocity  88  harmonic\n.* on   track 0  key  40 velocity 127  dead\n")
# Hairpins ramp the velocity (ADR-0078): the crescendo of bar 1 leads from mf
# (80) to the forte of bar 2 (96), so its second quarter is struck halfway at
# 88; the diminuendo of bar 3 has no softer dynamic after it and falls one step
# (16), so its eighth, three quarters of the way, is struck at 68.
add_test(NAME cli.sequence_hairpins
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/gp/guitar-techniques.gp" --notes)
set_tests_properties(cli.sequence_hairpins PROPERTIES
  PASS_REGULAR_EXPRESSION "\"start\":1.2,\"end\":1.8,\"key\":59,\"velocity\":80\},\{[^}]*\"start\":1.8,\"end\":6.9,\"key\":73,\"velocity\":88,.*\"pass\":5,\"start\":12.6,[^}]*\"velocity\":80\}.*\"pass\":5,\"start\":13.725,\"end\":14.1,\"key\":59,\"velocity\":68\}")
# Grace notes (ADR-0079): the thirty-second before the last beat of the Guitar
# Pro 3 melody cuts the eighth before it short and the beat's open E is
# hammered on from it; the eighth grace before the first beat of the last
# measure of guitar-techniques.gp borrows no time from the measure before and
# delays its beat.
add_test(NAME cli.sequence_grace_before_beat
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/gp/melody.gp3" --notes)
set_tests_properties(cli.sequence_grace_before_beat PROPERTIES
  PASS_REGULAR_EXPRESSION "\"start\":4\\.4,\"end\":4\\.7,\"key\":45,\"velocity\":96\},\{[^}]*\"start\":4\\.7,\"end\":4\\.8,\"key\":43,\"velocity\":96\},\{[^}]*\"start\":4\\.8,\"end\":5\\.6,\"key\":40,\"velocity\":96,\"struck\":false\}")
add_test(NAME cli.sequence_grace_on_measure_start
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/gp/guitar-techniques.gp" --notes)
set_tests_properties(cli.sequence_grace_on_measure_start PROPERTIES
  PASS_REGULAR_EXPRESSION "\"pass\":5,\"start\":12\\.6,\"end\":12\\.975,\"key\":59,\"velocity\":80\},\{[^}]*\"pass\":5,\"start\":12\\.975,\"end\":13\\.35,\"key\":57,")
add_test(NAME cli.sequence_rejects_missing_file
  COMMAND elara-cli sequence "${ELARA_CORPUS}/missing.gp")
set_tests_properties(cli.sequence_rejects_missing_file PROPERTIES WILL_FAIL TRUE)
# Practice playback (ADR-0024): half speed doubles every time; the metronome
# clicks every beat of band.gp (two 4/4 measures at quarter = 90) on a fifth
# track, a high block on each first beat.
add_test(NAME cli.sequence_speed
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/musicxml/melody.musicxml" --speed 50)
set_tests_properties(cli.sequence_speed PROPERTIES
  PASS_REGULAR_EXPRESSION "\"duration\":16\\.8,\"speed\":0\\.5,\"metronome\":false,\"measures\":\\[{\"measure\":0,\"start\":0,\"end\":4\\.8}")
add_test(NAME cli.sequence_metronome
  COMMAND elara-cli sequence "${ELARA_CORPUS}/gp/band.gp" --metronome --events)
set_tests_properties(cli.sequence_metronome PROPERTIES
  PASS_REGULAR_EXPRESSION "112 events in 5\\.333 s\nat 100 % speed, metronome on track 4\n.*\n +0\\.000 on   track 4  key  76 velocity 100\n.*\n +0\\.667 on   track 4  key  77 velocity  80\n.*\n +2\\.667 on   track 4  key  76 velocity 100\n")
add_test(NAME cli.render_audio_metronome
  COMMAND elara-cli --json render-audio "${ELARA_CORPUS}/musicxml/melody.musicxml" --tail 0
          --metronome --speed 200)
set_tests_properties(cli.render_audio_metronome PROPERTIES
  PASS_REGULAR_EXPRESSION "\"seconds\":4\\.2,.*\"eventCount\":4[0-9],\"speed\":2,\"metronome\":true}")
add_test(NAME cli.sequence_rejects_speed
  COMMAND elara-cli sequence "${ELARA_CORPUS}/musicxml/melody.musicxml" --speed 300)
set_tests_properties(cli.sequence_rejects_speed PROPERTIES WILL_FAIL TRUE)

# The built-in synthesizer, offline (ADR-0018). melody.musicxml lasts 8.4 s;
# with the default 2 s tail that is 10.4 s, 499200 frames at 48 kHz.
set(_melody_wav "${CMAKE_CURRENT_BINARY_DIR}/melody.wav")
add_test(NAME cli.render_audio_events
  COMMAND elara-cli --json render-audio "${ELARA_CORPUS}/musicxml/melody.musicxml"
          --out "${_melody_wav}" --events)
set_tests_properties(cli.render_audio_events PROPERTIES
  PASS_REGULAR_EXPRESSION "\"sampleRate\":48000,\"channels\":2,\"frames\":499200,\"seconds\":10\\.4,\"peak\":0\\.[0-9]*[1-9][0-9]*,\"rms\":0\\.[0-9]+,\"tracks\":\\[{\"track\":0,\"name\":\"Flute\",\"timbre\":\"wind\",\"sound\":\"wind\"}\\],\"eventCount\":20,\"events\":\\[{\"frame\":0,\"kind\":\"on\",\"track\":0,\"key\":60,\"velocity\":80},{\"frame\":28800,\"kind\":\"off\",\"track\":0,\"key\":60,\"velocity\":0},{\"frame\":28800,\"kind\":\"on\",\"track\":0,\"key\":62,"
  FIXTURES_SETUP melody_wav)
# 44-byte header + 499200 stereo 16-bit frames.
add_test(NAME cli.render_audio_file
  COMMAND "${CMAKE_COMMAND}" -DFILE=${_melody_wav} -DEXPECTED_SIZE=1996844
          -P "${CMAKE_CURRENT_SOURCE_DIR}/check_file_size.cmake")
set_tests_properties(cli.render_audio_file PROPERTIES FIXTURES_REQUIRED melody_wav)
add_test(NAME cli.render_audio_band
  COMMAND elara-cli render-audio "${ELARA_CORPUS}/gp/band.gp" --sample-rate 44100 --tail 1)
set_tests_properties(cli.render_audio_band PROPERTIES
  PASS_REGULAR_EXPRESSION "rendered 6\\.333 s \\(279300 frames @ 44100 Hz, stereo\\), peak 0\\.[0-9]+, rms 0\\.[0-9]+, 96 events\n  track 1 Bass: bass\n  track 2 Piano: piano\n  track 3 Drums: drums\n  track 4 Clarinet: wind\n")
add_test(NAME cli.render_audio_rejects_missing_file
  COMMAND elara-cli render-audio "${ELARA_CORPUS}/missing.gp")
set_tests_properties(cli.render_audio_rejects_missing_file PROPERTIES WILL_FAIL TRUE)
# SoundFonts (ADR-0043), with a test font of six presets that all play one sine
# wave: band.gp's bass, piano and drums find their General MIDI presets; its
# clarinet (program 71) has no reed in the font and plays its first program.
set(_test_font "${CMAKE_CURRENT_BINARY_DIR}/test-soundfont.sf2")
add_test(NAME cli.soundfont_write COMMAND elara_make_test_soundfont "${_test_font}")
set_tests_properties(cli.soundfont_write PROPERTIES FIXTURES_SETUP cli_soundfont)
add_test(NAME cli.soundfont COMMAND elara-cli --json soundfont "${_test_font}")
set_tests_properties(cli.soundfont PROPERTIES
  FIXTURES_REQUIRED cli_soundfont
  PASS_REGULAR_EXPRESSION "\"presetCount\":6,\"presets\":\\[{\"bank\":0,\"program\":0,\"name\":\"Test Piano\"},{\"bank\":0,\"program\":25,\"name\":\"Test Guitar\"},.*{\"bank\":128,\"program\":0,\"name\":\"Test Kit\"}\\]}")
add_test(NAME cli.soundfont_text COMMAND elara-cli soundfont "${_test_font}")
set_tests_properties(cli.soundfont_text PROPERTIES
  FIXTURES_REQUIRED cli_soundfont
  PASS_REGULAR_EXPRESSION ": 6 presets\n    0:  0  Test Piano\n    0: 25  Test Guitar\n")
add_test(NAME cli.soundfont_rejects_other_files
  COMMAND elara-cli soundfont "${ELARA_CORPUS}/gp/band.gp")
set_tests_properties(cli.soundfont_rejects_other_files PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli.render_audio_soundfont
  COMMAND elara-cli --json render-audio "${ELARA_CORPUS}/gp/band.gp" --tail 0
          --soundfont "${_test_font}")
set_tests_properties(cli.render_audio_soundfont PROPERTIES
  FIXTURES_REQUIRED cli_soundfont
  PASS_REGULAR_EXPRESSION "\"soundFont\":\"[^\"]*test-soundfont\\.sf2\",\"sfz\":\\[\\],\"sampleRate\":48000,.*\"peak\":0\\.[0-9]*[1-9][0-9]*,.*\"tracks\":\\[{\"track\":0,\"name\":\"Bass\",\"timbre\":\"bass\",\"sound\":\"Test Bass\"},{\"track\":1,\"name\":\"Piano\",\"timbre\":\"piano\",\"sound\":\"Test Piano\"},{\"track\":2,\"name\":\"Drums\",\"timbre\":\"drums\",\"sound\":\"Test Kit\"},{\"track\":3,\"name\":\"Clarinet\",\"timbre\":\"wind\",\"sound\":\"Test Piano\"}\\]")
add_test(NAME cli.render_audio_rejects_missing_soundfont
  COMMAND elara-cli render-audio "${ELARA_CORPUS}/gp/band.gp" --soundfont missing.sf2)
set_tests_properties(cli.render_audio_rejects_missing_soundfont PROPERTIES WILL_FAIL TRUE)

# SFZ instruments (ADR-0066), with a test guitar of two plucked samples, a map
# it includes, a release region and an opcode Elara does not play. It plays
# band.gp's bass, piano and clarinet; the drums stay on the built-in
# synthesizer, or on the SoundFont when there is one.
set(_test_sfz "${CMAKE_CURRENT_BINARY_DIR}/test-sfz/test-guitar.sfz")
add_test(NAME cli.sfz_write COMMAND elara_make_test_sfz "${CMAKE_CURRENT_BINARY_DIR}/test-sfz")
set_tests_properties(cli.sfz_write PROPERTIES FIXTURES_SETUP cli_sfz)
add_test(NAME cli.sfz COMMAND elara-cli --json sfz "${_test_sfz}")
set_tests_properties(cli.sfz PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  PASS_REGULAR_EXPRESSION "\"name\":\"test-guitar\",\"regions\":3,\"samples\":2,\"sampleBytes\":176400,\"lowestKey\":0,\"highestKey\":127,\"missingSamples\":\\[\\],\"unknownOpcodes\":\\[\"lfoN_pitch_onccN\"\\],\"skippedHeaders\":\\[\\],\"techniques\":{\"palm-mute\":{\"cc\":70,\"label\":\"Muting\"},\"dead\":null,\"harmonic\":null,\"legato\":null},\"loadSeconds\":[0-9.e-]+}")
add_test(NAME cli.sfz_text COMMAND elara-cli sfz "${_test_sfz}")
set_tests_properties(cli.sfz_text PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  PASS_REGULAR_EXPRESSION "^test-guitar: 3 regions over keys 0 to 127, 2 samples \\(0\\.2 MB\\) read in [0-9.]+ s\n  opcodes not played: lfoN_pitch_onccN\n  palm-mute: controller 70 \"Muting\"\n$")
# The same pluck as a FLAC sample: as many samples in memory as the WAV.
add_test(NAME cli.sfz_flac
  COMMAND elara-cli --json sfz "${CMAKE_CURRENT_BINARY_DIR}/test-sfz/test-flac.sfz")
set_tests_properties(cli.sfz_flac PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  PASS_REGULAR_EXPRESSION "\"name\":\"test-flac\",\"regions\":1,\"samples\":1,\"sampleBytes\":88200,.*\"missingSamples\":\\[\\],")
add_test(NAME cli.render_audio_sfz_flac
  COMMAND elara-cli --json render-audio "${ELARA_CORPUS}/gp/band.gp"
          --sfz "${CMAKE_CURRENT_BINARY_DIR}/test-sfz/test-flac.sfz")
set_tests_properties(cli.render_audio_sfz_flac PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  PASS_REGULAR_EXPRESSION "\"peak\":0\\.[0-9]*[1-9][0-9]*,.*\"sound\":\"test-flac\"")
# Hammer-ons and pull-offs play the library's legato samples (ADR-0070): the
# test library names a keyswitch for them, an octave up. A D3 picked for an
# eighth and an E3 hammered on for a dotted half sound mostly an E4.
set(_legato_sfz "${CMAKE_CURRENT_BINARY_DIR}/test-sfz/test-legato.sfz")
set(_legato_song "${CMAKE_CURRENT_BINARY_DIR}/sfz-legato.elara")
set(_legato_wav "${CMAKE_CURRENT_BINARY_DIR}/sfz-legato.wav")
add_test(NAME cli.sfz_legato COMMAND elara-cli --json sfz "${_legato_sfz}")
set_tests_properties(cli.sfz_legato PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  PASS_REGULAR_EXPRESSION "\"legato\":{\"keyswitch\":25,\"label\":\"Hammer-ons and pull-offs\"}")
add_test(NAME cli.sfz_legato_song
  COMMAND elara-cli edit --out "${_legato_song}" "add-track electric-guitar" "append-measures 1"
          "note 0 0 4 0 8" "note 0 0 4 2 2." "rest 0 0 8" "caret 0 0 1 4" "effect legato")
set_tests_properties(cli.sfz_legato_song PROPERTIES FIXTURES_SETUP cli_sfz_legato_song)
add_test(NAME cli.sfz_legato_events COMMAND elara-cli --json sequence "${_legato_song}" --events)
set_tests_properties(cli.sfz_legato_events PROPERTIES
  FIXTURES_REQUIRED cli_sfz_legato_song
  PASS_REGULAR_EXPRESSION "{\"time\":0,\"kind\":\"on\",\"track\":0,\"key\":50,\"velocity\":80},{\"time\":0\\.25,\"kind\":\"legato\",\"track\":0,\"key\":50,\"cents\":200,\"velocity\":80},{\"time\":1\\.75,\"kind\":\"off\"")
add_test(NAME cli.sfz_legato_render
  COMMAND elara-cli render-audio "${_legato_song}" --sfz "${_legato_sfz}" --out "${_legato_wav}")
set_tests_properties(cli.sfz_legato_render PROPERTIES
  FIXTURES_REQUIRED "cli_sfz;cli_sfz_legato_song"
  FIXTURES_SETUP cli_sfz_legato_wav)
add_test(NAME cli.sfz_legato_pitch COMMAND elara-cli --json tune "${_legato_wav}")
set_tests_properties(cli.sfz_legato_pitch PROPERTIES
  FIXTURES_REQUIRED cli_sfz_legato_wav
  PASS_REGULAR_EXPRESSION "\"note\":\"E4\",\"cents\":-?[0-9](\\.[0-9]+)?,")
add_test(NAME cli.sfz_rejects_other_files COMMAND elara-cli sfz "${ELARA_CORPUS}/gp/band.gp")
set_tests_properties(cli.sfz_rejects_other_files PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli.render_audio_sfz
  COMMAND elara-cli --json render-audio "${ELARA_CORPUS}/gp/band.gp" --sfz "${_test_sfz}")
set_tests_properties(cli.render_audio_sfz PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  PASS_REGULAR_EXPRESSION "\"soundFont\":null,\"sfz\":\\[{\"file\":\"[^\"]*test-guitar\\.sfz\",\"tracks\":\"pitched\"}\\],\"sampleRate\":48000,.*\"peak\":0\\.[0-9]*[1-9][0-9]*,.*\"tracks\":\\[{\"track\":0,\"name\":\"Bass\",\"timbre\":\"bass\",\"sound\":\"test-guitar\"},{\"track\":1,\"name\":\"Piano\",\"timbre\":\"piano\",\"sound\":\"test-guitar\"},{\"track\":2,\"name\":\"Drums\",\"timbre\":\"drums\",\"sound\":\"drums\"},{\"track\":3,\"name\":\"Clarinet\",\"timbre\":\"wind\",\"sound\":\"test-guitar\"}\\]")
add_test(NAME cli.render_audio_sfz_and_soundfont
  COMMAND elara-cli --json render-audio "${ELARA_CORPUS}/gp/band.gp" --sfz "${_test_sfz}"
          --soundfont "${_test_font}")
set_tests_properties(cli.render_audio_sfz_and_soundfont PROPERTIES
  FIXTURES_REQUIRED "cli_sfz;cli_soundfont"
  PASS_REGULAR_EXPRESSION "{\"track\":2,\"name\":\"Drums\",\"timbre\":\"drums\",\"sound\":\"Test Kit\"}")
# A song of one D3 played on the test guitar sounds a D3: the sample of A2,
# read at 22050 Hz, is moved up five semitones and played at 48000 Hz.
set(_sfz_song "${CMAKE_CURRENT_BINARY_DIR}/sfz-d3.elara")
set(_sfz_song_wav "${CMAKE_CURRENT_BINARY_DIR}/sfz-d3.wav")
add_test(NAME cli.sfz_song
  COMMAND elara-cli edit --out "${_sfz_song}" "add-track electric-guitar" "append-measures 1"
          "note 0 0 4 0 1")
set_tests_properties(cli.sfz_song PROPERTIES FIXTURES_SETUP cli_sfz_song)
add_test(NAME cli.sfz_render_song
  COMMAND elara-cli render-audio "${_sfz_song}" --sfz "${_test_sfz}" --out "${_sfz_song_wav}")
set_tests_properties(cli.sfz_render_song PROPERTIES
  FIXTURES_REQUIRED "cli_sfz;cli_sfz_song"
  FIXTURES_SETUP cli_sfz_song_wav)
add_test(NAME cli.sfz_song_pitch COMMAND elara-cli --json tune "${_sfz_song_wav}")
set_tests_properties(cli.sfz_song_pitch PROPERTIES
  FIXTURES_REQUIRED cli_sfz_song_wav
  PASS_REGULAR_EXPRESSION "\"note\":\"D3\",\"cents\":-?0(\\.[0-9]+)?,")
# Two takes of each note, one picked at random per note (lorand, hirand): the
# D3 plays one of them, as loud as the library of one sample (both would
# reach about 0.53).
set(_takes_sfz "${CMAKE_CURRENT_BINARY_DIR}/test-sfz/test-takes.sfz")
add_test(NAME cli.sfz_takes COMMAND elara-cli --json sfz "${_takes_sfz}")
set_tests_properties(cli.sfz_takes PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  PASS_REGULAR_EXPRESSION "\"name\":\"test-takes\",\"regions\":2,.*\"unknownOpcodes\":\\[\\],")
add_test(NAME cli.render_audio_sfz_takes
  COMMAND elara-cli --json render-audio "${_sfz_song}" --sfz "${_takes_sfz}")
set_tests_properties(cli.render_audio_sfz_takes PROPERTIES
  FIXTURES_REQUIRED "cli_sfz;cli_sfz_song"
  PASS_REGULAR_EXPRESSION "\"peak\":0\\.2[0-9]*,")
add_test(NAME cli.render_audio_sfz_basses
  COMMAND elara-cli --json render-audio "${ELARA_CORPUS}/gp/band.gp" --sfz "basses=${_test_sfz}")
set_tests_properties(cli.render_audio_sfz_basses PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  PASS_REGULAR_EXPRESSION "\"sfz\":\\[{\"file\":\"[^\"]*test-guitar\\.sfz\",\"tracks\":\"basses\"}\\],.*\"tracks\":\\[{\"track\":0,\"name\":\"Bass\",\"timbre\":\"bass\",\"sound\":\"test-guitar\"},{\"track\":1,\"name\":\"Piano\",\"timbre\":\"piano\",\"sound\":\"piano\"},{\"track\":2,\"name\":\"Drums\",\"timbre\":\"drums\",\"sound\":\"drums\"},{\"track\":3,\"name\":\"Clarinet\",\"timbre\":\"wind\",\"sound\":\"wind\"}\\]")
# A later instrument takes the tracks it is for from an earlier one.
add_test(NAME cli.render_audio_sfz_layers
  COMMAND elara-cli --json render-audio "${ELARA_CORPUS}/gp/band.gp" --soundfont "${_test_font}"
          --sfz "pitched=${_test_sfz}" --sfz "basses=${_test_sfz}")
set_tests_properties(cli.render_audio_sfz_layers PROPERTIES
  FIXTURES_REQUIRED "cli_sfz;cli_soundfont"
  PASS_REGULAR_EXPRESSION "\"tracks\":\"pitched\"},{\"file\":\"[^\"]*test-guitar\\.sfz\",\"tracks\":\"basses\"}\\],.*\"sound\":\"test-guitar\"},.*\"name\":\"Drums\",\"timbre\":\"drums\",\"sound\":\"Test Kit\"")
add_test(NAME cli.render_audio_rejects_unknown_sfz_tracks
  COMMAND elara-cli render-audio "${ELARA_CORPUS}/gp/band.gp" --sfz "drums=${_test_sfz}")
set_tests_properties(cli.render_audio_rejects_unknown_sfz_tracks PROPERTIES
  FIXTURES_REQUIRED cli_sfz
  WILL_FAIL TRUE)
add_test(NAME cli.render_audio_rejects_missing_sfz
  COMMAND elara-cli render-audio "${ELARA_CORPUS}/gp/band.gp" --sfz missing.sfz)
set_tests_properties(cli.render_audio_rejects_missing_sfz PROPERTIES WILL_FAIL TRUE)
# Playing needs an output device, which CI does not have; a missing file fails
# before the device is opened.
add_test(NAME cli.play_rejects_missing_file
  COMMAND elara-cli play "${ELARA_CORPUS}/missing.gp")
set_tests_properties(cli.play_rejects_missing_file PROPERTIES WILL_FAIL TRUE)
# An output device chosen by type and name, or by name alone (ADR-0122), must
# be there: a device no system has fails, naming it, before anything plays.
add_test(NAME cli.play_rejects_unknown_device
  COMMAND elara-cli play "${ELARA_CORPUS}/gp/band.gp" --device "Nowhere / No such device")
set_tests_properties(cli.play_rejects_unknown_device PROPERTIES
  PASS_REGULAR_EXPRESSION "error: no output device \"Nowhere / No such device\"")
add_test(NAME cli.tone_rejects_unknown_device
  COMMAND elara-cli tone --seconds 0.1 --device "No such device")
set_tests_properties(cli.tone_rejects_unknown_device PROPERTIES
  PASS_REGULAR_EXPRESSION "error: no output device \"No such device\"")
# Count-in and loop (ADR-0024): the count-in is a measure of beats where
# playback starts (melody.musicxml ends in 3/4 at quarter = 100; band.gp is
# 4/4 at quarter = 90, here at half speed). A loop must start before it ends,
# which play checks before it opens a device.
add_test(NAME cli.sequence_count_in
  COMMAND elara-cli --json sequence "${ELARA_CORPUS}/musicxml/melody.musicxml" --count-in-at 7)
set_tests_properties(cli.sequence_count_in PROPERTIES
  PASS_REGULAR_EXPRESSION "\"metronome\":false,\"countIn\":{\"clicks\":3,\"interval\":0\.6},\"measures\":")
add_test(NAME cli.sequence_count_in_text
  COMMAND elara-cli sequence "${ELARA_CORPUS}/gp/band.gp" --count-in-at 1 --speed 50)
set_tests_properties(cli.sequence_count_in_text PROPERTIES
  PASS_REGULAR_EXPRESSION "at 50 % speed\ncount-in at 1\.000 s: 4 clicks 1\.333 s apart\n")
add_test(NAME cli.play_rejects_reversed_loop
  COMMAND elara-cli play "${ELARA_CORPUS}/musicxml/melody.musicxml" --loop 2 1)
set_tests_properties(cli.play_rejects_reversed_loop PROPERTIES
  PASS_REGULAR_EXPRESSION "error: --loop needs its start before its end")

# The playback cursor: guitar-techniques.gp plays measures 1 2 1 2 1 3, about
# 2.5 s each, so 6 s is in the third measure played, the first one written.
add_test(NAME cli.render_cursor_in_a_repeat
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --tracks all
          --layout page --cursor-at 6)
set_tests_properties(cli.render_cursor_in_a_repeat PROPERTIES
  PASS_REGULAR_EXPRESSION "\"cursor\":{\"seconds\":6,\"played\":2,\"measure\":0,\"page\":0,\"x\":[0-9.]+,\"y\":[0-9.]+,\"height\":4},\"pages\":\\[")
add_test(NAME cli.render_cursor_after_the_end
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --cursor-at 99)
set_tests_properties(cli.render_cursor_after_the_end PROPERTIES
  PASS_REGULAR_EXPRESSION "cursor at 99 s: after the end")

# A click where the cursor stood at 6 s (the second time through measure 1)
# starts playback on the same column the first time through: at 1 s.
add_test(NAME cli.render_time_at_a_note
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --tracks all
          --layout page --time-at 0 29.9 45.7)
set_tests_properties(cli.render_time_at_a_note PROPERTIES
  PASS_REGULAR_EXPRESSION "\"timeAt\":{\"page\":0,\"x\":29.9,\"y\":45.7,\"measure\":0,\"seconds\":1},\"pages\":\\[")
# The symbol drawn at a point of a page, of those that are edited where they
# are drawn (ADR-0130): the 3/4 that starts the third measure, with its
# measure, the box round its two numbers and its staff; a key signature by its
# name; and nothing in the page's corner.
add_test(NAME cli.render_symbol_at_a_time_signature
  COMMAND elara-cli --json render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --tracks all
          --layout page --symbol-at 0 54.6 45.5)
set_tests_properties(cli.render_symbol_at_a_time_signature PROPERTIES
  PASS_REGULAR_EXPRESSION "\"symbolAt\":{\"page\":0,\"x\":54.6,\"y\":45.5,\"role\":\"timeSignature\",\"measure\":2,\"box\":\\[[0-9.,]+\\],\"staff\":{\"track\":0,\"staff\":0}},\"pages\":\\[")
add_test(NAME cli.render_symbol_at_a_key_signature
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --tracks all
          --layout page --symbol-at 0 13.2 44)
set_tests_properties(cli.render_symbol_at_a_key_signature PROPERTIES
  PASS_REGULAR_EXPRESSION "symbol at page 1, x 13.2, y 44: keySignature of measure 1")
add_test(NAME cli.render_symbol_at_nothing
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --tracks all
          --layout page --symbol-at 0 1 1)
set_tests_properties(cli.render_symbol_at_nothing PROPERTIES
  PASS_REGULAR_EXPRESSION "symbol at page 1, x 1, y 1: none there")
add_test(NAME cli.render_time_at_nothing
  COMMAND elara-cli render "${ELARA_CORPUS}/gp/guitar-techniques.gp" --time-at 0 1 1)
set_tests_properties(cli.render_time_at_nothing PROPERTIES
  PASS_REGULAR_EXPRESSION "time at page 1, x 1, y 1: no measure there")
