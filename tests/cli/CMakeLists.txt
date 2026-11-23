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
