# Every string of the UI has a translation. Refreshes a copy of each .ts file
# from the app's sources, as `python scripts/elara.py translations` does, and
# fails when:
#   - the copy differs from the committed file: a string was added, changed or
#     removed in the sources without refreshing the translations;
#   - a translation is unfinished or empty.
#
# The source language's file holds only the plural forms ("%n measure(s)").
#
# Usage: cmake -DLUPDATE=<exe> -DLCONVERT=<exe> -DAPP_DIR=<src/app>
#              -DSOURCE_LANGUAGE=en -DWORK_DIR=<dir> -P check_translations.cmake
file(GLOB ts_files "${APP_DIR}/i18n/*.ts")
if(NOT ts_files)
  message(FATAL_ERROR "no .ts files in ${APP_DIR}/i18n")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(problems "")
foreach(ts IN LISTS ts_files)
  get_filename_component(name "${ts}" NAME)
  set(copy "${WORK_DIR}/${name}")
  file(COPY_FILE "${ts}" "${copy}")
  execute_process(
    COMMAND "${LUPDATE}" "${APP_DIR}/qml" "${APP_DIR}/src" -ts "${copy}"
      -no-obsolete -locations none -silent
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "lupdate failed on ${name}:\n${output}")
  endif()
  if(name MATCHES "_${SOURCE_LANGUAGE}\\.ts$")
    execute_process(
      COMMAND "${LCONVERT}" -pluralonly -i "${copy}" -o "${copy}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "lconvert failed on ${name}:\n${output}")
    endif()
  endif()

  file(READ "${ts}" committed)
  file(READ "${copy}" refreshed)
  string(REPLACE "\r\n" "\n" committed "${committed}")
  string(REPLACE "\r\n" "\n" refreshed "${refreshed}")
  if(NOT committed STREQUAL refreshed)
    list(APPEND problems
      "${name}: out of date with the sources; run `python scripts/elara.py translations`")
  endif()

  string(REGEX MATCHALL "<source>[^<]*</source>[ \n]*<translation type=\"unfinished\""
    unfinished "${refreshed}")
  string(REGEX MATCHALL "<source>[^<]*</source>[ \n]*<translation></translation>"
    empty "${refreshed}")
  string(REGEX MATCHALL "<numerusform></numerusform>" emptyForms "${refreshed}")
  foreach(match IN LISTS unfinished empty)
    string(REGEX REPLACE "^<source>([^<]*)</source>.*" "\\1" source "${match}")
    list(APPEND problems "${name}: not translated: ${source}")
  endforeach()
  if(emptyForms)
    list(APPEND problems "${name}: a plural form is empty")
  endif()
endforeach()

if(problems)
  list(JOIN problems "\n  " report)
  message(FATAL_ERROR "translations are incomplete:\n  ${report}")
endif()
list(LENGTH ts_files count)
message(STATUS "translations: ${count} files complete")
