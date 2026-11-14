# Explicit registration helper, compatible with the project's CMake 3.16 floor.
# The wrapper changes only child TEMP/TMP/TMPDIR and preserves command arguments.
function(minecraftc_add_test)
    cmake_parse_arguments(TEST "" "NAME;WORKING_DIRECTORY" "COMMAND" ${ARGN})
    if(TEST_UNPARSED_ARGUMENTS OR NOT TEST_NAME OR NOT TEST_COMMAND)
        message(FATAL_ERROR "Invalid minecraftc_add_test arguments: ${ARGN}")
    endif()
    list(GET TEST_COMMAND 0 test_program)
    if(TARGET "${test_program}")
        list(REMOVE_AT TEST_COMMAND 0)
        list(INSERT TEST_COMMAND 0 "$<TARGET_FILE:${test_program}>")
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/run_test.py --name ${TEST_NAME}
        --root ${CMAKE_BINARY_DIR}/t -- ${TEST_COMMAND})
    set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 120 LABELS "fast")
    if(TEST_WORKING_DIRECTORY)
        set_tests_properties(${TEST_NAME} PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}")
    endif()
endfunction()
