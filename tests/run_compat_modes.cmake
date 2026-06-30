set(TEXT_FILE "${TDIR}/compat_replace.txt")
set(BINARY_FILE "${TDIR}/compat_clear.bin")

file(WRITE "${TEXT_FILE}" "foo one foo two\n")
execute_process(
  COMMAND "${STRCLEAR}" -r "${TEXT_FILE}" "foo" "bar"
  RESULT_VARIABLE replace_result
)
if(NOT replace_result EQUAL 0)
  message(FATAL_ERROR "Compatibility -r mode failed: ${replace_result}")
endif()

file(READ "${TEXT_FILE}" text_content)
if(NOT "${text_content}" STREQUAL "bar one bar two\n")
  message(FATAL_ERROR "Compatibility -r mode produced unexpected text:\n${text_content}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BINARY_SRC}" "${BINARY_FILE}")
execute_process(
  COMMAND "${STRCLEAR}" -b -c "${BINARY_FILE}" "foo" "/path/not/present"
  RESULT_VARIABLE clear_result
)
if(NOT clear_result EQUAL 0)
  message(FATAL_ERROR "Compatibility -b -c mode failed: ${clear_result}")
endif()

execute_process(
  COMMAND "${CHARCNT}" "${BINARY_FILE}"
  RESULT_VARIABLE null_count
)
if(NOT null_count EQUAL 6)
  message(FATAL_ERROR "Compatibility -b -c mode expected 6 NUL chars, found ${null_count}")
endif()
