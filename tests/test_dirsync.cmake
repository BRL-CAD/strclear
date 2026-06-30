# CMake test suite for dirsync
# Place this in your tests/ directory and add it to enable_testing()/add_test setup

set(SRCDIR  "${TESTDIR}/src")
set(DSTDIR  "${TESTDIR}/dst")
set(LISTFILE "${TESTDIR}/list.txt")

file(REMOVE_RECURSE "${TESTDIR}")
file(MAKE_DIRECTORY "${SRCDIR}")
file(MAKE_DIRECTORY "${DSTDIR}")

# Helper: verify file contents
function(assert_file_content file expected)
  file(READ "${file}" actual)
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "File ${file} content mismatch:\nExpected:\n${expected}\nActual:\n${actual}")
  endif()
endfunction()

# Helper: verify file existence/non-existence
function(assert_exists file)
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "Expected file or dir ${file} is missing")
  endif()
endfunction()
function(assert_not_exists file)
  if(EXISTS "${file}")
    message(FATAL_ERROR "File or dir ${file} should NOT exist")
  endif()
endfunction()

# Helper: verify symlink target
if(CHECK_SYMLINKS)
  function(assert_symlink file expected_target)
    file(READ_SYMLINK "${file}" actual)
    if(NOT "${actual}" STREQUAL "${expected_target}")
      message(FATAL_ERROR "Symlink ${file} target mismatch: expected '${expected_target}', got '${actual}'")
    endif()
  endfunction()
endif()

# Helper: verify lines in a file (for listfile)
function(assert_file_lines file expected_list)
  file(STRINGS "${file}" lines)
  list(LENGTH lines nlines)
  list(LENGTH expected_list nexpected)
  if(NOT "${nlines}" STREQUAL "${nexpected}")
    message(FATAL_ERROR "Listfile ${file} line count mismatch: got ${nlines}, expected ${nexpected}")
  endif()
  foreach(idx RANGE 0 ${nlines})
    list(GET expected_list ${idx} eline)
    list(GET lines ${idx} aline)
    if(NOT "${aline}" STREQUAL "${eline}")
      message(FATAL_ERROR "Listfile ${file} line ${idx} mismatch: got '${aline}', expected '${eline}'")
    endif()
  endforeach()
endfunction()

# --- 1. Basic copy and hierarchy preservation ---
file(WRITE "${SRCDIR}/file1.txt" "Hello\n")
file(MAKE_DIRECTORY "${SRCDIR}/subdir")
file(WRITE "${SRCDIR}/subdir/file2.txt" "World\n")

execute_process(COMMAND ${DIRSYNC} "${SRCDIR}" "${DSTDIR}")

assert_exists("${DSTDIR}/file1.txt")
assert_exists("${DSTDIR}/subdir")
assert_exists("${DSTDIR}/subdir/file2.txt")
assert_file_content("${DSTDIR}/file1.txt" "Hello\n")
assert_file_content("${DSTDIR}/subdir/file2.txt" "World\n")

# --- 2. Incremental sync: change, add, remove ---
file(WRITE "${SRCDIR}/file1.txt" "Changed\n")
file(WRITE "${SRCDIR}/file3.txt" "New file\n")
file(REMOVE "${SRCDIR}/subdir/file2.txt")

execute_process(COMMAND ${DIRSYNC} "${SRCDIR}" "${DSTDIR}")

assert_file_content("${DSTDIR}/file1.txt" "Changed\n")
assert_not_exists("${DSTDIR}/subdir/file2.txt")
assert_file_content("${DSTDIR}/file3.txt" "New file\n")
assert_exists("${DSTDIR}/subdir")

if(CHECK_SYMLINKS)
  # --- 3. Symlink cases ---
  # a) Absolute symlink to inside tree (should be converted to relative)
  file(WRITE "${SRCDIR}/inlink.txt" "target")
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink "${SRCDIR}/inlink.txt" "${SRCDIR}/abs_in_link")
  # b) Absolute symlink to outside tree (should remain absolute)
  file(WRITE "${TESTDIR}/outside.txt" "not in src")
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink "${TESTDIR}/outside.txt" "${SRCDIR}/abs_out_link")
  # c) Relative symlink to inside (should remain relative)
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink "inlink.txt" "${SRCDIR}/rel_in_link")
  # d) Relative symlink to outside (should remain relative)
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink "../outside.txt" "${SRCDIR}/rel_out_link")

  execute_process(COMMAND ${DIRSYNC} "${SRCDIR}" "${DSTDIR}")

  # The targets must be checked as strings, not resolved!
  file(READ_SYMLINK "${DSTDIR}/abs_in_link" abs_in)
  file(READ_SYMLINK "${DSTDIR}/abs_out_link" abs_out)
  file(READ_SYMLINK "${DSTDIR}/rel_in_link" rel_in)
  file(READ_SYMLINK "${DSTDIR}/rel_out_link" rel_out)

  # abs_in_link should now be a relative symlink to inlink.txt
  if(NOT "${abs_in}" STREQUAL "inlink.txt")
    message(FATAL_ERROR "abs_in_link not fixed to relative: got '${abs_in}'")
  endif()
  # abs_out_link should remain absolute
  if(NOT "${abs_out}" STREQUAL "${TESTDIR}/outside.txt")
    message(FATAL_ERROR "abs_out_link should remain absolute: got '${abs_out}'")
  endif()
  # rel_in_link should remain as inlink.txt
  if(NOT "${rel_in}" STREQUAL "inlink.txt")
    message(FATAL_ERROR "rel_in_link wrong: got '${rel_in}'")
  endif()
  # rel_out_link should remain as ../outside.txt
  if(NOT "${rel_out}" STREQUAL "../outside.txt")
    message(FATAL_ERROR "rel_out_link wrong: got '${rel_out}'")
  endif()
endif()

# --- 4. Listfile output ---
file(WRITE "${SRCDIR}/file4.txt" "listme")
execute_process(COMMAND ${DIRSYNC} "${SRCDIR}" "${DSTDIR}" --listfile "${LISTFILE}")

# Only file4.txt should be listed as newly added (and possibly abs_in_link if it was new)
file(STRINGS "${LISTFILE}" listed)
list(FIND listed "${DSTDIR}/file4.txt" idx4)
if(idx4 EQUAL -1)
  message(FATAL_ERROR "file4.txt missing from listfile")
endif()

message("All dirsync tests passed.")

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
