# Reset input file
set(TFILE "withpaths.txt")
set(TF "${TDIR}/${TFILE}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TDIR}/${TFILE}.in" "${TF}")

# Make sure target files are staged
file(COPY "${SDIR}/relpath.txt" DESTINATION "${TDIR}")
file(COPY "${SDIR}/abspath.txt" DESTINATION "${TDIR}")

# Set value to replace with and check
set(expected "CLEARED")

# Run strclear
execute_process(COMMAND "${STRCLEAR}" -v -p "${TF}" "relpath.txt" ${expected})

# Check output file contents
file(READ "${TF}" file_content)
string(FIND "${file_content}" "${expected}" found)
if(found EQUAL -1)
  message(FATAL_ERROR "File ${TF} does not contain expected text: '${expected}'\nActual content:\n${file_content}")
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8
