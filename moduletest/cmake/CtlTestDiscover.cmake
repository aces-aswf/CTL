# CtlTestDiscover.cmake
#
# Exposes ctltest_add_yaml(relpath) which registers one ctest entry per YAML
# suite file, invoking ctltest_run_one on it. Test name is "ctltest::<relpath>"
# and all entries carry the "ctltest" label for filtering via `ctest -L ctltest`.
#
# Used by moduletest/CMakeLists.txt to process tests/manifest.txt.

function(ctltest_add_yaml rel_yaml_path)
  set(_abs "${CMAKE_CURRENT_SOURCE_DIR}/${rel_yaml_path}")
  if(NOT EXISTS "${_abs}")
    message(FATAL_ERROR
      "ctltest manifest references ${rel_yaml_path} but file not found at ${_abs}")
  endif()
  set(_name "ctltest::${rel_yaml_path}")
  add_test(
    NAME "${_name}"
    COMMAND $<TARGET_FILE:ctltest_run_one> "${_abs}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  )
  set_tests_properties("${_name}" PROPERTIES LABELS "ctltest")
endfunction()
