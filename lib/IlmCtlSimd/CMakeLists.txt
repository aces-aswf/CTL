# Build the halfExpLogTable.h file
#add_executable(halfExpLogTable halfExpLogTable.cpp)
#if(IlmBase_FOUND)
#	target_link_libraries(halfExpLogTable
#	PUBLIC
#	# For OpenEXR 2.4/2.5:
#	$<$<TARGET_EXISTS:IlmBase::Imath>:IlmBase::Imath>
#	$<$<TARGET_EXISTS:IlmBase::Half>:IlmBase::Half>
#	)
#else()
#	target_link_libraries(halfExpLogTable
#	PUBLIC
#	# For OpenEXR/Imath 3.x:
#	$<$<TARGET_EXISTS:Imath::Imath>:Imath::Imath>
#	$<$<TARGET_EXISTS:Imath::Half>:Imath::Half>
#	)
#endif()

#target_link_libraries(halfExpLogTable
#	PUBLIC
#	# For OpenEXR/Imath 3.x:
#	$<$<TARGET_EXISTS:Imath::Imath>:Imath::Imath>
#	$<$<TARGET_EXISTS:Imath::Half>:Imath::Half>
#	# For OpenEXR 2.4/2.5:
#	$<$<TARGET_EXISTS:IlmBase::Imath>:IlmBase::Imath>
#	$<$<TARGET_EXISTS:IlmBase::Half>:IlmBase::Half>
#)

#set(HALF_TABLE "${CMAKE_CURRENT_BINARY_DIR}/halfExpLogTable.h")
#set(HALF_EXPFUNCS "${CMAKE_CURRENT_BINARY_DIR}/halfExpLog.h")

#add_custom_command( OUTPUT "${HALF_TABLE}" "${HALF_EXPFUNCS}"
#	COMMAND halfExpLogTable "${HALF_TABLE}" "${HALF_EXPFUNCS}"
#	DEPENDS halfExpLogTable
#)

set(SOURCES
	CtlSimdAddr.cpp
	CtlSimdArena.cpp
	CtlSimdFunctionCall.cpp
	CtlSimdHalfExpLog.cpp
	CtlSimdInst.cpp
	CtlSimdInterpreter.cpp
	CtlSimdLContext.cpp
	CtlSimdModule.cpp
	CtlSimdReg.cpp
	CtlSimdStdLibAssert.cpp
	CtlSimdStdLibColorSpace.cpp
	CtlSimdStdLibInterpolator.cpp
	CtlSimdStdLibLimits.cpp
	CtlSimdStdLibLookupTable.cpp
	CtlSimdStdLibMath.cpp
	CtlSimdStdLibPrint.cpp
	CtlSimdStdLibrary.cpp
	CtlSimdStdTypes.cpp
	CtlSimdSyntaxTree.cpp
	CtlSimdType.cpp
	CtlSimdXContext.cpp
	#${HALF_TABLE}
	#${HALF_EXPFUNCS}
	halfExpLogTable.h
	halfExpLog.h
)
add_library(IlmCtlSimd ${SOURCES})

target_include_directories(IlmCtlSimd
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(IlmCtlSimd
	PUBLIC
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

if(CTL_USE_ACCELERATE)
	target_compile_definitions(IlmCtlSimd PRIVATE CTL_USE_ACCELERATE=1)
	target_link_libraries(IlmCtlSimd PRIVATE "-framework Accelerate")
endif()

if(CTL_USE_SLEEF)
	target_compile_definitions(IlmCtlSimd PRIVATE
		CTL_USE_SLEEF=1
		CTL_SLEEF_ULP=${CTL_SLEEF_ULP})
	target_link_libraries(IlmCtlSimd PRIVATE ${CTL_SLEEF_TARGET})
	# Set only when sleef came from FetchContent, whose in-tree targets do not
	# export the directory holding the generated sleef.h.
	if(CTL_SLEEF_INCLUDE_DIR)
		target_include_directories(IlmCtlSimd PRIVATE ${CTL_SLEEF_INCLUDE_DIR})
	endif()
endif()

target_link_libraries(IlmCtlSimd PUBLIC Threads::Threads)

set_target_properties(IlmCtlSimd
	PROPERTIES
		VERSION "${CTL_VERSION}"
		SOVERSION "${CTL_VERSION_MAJOR}.${CTL_VERSION_MINOR}"
)

install(FILES CtlSimdInterpreter.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CTL)

install(TARGETS IlmCtlSimd DESTINATION ${CMAKE_INSTALL_LIBDIR})
