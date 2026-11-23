# elara_fetch_file(<url> <file> <sha256> <what>)
#
# Downloads <url> to <file> unless <file> already has the SHA-256 <sha256>.
# A CI runner now and then gets a transient HTTP error from the file host, so
# a failed or corrupt download is retried a few times before configure stops.
# The hash is checked here rather than with EXPECTED_HASH, which turns any
# failed transfer into an error that no retry can undo.

set(ELARA_FETCH_ATTEMPTS 4 CACHE STRING "How many times a third-party file download is tried")
set(ELARA_FETCH_RETRY_DELAY 5 CACHE STRING "Seconds to wait before trying a download again")

function(elara_fetch_file url file sha256 what)
  if(EXISTS "${file}")
    file(SHA256 "${file}" _have)
    if(_have STREQUAL sha256)
      return()
    endif()
  endif()
  message(STATUS "Downloading ${what}")
  set(_reason "")
  foreach(_attempt RANGE 1 ${ELARA_FETCH_ATTEMPTS})
    if(_attempt GREATER 1)
      message(STATUS "Downloading ${what} again (attempt ${_attempt}): ${_reason}")
      execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep ${ELARA_FETCH_RETRY_DELAY})
    endif()
    file(DOWNLOAD "${url}" "${file}" TLS_VERIFY ON INACTIVITY_TIMEOUT 60 STATUS _status)
    list(GET _status 0 _code)
    if(_code EQUAL 0)
      file(SHA256 "${file}" _have)
      if(_have STREQUAL sha256)
        return()
      endif()
      set(_reason "SHA-256 ${_have}, expected ${sha256}")
    else()
      list(GET _status 1 _reason)
    endif()
    file(REMOVE "${file}")
  endforeach()
  message(FATAL_ERROR "Could not download ${what} from ${url}: ${_reason}")
endfunction()
