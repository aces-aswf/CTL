if(NOT DEFINED EXR_CTL_EXR_BUILD_DIR)
    set(EXR_CTL_EXR_BUILD_DIR "${CMAKE_BINARY_DIR}/OpenEXR_CTL/exr_ctl_exr")
    message("exr_ctl_exr build director: ${EXR_CTL_EXR_BUILD_DIR}")
endif()

if(NOT DEFINED EXR_CTL_EXR_EXECUTABLE_PATH)
    set(EXR_CTL_EXR_EXECUTABLE_PATH "${EXR_CTL_EXR_BUILD_DIR}/exr_ctl_exr")
    message("exr_ctl_exr test executable: ${EXR_CTL_EXR_EXECUTABLE_PATH}")
endif()

set(EXR_CTL_EXR_OUTPUT_FOLDER "${CMAKE_BINARY_DIR}/unittest/exr_ctl_exr/output")
message("exr_ctl_exr test output folder will be created at: ${EXR_CTL_EXR_OUTPUT_FOLDER}")
add_custom_target(exr-ctl-exr-build-time-make-directory ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${EXR_CTL_EXR_OUTPUT_FOLDER})

set(TEST_FILES "${PROJECT_SOURCE_DIR}/unittest/exr_ctl_exr")

# copy CTL files to build folder
add_custom_target(
    exr-ctl-exr-build-time-copy-CTL-file1 ALL
  COMMAND ${CMAKE_COMMAND} -E copy 
    ${TEST_FILES}/change_saturation.ctl 
    "${EXR_CTL_EXR_BUILD_DIR}")

if(OpenEXR_FOUND)

    add_test(NAME "exr-ctl-exr-noargs" COMMAND ${EXR_CTL_EXR_EXECUTABLE_PATH} -C change_saturation "${TEST_FILES}/marci-512.exr" "${EXR_CTL_EXR_OUTPUT_FOLDER}/exr-ctl-exr-noargs.exr" WORKING_DIRECTORY "${EXR_CTL_EXR_BUILD_DIR}")

    add_test(NAME "exr-ctl-exr-t-3" COMMAND ${EXR_CTL_EXR_EXECUTABLE_PATH} -C change_saturation -t 3 "${TEST_FILES}/marci-512.exr" "${EXR_CTL_EXR_OUTPUT_FOLDER}/exr-ctl-exr-t-3.exr" WORKING_DIRECTORY "${EXR_CTL_EXR_BUILD_DIR}")

    add_test(NAME "exr-ctl-exr-t-4" COMMAND ${EXR_CTL_EXR_EXECUTABLE_PATH} -C change_saturation -t 4 "${TEST_FILES}/marci-512.exr" "${EXR_CTL_EXR_OUTPUT_FOLDER}/exr-ctl-exr-t-4.exr" WORKING_DIRECTORY "${EXR_CTL_EXR_BUILD_DIR}")

    add_test(NAME "exr-ctl-exr-sScale-1.0" COMMAND ${EXR_CTL_EXR_EXECUTABLE_PATH} -C change_saturation -float sScale 1.0 "${TEST_FILES}/marci-512.exr" "${EXR_CTL_EXR_OUTPUT_FOLDER}/exr-ctl-exr-sScale-1.0.exr" WORKING_DIRECTORY "${EXR_CTL_EXR_BUILD_DIR}")

    add_test(NAME "exr-ctl-exr-sScale-1.2" COMMAND ${EXR_CTL_EXR_EXECUTABLE_PATH} -C change_saturation -float sScale 1.2 "${TEST_FILES}/marci-512.exr" "${EXR_CTL_EXR_OUTPUT_FOLDER}/exr-ctl-exr-sScale-1.2.exr" WORKING_DIRECTORY "${EXR_CTL_EXR_BUILD_DIR}")

    add_test(NAME "exr-ctl-exr-sScale-0.8" COMMAND ${EXR_CTL_EXR_EXECUTABLE_PATH} -C change_saturation -float sScale 0.8 "${TEST_FILES}/marci-512.exr" "${EXR_CTL_EXR_OUTPUT_FOLDER}/exr-ctl-exr-sScale-0.8.exr" WORKING_DIRECTORY "${EXR_CTL_EXR_BUILD_DIR}")

    add_test(NAME "exr-ctl-exr-sScale-1.2-adoptedNeutral-D65" COMMAND ${EXR_CTL_EXR_EXECUTABLE_PATH} -C change_saturation -float sScale 1.2 -float2 adoptedNeutral 0.31271 0.32902 "${TEST_FILES}/marci-512.exr" "${EXR_CTL_EXR_OUTPUT_FOLDER}/exr-ctl-exr-sScale-1.2-adoptedNeutral-D65.exr" WORKING_DIRECTORY "${EXR_CTL_EXR_BUILD_DIR}")

    add_test(NAME "exr-ctl-exr-sScale-1.2-adoptedNeutral-D93" COMMAND ${EXR_CTL_EXR_EXECUTABLE_PATH} -C change_saturation -float sScale 1.2 -float2 adoptedNeutral 0.28315 0.29711 "${TEST_FILES}/marci-512.exr" "${EXR_CTL_EXR_OUTPUT_FOLDER}/exr-ctl-exr-sScale-1.2-adoptedNeutral-D93.exr" WORKING_DIRECTORY "${EXR_CTL_EXR_BUILD_DIR}")

endif()

ctl_configure_test_environment()
