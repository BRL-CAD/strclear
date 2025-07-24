# Reset input file
set(TF "${TDIR}/${TFILE}${TNUM}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TDIR}/${TFILE}.in" "${TF}")

# report original file contents
if (NOT QUIET_RUN)
  file(READ "${TF}" orig_file_content)
  message("Original file contents:\n\n${orig_file_content}\n")
endif()

# Run strclear
message("WORKING_DIRECTORY ${TDIR}")
message("${STRCLEAR} ${STRCLEAR_OPTS} ${TF} ${TGTSTR} ${EXPSTR}")
execute_process(
  COMMAND "${STRCLEAR}" ${STRCLEAR_OPTS} "${TF}" "${TGTSTR}" ${EXPSTR}
  WORKING_DIRECTORY ${TDIR}
  )

# Check output file contents
file(READ "${TF}" file_content)

# If we were assigned a charcnt executable, look for NULL characters.
# Otherwise, count EXPSTR instances
if (CHARCNT)
  message("${CHARCNT} ${TF} ${EXPCHAR}")
  execute_process(
    COMMAND "${CHARCNT}" "${TF}" ${EXPCHAR}
    RESULT_VARIABLE count
  )
  if (count LESS 0)
    message(FATAL_ERROR "charcnt unable to run successfully\n")
  endif()
else()
  # Match all occurrences of CLEARED and store them in a list
  string(REPLACE "\n" "" file_content "${file_content}")
  string(REGEX MATCHALL "${EXPSTR}" MATCHES "${file_content}")
  list(LENGTH MATCHES count)
endif()

file(READ "${TF}" file_content)
if(NOT ${count} EQUAL ${EXPCNT})
  if (CHARCNT)
    set(nstr)
    if (NOT EXPCHAR)
      set(nstr "null")
    else()
      set(nstr "${EXPCHAR}")
    endif()
    message(FATAL_ERROR "File ${TF} does not contain ${EXPCNT} null chars - found ${count}")
  else()
    message(FATAL_ERROR "File ${TF} does not contain expected text: ${EXPCNT}x'${EXPSTR}'\nActual content:\n${file_content}")
  endif()
else()
  if (NOT QUIET_RUN)
    message("Final file contents:\n\n${file_content}\n")
  endif()
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
