# make_large_file.cmake
# Usage: invoked by CTest to create ${TESTDIR}/large.txt with 1000 lines containing "foo"
set(TESTDIR "$ENV{TESTDIR}")
if(NOT TESTDIR)
    set(TESTDIR "${CMAKE_CURRENT_BINARY_DIR}/testfiles")
endif()
file(WRITE "${TESTDIR}/large.txt" "")
foreach(i RANGE 1 1000)
    file(APPEND "${TESTDIR}/large.txt" "foo this is line ${i}\n")
endforeach()
