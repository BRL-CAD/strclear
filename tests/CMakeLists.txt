###################################################
#                strclear tests
###################################################

# Directory for test artifacts
set(TESTDIR "${CMAKE_CURRENT_BINARY_DIR}/testfiles")
file(MAKE_DIRECTORY "${TESTDIR}")

# Helper function to compare file contents
function(assert_file_contains file expected_text)
  file(READ "${file}" file_content)
  string(FIND "${file_content}" "${expected_text}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "File ${file} does not contain expected text: '${expected_text}'\nActual content:\n${file_content}")
  endif()
endfunction()

# Helper function to assert file does NOT contain text
function(assert_file_not_contains file unexpected_text)
  file(READ "${file}" file_content)
  string(FIND "${file_content}" "${unexpected_text}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "File ${file} still contains text: '${unexpected_text}'\nActual content:\n${file_content}")
  endif()
endfunction()

# --- Test 1: Non-filesystem string replace ---
set(TEXT1 "This is a test string foo and bar.\nAnother foo here.")
set(FILE1 "${TESTDIR}/plain.txt")
file(WRITE "${FILE1}" "${TEXT1}")

add_test(NAME BasicStringReplace
  COMMAND strclear -v "${FILE1}" foo baz
)
set_tests_properties(BasicStringReplace PROPERTIES
  FIXTURES_SETUP basic_replace
)

add_test(NAME VerifyBasicStringReplace
  COMMAND ${CMAKE_COMMAND} -Dfile=${FILE1} -Dexpected=baz -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyBasicStringReplace PROPERTIES
  FIXTURES_REQUIRED basic_replace
)

# --- Test 2: Path expansion (filesystem) ---
set(REL_PATH "relpath.txt")
set(ABS_PATH "${TESTDIR}/abspath.txt")
set(FILE2 "${TESTDIR}/withpaths.txt")

get_filename_component(ABS_PATH_FOR_FILE "${ABS_PATH}" ABSOLUTE)
set(TEXT2 "Here is a path: ${REL_PATH}\nAnd here is another: ${ABS_PATH_FOR_FILE}\n")
file(WRITE "${FILE2}" "${TEXT2}")

file(WRITE "${TESTDIR}/${REL_PATH}" "dummy")
file(WRITE "${ABS_PATH}" "dummy")

add_test(NAME PathExpansionReplace
  COMMAND strclear -v -p "${FILE2}" "${REL_PATH}" CLEARED
)
set_tests_properties(PathExpansionReplace PROPERTIES
  FIXTURES_SETUP path_replace
)

add_test(NAME VerifyPathExpansionReplace
  COMMAND ${CMAKE_COMMAND} -Dfile=${FILE2} -Dexpected=CLEARED -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyPathExpansionReplace PROPERTIES
  FIXTURES_REQUIRED path_replace
)

# --- Test 3: Symlink path expansion ---
set(SRC_FILE "${TESTDIR}/target.txt")
set(SYM_PATH "${TESTDIR}/symlink.txt")
set(FILE3 "${TESTDIR}/withsymlink.txt")
set(TEXT3 "Original: ${SRC_FILE}\nSymlink: ${SYM_PATH}\n")
file(WRITE "${FILE3}" "${TEXT3}")
file(WRITE "${SRC_FILE}" "dummy")

file(CREATE_LINK "${SRC_FILE}" "${SYM_PATH}" SYMBOLIC)

add_test(NAME SymlinkPathExpansionReplace
  COMMAND strclear -v -p "${FILE3}" "${SYM_PATH}" "CLEAN"
)
set_tests_properties(SymlinkPathExpansionReplace PROPERTIES
  FIXTURES_SETUP symlink_replace
)

add_test(NAME VerifySymlinkPathExpansionReplace
  COMMAND ${CMAKE_COMMAND} -Dfile=${FILE3} -Dexpected=CLEAN -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifySymlinkPathExpansionReplace PROPERTIES
  FIXTURES_REQUIRED symlink_replace
)

# --- Test 4: No Replacement Occurs ---
set(FILE_NOMATCH "${TESTDIR}/no_match.txt")
set(TEXT_NOMATCH "Just a normal text file.")
file(WRITE "${FILE_NOMATCH}" "${TEXT_NOMATCH}")

add_test(NAME NoReplacementIfNoMatch
  COMMAND strclear -v "${FILE_NOMATCH}" "foo" "bar"
)
set_tests_properties(NoReplacementIfNoMatch PROPERTIES
  FIXTURES_SETUP no_replace
)

add_test(NAME VerifyNoReplacementIfNoMatch
  COMMAND ${CMAKE_COMMAND} -Dfile=${FILE_NOMATCH} -Dexpected=${TEXT_NOMATCH} -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyNoReplacementIfNoMatch PROPERTIES
  FIXTURES_REQUIRED no_replace
)

# --- Test 5: Multiple Occurrences ---
set(FILE_MULTI "${TESTDIR}/multi.txt")
set(TEXT_MULTI "foo bar foo baz foo")
file(WRITE "${FILE_MULTI}" "${TEXT_MULTI}")

add_test(NAME MultipleReplacements
  COMMAND strclear -v "${FILE_MULTI}" foo CLEARED
)
set_tests_properties(MultipleReplacements PROPERTIES
  FIXTURES_SETUP multi_replace
)

add_test(NAME VerifyMultipleReplacements
  COMMAND ${CMAKE_COMMAND} -Dfile=${FILE_MULTI} -Dexpected=CLEARED -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyMultipleReplacements PROPERTIES
  FIXTURES_REQUIRED multi_replace
)

# --- Test 6: Empty Search String (should fail) ---
set(FILE_EMPTYSEARCH "${TESTDIR}/emptysearch.txt")
file(WRITE "${FILE_EMPTYSEARCH}" "some text")

add_test(NAME EmptySearchString
  COMMAND strclear -v "${FILE_EMPTYSEARCH}" "" replacement
)
set_tests_properties(EmptySearchString PROPERTIES
  WILL_FAIL TRUE
)

# --- Test 7: File with Only Search String ---
set(FILE_ONLYSEARCH "${TESTDIR}/onlysearch.txt")
file(WRITE "${FILE_ONLYSEARCH}" "foo")

add_test(NAME OnlySearchString
  COMMAND strclear -v "${FILE_ONLYSEARCH}" foo gone
)
set_tests_properties(OnlySearchString PROPERTIES
  FIXTURES_SETUP onlysearch_replace
)

add_test(NAME VerifyOnlySearchString
  COMMAND ${CMAKE_COMMAND} -Dfile=${FILE_ONLYSEARCH} -Dexpected=gone -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyOnlySearchString PROPERTIES
  FIXTURES_REQUIRED onlysearch_replace
)

# --- Test 8: Large File Replacement ---
set(FILE_LARGE "${TESTDIR}/large.txt")
set(SEARCH_LARGE "foo")
set(REPLACE_LARGE "bar")
file(WRITE "${FILE_LARGE}" "")
foreach(i RANGE 1 1000)
  file(APPEND "${FILE_LARGE}" "foo this is line ${i}\n")
endforeach()

add_test(NAME LargeFileReplacement
  COMMAND strclear -v "${FILE_LARGE}" foo bar
)
set_tests_properties(LargeFileReplacement PROPERTIES
  FIXTURES_SETUP large_replace
)

add_test(NAME VerifyLargeFileReplacement
  COMMAND ${CMAKE_COMMAND} -Dfile=${FILE_LARGE} -Dexpected=bar -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyLargeFileReplacement PROPERTIES
  FIXTURES_REQUIRED large_replace
)

# --- Test 9: Unicode/UTF-8 Replacement ---
#     Disabled for now - we don't support Unicode yet
#set(FILE_UTF8 "${TESTDIR}/utf8.txt")
## Note: 💧 is a multi-byte character; CMake will encode as UTF-8
#file(WRITE "${FILE_UTF8}" "αβγ foo δει foo ζ")
#add_test(NAME UnicodeReplacement
#    COMMAND strclear -v "${FILE_UTF8}" foo 💧
#)
#set_tests_properties(UnicodeReplacement PROPERTIES
#    FIXTURES_SETUP unicode_replace
#)
#
#add_test(NAME VerifyUnicodeReplacement
#    COMMAND ${CMAKE_COMMAND} -Dfile=${FILE_UTF8} -Dexpected=💧 -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
#)
#set_tests_properties(VerifyUnicodeReplacement PROPERTIES
#    FIXTURES_REQUIRED unicode_replace
#)

# --- Test 10: Read-only File (should fail) ---
set(FILE_RO "${TESTDIR}/readonly.txt")
file(WRITE "${FILE_RO}" "foo bar")
execute_process(COMMAND chmod a-w "${FILE_RO}")

add_test(NAME ReadOnlyFile
  COMMAND strclear -v "${FILE_RO}" foo bar
)
set_tests_properties(ReadOnlyFile PROPERTIES
  WILL_FAIL TRUE
)

# --- Test 11: Help and Version Output ---
add_test(NAME StrclearHelp
  COMMAND strclear -v --help
)
set_tests_properties(StrclearHelp PROPERTIES
  PASS_REGULAR_EXPRESSION "usage|help|clear"
)

# --- Test 12: Nonexistent File (should fail) ---
set(NONEXISTENT_FILE "${TESTDIR}/does_not_exist.txt")
add_test(NAME NonexistentFile
  COMMAND strclear -v "${NONEXISTENT_FILE}" foo bar
)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
