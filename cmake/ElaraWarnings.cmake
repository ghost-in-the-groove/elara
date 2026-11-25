# Strict warnings for Elara's own code, and the sanitizers ELARA_SANITIZE lists
# (ADR-0105).
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
      if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC finds these only when optimising (never in the dev preset), from
        # an analysis that also flags paths inside libstdc++ and loops it
        # cannot prove run: shown, but not fatal to a release build.
        list(APPEND flags -Wno-error=null-dereference -Wno-error=array-bounds
          -Wno-error=stringop-overflow -Wno-error=stringop-overread
          -Wno-error=maybe-uninitialized)
      endif()
    endif()
  endif()
  set(${out_var} ${flags} PARENT_SCOPE)
endfunction()

# Sanitizers for Elara's code only: JUCE's sources and the vcpkg packages stay
# as they are. _GLIBCXX_ASSERTIONS adds bounds checks to libstdc++'s containers.
function(_elara_sanitizer_flags out_var)
  set(flags)
  if(ELARA_SANITIZE)
    set(flags -fsanitize=${ELARA_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer
      -D_GLIBCXX_ASSERTIONS)
  endif()
  set(${out_var} ${flags} PARENT_SCOPE)
endfunction()

function(elara_target_warnings target)
  _elara_warning_flags(flags)
  _elara_sanitizer_flags(sanitizers)
  target_compile_options(${target} PRIVATE ${flags} ${sanitizers})
  if(ELARA_SANITIZE)
    # The sanitizer runtime goes to every program that links the target.
    target_link_options(${target} PUBLIC -fsanitize=${ELARA_SANITIZE})
  endif()
endfunction()

function(elara_source_warnings)
  _elara_warning_flags(flags)
  _elara_sanitizer_flags(sanitizers)
  list(APPEND flags ${sanitizers})
  set_source_files_properties(${ARGN}
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTIES COMPILE_OPTIONS "${flags}")
endfunction()
