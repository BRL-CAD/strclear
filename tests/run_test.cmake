# Reset input file
set(TF "${TDIR}/${TFILE}${TNUM}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TDIR}/${TFILE}.in" "${TF}")

# Run strclear
message("WORKING_DIRECTORY ${TDIR}")
message("${STRCLEAR} ${STRCLEAR_OPTS} ${TF} ${TGTSTR} ${EXPSTR}")
execute_process(
  COMMAND "${STRCLEAR}" ${STRCLEAR_OPTS} "${TF}" "${TGTSTR}" ${EXPSTR}
  WORKING_DIRECTORY ${TDIR}
  )

# Check output file contents
file(READ "${TF}" file_content)

# Match all occurrences of CLEARED and store them in a list
string(REPLACE "\n" "" file_content "${file_content}")
string(REGEX MATCHALL "${EXPSTR}" MATCHES "${file_content}")
list(LENGTH MATCHES count)

if(NOT ${count} EQUAL ${EXPCNT})
  file(READ "${TF}" file_content)
  message(FATAL_ERROR "File ${TF} does not contain expected text: ${EXPCNT}x'${EXPSTR}'\nActual content:\n${file_content}")
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
