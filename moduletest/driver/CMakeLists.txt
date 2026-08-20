# ctltest_run_one - ctest driver binary.
# Not installed; invoked only by ctest via ctltest_add_yaml().
# Shares 100% of the execution path with the CLI (v0.4+) via ctltest_core::Runner.

add_executable(ctltest_run_one run_one.cc)

target_link_libraries(ctltest_run_one
  PRIVATE
    ctltest_core
)
