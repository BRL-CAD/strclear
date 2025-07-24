###################################################
#                strclear tests
###################################################

# --- Testdata staging requirements ---
# (plain.txt, withpaths.txt, relpath.txt, abspath.txt, target.txt, withsymlink.txt, no_match.txt,
# multi.txt, emptysearch.txt, onlysearch.txt, cclear.bin)
# Place these in testdata/ in the same directory as this CMakeLists.txt

# --- CMake scripts for custom staging ---
# make_large_file.cmake: create ${TESTDIR}/large.txt with lots of "foo"
# stage_mixed_list.cmake: create mixed1.txt, mixed2.txt, mixed3.txt, mixedbin.bin, mixed_files.txt
# stage_parallel_list.cmake: create par1.txt ... par20.txt, parallel_files.txt
# stage_filter_list.cmake: create text1.txt, text2.txt, bin1.bin, bin2.bin, filter_files.txt

# Counting characters in a binary file is a little outside
# CMake's wheelhouse, so add a utility to do it.
add_executable(charcnt charcnt.cpp)

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
# Set up a template to use as input.  Since the contents are altered, the test
# needs to stage it each time - that's why we write it out with the .in suffix.
set(PLAINFILE "plain.txt")
file(WRITE "${TESTDIR}/${PLAINFILE}.in" "This is a test string foo and bar.\nAnother foo here.")
add_test(NAME BasicStringReplace
  COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${PLAINFILE}
  -DTNUM=1 -DTGTSTR=foo -DEXPSTR=CLEARED -DEXPCNT=2 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
)

# --- Test 2: Path expansion (filesystem) ---
# Set up the template for the to-be-altered file.  For this test we want to
# point to a file that is present - we can just target the plain.in file from
# the first test, since that is already placed on the filesystem by CMake.
set(WITHPATHSFILE "withpaths.txt")
get_filename_component(ABS_PATH_FOR_FILE "${TESTDIR}/${PLAINFILE}.in" ABSOLUTE)
set(WITHPATHS_TXT "Relative: ${PLAINFILE}.in\nAbsolute: ${ABS_PATH_FOR_FILE}\n")
file(WRITE "${TESTDIR}/${WITHPATHSFILE}.in" "${WITHPATHS_TXT}")

add_test(NAME PathExpansionReplace
  COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${WITHPATHSFILE}
  -DTNUM=2 -DTGTSTR=${PLAINFILE}.in -DEXPSTR=CLEARED -DEXPCNT=2 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v\;-p
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
)

# --- Test 3: Symlink path expansion ---
# IFF the OS supports making symlinks, we can do this test - find out
set(SYMFILE "symlink.txt")
file(CREATE_LINK "${TESTDIR}/${PLAINFILE}.in" "${TESTDIR}/${SYMFILE}" SYMBOLIC RESULT symlink_ret)
if(symlink_ret EQUAL 0)
  # Set up the template for the to-be-altered file.
  set(WITHSYMFILE "withsymlink.txt")
  set(WITHSYMLINK_TXT "Original: ${TESTDIR}/${PLAINFILE}.in\nSymlink: ${TESTDIR}/${SYMFILE}\n")
  file(WRITE "${TESTDIR}/${WITHSYMFILE}.in" "${WITHSYMLINK_TXT}")

  add_test(NAME SymlinkPathExpansionReplace
    COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${WITHSYMFILE}
    -DTNUM=3 -DTGTSTR=${SYMFILE} -DEXPSTR=CLEARED -DEXPCNT=2 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v\;-p
    -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
  )
endif()

# --- Test 4: No Replacement Occurs ---
set(NOMATCHFILE "no_match.txt")
file(WRITE "${TESTDIR}/${NOMATCHFILE}.in" "Just a normal text file.")
add_test(NAME NoReplacementIfNoMatch
  COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${NOMATCHFILE}
  -DTNUM=4 -DTGTSTR=foo -DEXPSTR=CLEARED -DEXPCNT=0 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
)

# --- Test 5: Many Occurrences ---
set(MULTIFILE "multi.txt")
file(WRITE "${TESTDIR}/${MULTIFILE}.in" "foo bar foo baz ")
foreach(i RANGE 100)
  file(APPEND "${TESTDIR}/${MULTIFILE}.in" "foo ")
endforeach()
add_test(NAME ManyOccurrences
  COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${MULTIFILE}
  -DTNUM=5 -DTGTSTR=foo -DEXPSTR=CLEARED -DEXPCNT=103 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
)

# --- Test 6: Empty Search String (should fail) ---
add_test(NAME EmptySearchString
  COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${NOMATCHFILE}
  -DTNUM=6 -DTGTSTR= -DEXPSTR=CLEARED -DEXPCNT=0 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
)

# --- Test 7: File with Only Search String ---
set(ONLYFILE "onlysearch.txt")
file(WRITE "${TESTDIR}/${ONLYFILE}.in" "foo")
add_test(NAME OnlySearchString
  COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${ONLYFILE}
  -DTNUM=7 -DTGTSTR=foo -DEXPSTR=CLEARED -DEXPCNT=1 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
)

# --- Test 8: Large(ish) File Replacement ---
set(LARGEFILE "large.txt")
file(WRITE "${TESTDIR}/${LARGEFILE}.in" "Autogenerated large file:\n")
foreach(i RANGE 1000)
  file(APPEND "${TESTDIR}/${LARGEFILE}.in" "Line foo: foo${i}foo\n")
endforeach()
add_test(NAME LargeFile
  COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${LARGEFILE}
  -DTNUM=8 -DTGTSTR=foo -DEXPSTR=CLEARED -DEXPCNT=3003 -DQUIET_RUN=1 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
)

# --- Test 9: Binary File Alteration ---
set(BINARYFILE "binary_file.bin")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/${BINARYFILE}" DESTINATION ${TESTDIR})
file(RENAME "${TESTDIR}/${BINARYFILE}" "${TESTDIR}/${BINARYFILE}.in")
add_test(NAME BinaryFile
  COMMAND ${CMAKE_COMMAND} -DSDIR=${CMAKE_CURRENT_SOURCE_DIR}/testdata -DTDIR=${TESTDIR} -DTFILE=${BINARYFILE}
  -DTNUM=9 -DTGTSTR=foo -DEXPSTR=NULL -DEXPCNT=6 -DQUIET_RUN=1 -DSTRCLEAR=$<TARGET_FILE:strclear> -DSTRCLEAR_OPTS=-v
  -DCHARCNT=$<TARGET_FILE:charcnt>
  -P "${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake"
)

if(0)
# --- Test 9: Mixed Input List via --files option ---
add_test(NAME MixedInputListReplace
  COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/stage_mixed_list.cmake"
  COMMAND strclear -v --files "${TESTDIR}/mixed_files.txt" foo REPLACED
)
set_tests_properties(MixedInputListReplace PROPERTIES
  FIXTURES_SETUP mixed_replace
)

add_test(NAME VerifyMixedInputListReplace1
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/mixed1.txt -Dexpected=REPLACED -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyMixedInputListReplace1 PROPERTIES
  FIXTURES_REQUIRED mixed_replace
)
add_test(NAME VerifyMixedInputListReplace2
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/mixed2.txt -Dexpected=REPLACED -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyMixedInputListReplace2 PROPERTIES
  FIXTURES_REQUIRED mixed_replace
)
add_test(NAME VerifyMixedInputListReplace3
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/mixed3.txt -Dexpected=REPLACED -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyMixedInputListReplace3 PROPERTIES
  FIXTURES_REQUIRED mixed_replace
)
add_test(NAME VerifyMixedInputListReplaceBin
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/mixedbin.bin -Dunexpected=foo -P "${CMAKE_CURRENT_LIST_DIR}/verify_not_contains.cmake"
)
set_tests_properties(VerifyMixedInputListReplaceBin PROPERTIES
  FIXTURES_REQUIRED mixed_replace
)

# --- Test 10: Parallel Processing (large mixed list, stress test) ---
add_test(NAME ParallelProcessingStress
  COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/stage_parallel_list.cmake"
  COMMAND strclear -v --files "${TESTDIR}/parallel_files.txt" magicword zzz
)
set_tests_properties(ParallelProcessingStress PROPERTIES
  FIXTURES_SETUP parallel_stress
)

foreach(i RANGE 1 20)
  set(FP "${TESTDIR}/par${i}.txt")
  add_test(NAME VerifyParallelProcessingStress${i}
    COMMAND ${CMAKE_COMMAND} -Dfile=${FP} -Dexpected=zzz -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
  )
  set_tests_properties(VerifyParallelProcessingStress${i} PROPERTIES
    FIXTURES_REQUIRED parallel_stress
  )
endforeach()

# --- Test 11: --text-only and --binary-only filtering from a mixed list ---
add_test(NAME TextOnlyFiltering
  COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/stage_filter_list.cmake"
  COMMAND strclear -v --text-only --files "${TESTDIR}/filter_files.txt" foo TEXTED
)
set_tests_properties(TextOnlyFiltering PROPERTIES
  FIXTURES_SETUP text_filter
)
add_test(NAME VerifyTextOnlyFiltering1
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/text1.txt -Dexpected=TEXTED -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyTextOnlyFiltering1 PROPERTIES
  FIXTURES_REQUIRED text_filter
)
add_test(NAME VerifyTextOnlyFiltering2
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/text2.txt -Dexpected=TEXTED -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyTextOnlyFiltering2 PROPERTIES
  FIXTURES_REQUIRED text_filter
)
add_test(NAME VerifyTextOnlyFilteringBin1
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/bin1.bin -Dexpected=foo -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyTextOnlyFilteringBin1 PROPERTIES
  FIXTURES_REQUIRED text_filter
)
add_test(NAME VerifyTextOnlyFilteringBin2
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/bin2.bin -Dexpected=foo -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyTextOnlyFilteringBin2 PROPERTIES
  FIXTURES_REQUIRED text_filter
)

add_test(NAME BinaryOnlyFiltering
  COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/stage_filter_list.cmake"
  COMMAND strclear -v --binary-only --files "${TESTDIR}/filter_files.txt" foo
)
set_tests_properties(BinaryOnlyFiltering PROPERTIES
  FIXTURES_SETUP binary_filter
)
add_test(NAME VerifyBinaryOnlyFiltering1
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/bin1.bin -Dunexpected=foo -P "${CMAKE_CURRENT_LIST_DIR}/verify_not_contains.cmake"
)
set_tests_properties(VerifyBinaryOnlyFiltering1 PROPERTIES
  FIXTURES_REQUIRED binary_filter
)
add_test(NAME VerifyBinaryOnlyFiltering2
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/bin2.bin -Dunexpected=foo -P "${CMAKE_CURRENT_LIST_DIR}/verify_not_contains.cmake"
)
set_tests_properties(VerifyBinaryOnlyFiltering2 PROPERTIES
  FIXTURES_REQUIRED binary_filter
)
add_test(NAME VerifyBinaryOnlyFilteringText1
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/text1.txt -Dexpected=foo -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyBinaryOnlyFilteringText1 PROPERTIES
  FIXTURES_REQUIRED binary_filter
)
add_test(NAME VerifyBinaryOnlyFilteringText2
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/text2.txt -Dexpected=foo -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyBinaryOnlyFilteringText2 PROPERTIES
  FIXTURES_REQUIRED binary_filter
)

# --- Test 12: --clear-char option for binary file ---
add_test(NAME BinaryClearChar
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_LIST_DIR}/testdata/cclear.bin" "${TESTDIR}/cclear.bin"
  COMMAND strclear -v --binary-only --clear_char=X "${TESTDIR}/cclear.bin" foo
)
set_tests_properties(BinaryClearChar PROPERTIES
  FIXTURES_SETUP clearchar
)
add_test(NAME VerifyBinaryClearChar
  COMMAND ${CMAKE_COMMAND} -Dfile=${TESTDIR}/cclear.bin -Dexpected=XXX -P "${CMAKE_CURRENT_LIST_DIR}/verify_contains.cmake"
)
set_tests_properties(VerifyBinaryClearChar PROPERTIES
  FIXTURES_REQUIRED clearchar
)

# --- Test 13: Help and Version Output ---
add_test(NAME StrclearHelp
  COMMAND strclear -v --help
)
set_tests_properties(StrclearHelp PROPERTIES
  PASS_REGULAR_EXPRESSION "usage|help|clear"
)

# --- Test 14: Nonexistent File (should fail) ---
add_test(NAME NonexistentFile
  COMMAND strclear -v "${TESTDIR}/does_not_exist.txt" foo bar
)

# --- Test 15: Read-only File (should fail) ---
set(FILE_RO "${TESTDIR}/readonly.txt")
file(WRITE "${FILE_RO}" "foo bar")
execute_process(COMMAND chmod a-w "${FILE_RO}")

add_test(NAME ReadOnlyFile
  COMMAND strclear -v "${FILE_RO}" foo bar
)
set_tests_properties(ReadOnlyFile PROPERTIES
  WILL_FAIL TRUE
)
endif()

# --- Test #: Unicode/UTF-8 Replacement ---
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

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
