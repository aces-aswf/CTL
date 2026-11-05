# Call this after the last add_test() in each directory so the UBSan runtime
# options apply to every test registered there for a UBSan build.
function(ctl_configure_test_environment)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type)
    if(build_type STREQUAL "ubsan")
        # Abort so CTest WILL_FAIL tests cannot mistake sanitizer errors for expected failures.
        get_property(tests DIRECTORY PROPERTY TESTS)
        set_property(TEST ${tests} APPEND PROPERTY ENVIRONMENT
            "UBSAN_OPTIONS=abort_on_error=1:print_stacktrace=1")
    endif()
endfunction()

add_subdirectory(IlmCtl)
add_subdirectory(IlmCtlMath)
add_subdirectory(IlmImfCtl)
if (CTL_BUILD_TOOLS)
    add_subdirectory(ctlrender)
    add_subdirectory(exrdpx)
    add_subdirectory(exr_ctl_exr)
endif()

