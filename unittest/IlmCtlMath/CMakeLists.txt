set(SOURCES
	main.cpp
	testAffineRec.cpp
	testGaussRec.cpp
	testMathVectorParity.cpp
)

add_executable(IlmCtlMathTest ${SOURCES})

target_include_directories(IlmCtlMathTest
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(IlmCtlMathTest
	PRIVATE
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
)

# testMathVectorParity validates whichever vectorized transcendental
# backend was compiled into IlmCtlSimd against scalar libm.  It calls
# the vendor entry points directly (vvexpf / Sleef_expf4_u10*), so it
# needs the same compile definitions and libraries as IlmCtlSimd.
if(CTL_USE_ACCELERATE)
	target_compile_definitions(IlmCtlMathTest PRIVATE CTL_USE_ACCELERATE=1)
	target_link_libraries(IlmCtlMathTest PRIVATE "-framework Accelerate")
endif()
if(CTL_USE_SLEEF)
	target_compile_definitions(IlmCtlMathTest PRIVATE CTL_USE_SLEEF=1)
	target_link_libraries(IlmCtlMathTest PRIVATE ${CTL_SLEEF_TARGET})
	# Set only when sleef came from FetchContent, whose in-tree targets do not
	# export the directory holding the generated sleef.h.
	if(CTL_SLEEF_INCLUDE_DIR)
		target_include_directories(IlmCtlMathTest PRIVATE ${CTL_SLEEF_INCLUDE_DIR})
	endif()
endif()

add_test(IlmCtlMath IlmCtlMathTest)
