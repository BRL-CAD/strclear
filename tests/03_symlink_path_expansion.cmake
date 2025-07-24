# Reset input file
set(TFILE "withsymlink.txt")
set(TF "${TDIR}/${TFILE}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TDIR}/${TFILE}.in" "${TF}")

# Set value to replace with and check
set(expected "CLEARED")

# Run strclear
message("WORKING_DIRECTORY ${TDIR}")
message("${STRCLEAR} -v -p ${TF} ${TGTSTR} ${expected}")
execute_process(
  COMMAND "${STRCLEAR}" -v -p "${TF}" "${TGTSTR}" ${expected}
  WORKING_DIRECTORY ${TDIR}
  )

# Check output file contents
file(READ "${TF}" file_content)

# Match all occurrences of CLEARED and store them in a list
string(REPLACE "\n" "" file_content "${file_content}")
string(REGEX MATCHALL "${expected}" MATCHES "${file_content}")
list(LENGTH MATCHES count)

if(NOT ${count} EQUAL 2)
  file(READ "${TF}" file_content)
  message(FATAL_ERROR "File ${TF} does not contain expected text: 2x'${expected}'\nActual content:\n${file_content}")
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
