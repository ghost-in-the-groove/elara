# Usage: cmake -DFILE=<path> -DEXPECTED_SIZE=<bytes> -P check_file_size.cmake
if(NOT EXISTS "${FILE}")
  message(FATAL_ERROR "missing file: ${FILE}")
endif()
file(SIZE "${FILE}" actual)
if(NOT actual EQUAL EXPECTED_SIZE)
  message(FATAL_ERROR "${FILE}: expected ${EXPECTED_SIZE} bytes, got ${actual}")
endif()
message(STATUS "${FILE}: ${actual} bytes")
