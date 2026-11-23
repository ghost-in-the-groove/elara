# Strict warnings for Elara's own code.
#
# Third-party code (JUCE module sources compiled into elara_audio_juce, Qt's
# generated moc/qmlcache files) must not be held to these flags, so there are
# two entry points:
#   elara_target_warnings(<target>)      - every source of a target we own
#   elara_source_warnings(<file>...)     - selected files of a mixed target

function(_elara_warning_flags out_var)
  if(MSVC)
    # /w15038: members initialized out of order, which GCC and Clang report too.
    set(flags /W4 /permissive- /w14265 /w14296 /w14826 /w14905 /w14906 /w14928 /w15038)
    if(ELARA_WARNINGS_AS_ERRORS)
      list(APPEND flags /WX)
    endif()
  else()
    set(flags
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
      -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough)
    if(ELARA_WARNINGS_AS_ERRORS)
      list(APPEND flags -Werror)
    endif()
  endif()
  set(${out_var} ${flags} PARENT_SCOPE)
endfunction()

function(elara_target_warnings target)
  _elara_warning_flags(flags)
  target_compile_options(${target} PRIVATE ${flags})
endfunction()

function(elara_source_warnings)
  _elara_warning_flags(flags)
  set_source_files_properties(${ARGN}
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTIES COMPILE_OPTIONS "${flags}")
endfunction()
