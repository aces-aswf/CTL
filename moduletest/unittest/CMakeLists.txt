# ctltest_unit - C++ unit tests for ctltest_core's public API.
#
# These are distinct from the YAML self-test suites under tests/selftest/,
# which exercise the framework end-to-end via the ctltest_run_one driver.
# ctltest_unit pokes the library's public surface directly (Value,
# Tolerance::merge, compareTyped, validateTolerancePaths, CsvTable, YamlLoader,
# Marshal round-trips) so failures point at exactly the API that regressed.

add_executable(ctltest_unit ctltest_unit.cc)

target_link_libraries(ctltest_unit
  PRIVATE
    ctltest_core
)

# Marshal round-trip tests drive InterpRunner against the shared st_scalar.ctl
# fixture; the runner's setModulePaths({"."}) resolves against CWD, so copy the
# fixture next to the binary at configure time. Keeping the path relative means
# the binary works both from a plain build dir and from the install tree if
# copied out (self-tests already depend on this fixture being stable).
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/st_scalar.ctl
  ${CMAKE_CURRENT_BINARY_DIR}/st_scalar.ctl
  COPYONLY
)

# v1.2 hardening: ut_native.ctl is a TestKit fixture for the ctl_native
# dispatch tests. Copied next to the binary so InterpRunner::setModulePaths({"."})
# can resolve "ut_native" alongside "st_scalar".
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ut_native.ctl
  ${CMAKE_CURRENT_BINARY_DIR}/ut_native.ctl
  COPYONLY
)

# Pass the absolute path of the standalone CLI binary into the unit-test
# binary so subprocess-driven CLI tests can invoke it without relying on
# install paths. ctltest_unit also needs the cli target built first.
add_dependencies(ctltest_unit ctltest)
target_compile_definitions(ctltest_unit PRIVATE
  CTLTEST_CLI_BINARY="$<TARGET_FILE:ctltest>"
)

add_test(
  NAME    ctltest::unit
  COMMAND ctltest_unit
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ctltest-unit label keeps these out of the ctltest label (which is for the
# YAML-driven self-tests). `ctest -L ctltest-unit` runs the C++ tier only.
set_tests_properties(ctltest::unit PROPERTIES LABELS "ctltest-unit")
