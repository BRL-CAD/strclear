# Reset input file
set(TFILE "plain.txt")
file(COPY "${SDIR}/${TFILE}" DESTINATION "${TDIR}")
set(TF "${TDIR}/${TFILE}")

# Set output string
set(expected "baz")

# Run strclear
execute_process(COMMAND "${STRCLEAR}" -v "${TF}" foo ${expected})

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
