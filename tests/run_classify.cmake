set(TEXT_FILE "${TDIR}/classify_text.txt")
set(BINARY_FILE "${TDIR}/classify_binary.bin")
set(FILE_LIST "${TDIR}/classify_files.txt")

file(WRITE "${TEXT_FILE}" "plain text\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BINARY_SRC}" "${BINARY_FILE}")
file(WRITE "${FILE_LIST}" "${TEXT_FILE}\n${BINARY_FILE}\n")

execute_process(
  COMMAND "${STRCLEAR}" -B "${TEXT_FILE}"
  RESULT_VARIABLE text_result
)
if(NOT text_result EQUAL 1)
  message(FATAL_ERROR "-B should return 1 for text files, got ${text_result}")
endif()

execute_process(
  COMMAND "${STRCLEAR}" -B "${BINARY_FILE}"
  RESULT_VARIABLE binary_result
)
if(NOT binary_result EQUAL 0)
  message(FATAL_ERROR "-B should return 0 for binary files, got ${binary_result}")
endif()

execute_process(
  COMMAND "${STRCLEAR}" --classify --files "${FILE_LIST}"
  RESULT_VARIABLE classify_result
  OUTPUT_VARIABLE classify_output
)
if(NOT classify_result EQUAL 0)
  message(FATAL_ERROR "--classify failed: ${classify_result}")
endif()

string(FIND "${classify_output}" "\"type\":\"TEXT\"" text_type)
string(FIND "${classify_output}" "\"path\":\"${TEXT_FILE}\"" text_path)
string(FIND "${classify_output}" "\"type\":\"BINARY\"" binary_type)
string(FIND "${classify_output}" "\"path\":\"${BINARY_FILE}\"" binary_path)
if(text_type EQUAL -1 OR text_path EQUAL -1 OR binary_type EQUAL -1 OR binary_path EQUAL -1)
  message(FATAL_ERROR "Unexpected --classify output:\n${classify_output}")
endif()
