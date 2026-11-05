set(SOURCES
    main.cpp
    testSourceDestination.cpp
    testTypes.cpp
)

add_executable(IlmImfCtlTest ${SOURCES})

target_include_directories(IlmImfCtlTest
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries( IlmImfCtlTest
	PRIVATE
		IlmImfCtl
		IlmCtlSimd
		IlmCtlMath
		IlmCtl
	PRIVATE
    # For OpenEXR/Imath 3.x:
      $<$<TARGET_EXISTS:OpenEXR::OpenEXR>:OpenEXR::OpenEXR>
      $<$<TARGET_EXISTS:Imath::Imath>:Imath::Imath>
      $<$<TARGET_EXISTS:Imath::Half>:Imath::Half>
    # For OpenEXR 2.4/2.5:
      $<$<TARGET_EXISTS:OpenEXR::IlmImf>:OpenEXR::IlmImf>
      $<$<TARGET_EXISTS:IlmBase::Imath>:IlmBase::Imath>
      $<$<TARGET_EXISTS:IlmBase::Half>:IlmBase::Half>
      $<$<TARGET_EXISTS:IlmBase::IlmThread>:IlmBase::IlmThread>
      $<$<TARGET_EXISTS:IlmBase::Iex>:IlmBase::Iex>
    # ZLIB for OpenEXR 2.x:
      $<$<TARGET_EXISTS:ZLIB::ZLIB>:ZLIB::ZLIB>
)

add_test( IlmImfCtl IlmImfCtlTest )

file(COPY
    function1.ctl
    function2.ctl
    function3.ctl
DESTINATION
	${CMAKE_CURRENT_BINARY_DIR}
)

ctl_configure_test_environment()
