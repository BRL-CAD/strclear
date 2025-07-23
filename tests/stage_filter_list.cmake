# stage_filter_list.cmake: create files for text/binary only filter
set(TESTDIR "$ENV{TESTDIR}")
if(NOT TESTDIR)
    set(TESTDIR "${CMAKE_CURRENT_BINARY_DIR}/testfiles")
endif()
file(WRITE "${TESTDIR}/text1.txt" "apple foo apple")
file(WRITE "${TESTDIR}/text2.txt" "banana foo banana")
file(WRITE "${TESTDIR}/bin1.bin" "xxxyyyfoozzz")
file(WRITE "${TESTDIR}/bin2.bin" "aaafooaaa")
file(WRITE "${TESTDIR}/filter_files.txt"
    "${TESTDIR}/text1.txt\n${TESTDIR}/text2.txt\n${TESTDIR}/bin1.bin\n${TESTDIR}/bin2.bin\n"
)
