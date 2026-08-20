# ctltest - standalone CLI, installed alongside ctlrender.

add_executable(ctltest main.cc)
target_link_libraries(ctltest PRIVATE ctltest_core)

install(TARGETS ctltest
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                            COMPONENT ctltest_runtime)
