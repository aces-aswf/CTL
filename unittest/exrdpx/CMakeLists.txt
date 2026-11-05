if(NOT DEFINED EXRDPX_BUILD_DIR)
    set(EXRDPX_BUILD_DIR "${CMAKE_BINARY_DIR}/OpenEXR_CTL/exrdpx")
    message("exrdpx build director: ${EXRDPX_BUILD_DIR}")
endif()

if(NOT DEFINED EXRDPX_EXECUTABLE_PATH)
    set(EXRDPX_EXECUTABLE_PATH "${EXRDPX_BUILD_DIR}/exrdpx")
    message("exrdpx test executable: ${EXRDPX_EXECUTABLE_PATH}")
endif()

set(EXRDPX_OUTPUT_FOLDER "${CMAKE_BINARY_DIR}/unittest/exrdpx/output")
message("exrdpx test output folder will be created at: ${EXRDPX_OUTPUT_FOLDER}")
add_custom_target(exrdpx-build-time-make-directory ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${EXRDPX_OUTPUT_FOLDER})

set(TEST_FILES "${PROJECT_SOURCE_DIR}/unittest/exrdpx")

# copy CTL files to build folder
add_custom_target(
  exrdpx-build-time-copy-CTL-file1 ALL
  COMMAND ${CMAKE_COMMAND} -E copy 
    ${TEST_FILES}/transform_DPX_EXR.ctl 
    "${EXRDPX_BUILD_DIR}")
add_custom_target(
    exrdpx-build-time-copy-CTL-file2 ALL
    COMMAND ${CMAKE_COMMAND} -E copy 
    ${TEST_FILES}/transform_EXR_DPX.ctl 
    "${EXRDPX_BUILD_DIR}")
add_custom_target(
    exrdpx-build-time-copy-CTL-file3 ALL
    COMMAND ${CMAKE_COMMAND} -E copy 
    ${TEST_FILES}/utilities.ctl 
    "${EXRDPX_BUILD_DIR}")

if(OpenEXR_FOUND)
    # Test DPX->EXR conversions
    add_test(NAME "exrdpx-DPX-to-EXR-noargs" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-noargs.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-xyz" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -xyz "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-xyz.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-yc" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -yc "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-yc.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-C" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -C transform_DPX_EXR "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-C.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t0" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 0 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t0.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t1" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 1 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t1.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t2" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 2 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t2.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t3" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 3 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t3.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t4" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 4 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t4.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t8" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 8 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t8.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t16" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 16 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t16.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t32" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 32 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t32.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t64" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 64 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t64.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-t128" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -t 128 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-t128.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-z-none" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -z none "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-z-none.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-z-rle" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -z rle "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-z-rle.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-z-zip" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -z zip "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-z-rle.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-z-piz" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -z piz "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-z-piz.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-z-pxr24" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -z pxr24 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-z-pxr24.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    add_test(NAME "exrdpx-DPX-to-EXR-z-b44" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.dpx" -z b44 "${EXRDPX_OUTPUT_FOLDER}/exrdpx-DPX-to-EXR-z-b44.exr" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")

    # Test >EXR-DPX conversions
    add_test(NAME "exrdpx-EXR-to-DPX-noargs" COMMAND ${EXRDPX_EXECUTABLE_PATH} "${TEST_FILES}/marci-512.exr" "${EXRDPX_OUTPUT_FOLDER}/exrdpx-EXR-to-DPX-noargs.dpx" WORKING_DIRECTORY "${EXRDPX_BUILD_DIR}")
endif()

ctl_configure_test_environment()

