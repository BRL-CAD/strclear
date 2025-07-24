# Reset input files
set(TF "${TDIR}/${TTXTFILE}${TNUM}")
set(BF "${TDIR}/${TBINFILE}${TNUM}")
foreach (i RANGE ${CPYCNT})
  execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TDIR}/${TTXTFILE}.in" "${TF}_${i}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TDIR}/${TBINFILE}.in" "${BF}_${i}")
endforeach()

# report original file contents
if (NOT QUIET_RUN)
  file(READ "${TF}" orig_file_content)
  message("Original text file contents:\n\n${orig_file_content}\n")
  execute_process(
    COMMAND "${CHARCNT}" "${BF}"
    RESULT_VARIABLE count
  )
  message("Original binary null char count: ${count}")
endif()

# assemble file list to pass to strclear
set(flist)
file(REMOVE "${TDIR}/${TNUM}.txt")
foreach (i RANGE ${CPYCNT})
  file(APPEND ${TDIR}/${TNUM}.txt "${TF}_${i}\n")
  file(APPEND ${TDIR}/${TNUM}.txt "${BF}_${i}\n")
endforeach()

# Run strclear
message("WORKING_DIRECTORY ${TDIR}")
message("${STRCLEAR} ${STRCLEAR_OPTS} --files ${TDIR}/${TNUM}.txt ${TGTSTR} ${EXPSTR}")
execute_process(
  COMMAND "${STRCLEAR}" ${STRCLEAR_OPTS} --files ${TDIR}/${TNUM}.txt "${TGTSTR}" ${EXPSTR}
  WORKING_DIRECTORY ${TDIR}
  )

# Check file contents
file(STRINGS ${TDIR}/${TNUM}.txt ilist)
foreach (f ${ilist})
  if (NOT EXISTS ${f})
    message(FATAL_ERROR "file ${f} not found")
  endif()
  if ("${f}" MATCHES ".*bin.*")
    execute_process(
      COMMAND ${CHARCNT} ${f}
      RESULT_VARIABLE bcount
      OUTPUT_VARIABLE bout
    )
    if(NOT ${bcount} EQUAL ${EXPNULLCNT})
      message(FATAL_ERROR "File ${f} does not contain ${EXPNULLCNT} null chars - found ${bcount}")
    endif()
  else()
    file(READ ${f} file_content)
    # Match all occurrences of CLEARED and store them in a list
    string(REPLACE "\n" "" file_content "${file_content}")
    string(REGEX MATCHALL "${EXPSTR}" MATCHES "${file_content}")
    list(LENGTH MATCHES tcount)

    if(NOT ${tcount} EQUAL ${EXPCNT})
      message(FATAL_ERROR "File ${f} does not contain expected text: ${EXPCNT}x'${EXPSTR}'\nActual content:\n${file_content}")
    endif()
  endif()
endforeach()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
