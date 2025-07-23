# stage_mixed_list.cmake: create files for the mixed --files test
set(TESTDIR "$ENV{TESTDIR}")
if(NOT TESTDIR)
    set(TESTDIR "${CMAKE_CURRENT_BINARY_DIR}/testfiles")
endif()
file(WRITE "${TESTDIR}/mixed1.txt" "alpha foo beta\nfoo gamma\n")
file(WRITE "${TESTDIR}/mixed2.txt" "foo delta\nfoo epsilon\n")
file(WRITE "${TESTDIR}/mixed3.txt" "zeta foo eta\n")
file(WRITE "${TESTDIR}/mixedbin.bin" "randomdatafoootherdata")
file(WRITE "${TESTDIR}/mixed_files.txt"
    "${TESTDIR}/mixed1.txt\n${TESTDIR}/mixed2.txt\n${TESTDIR}/mixed3.txt\n${TESTDIR}/mixedbin.bin\n"
)
