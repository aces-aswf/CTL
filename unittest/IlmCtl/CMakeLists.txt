set(SOURCES
	main.cpp
	testBoolStorage.cpp
  testCppCall.cpp
	testEndOfLine.cpp
	testExamples.cpp
	testHugeInit.cpp
	testParser.cpp
	testPathParser.cpp
	testVarying.cpp
	testVaryingLookup.cpp
	testVaryingReturn.cpp
)
add_executable(IlmCtlTest ${SOURCES})

target_include_directories(IlmCtlTest
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
)
                     
target_link_libraries(IlmCtlTest
	PRIVATE
		IlmCtlSimd
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

add_test(IlmCtl IlmCtlTest)

file(COPY
	common.ctl
	testBoolStorage.ctl
	example.ctl
	testArray.ctl
	testCast.ctl
	testComments.ctl
	testCppCall.ctl
	testCtlVersion.ctl
	test.ctl
	testDefaults.ctl
	testEmpty.ctl
	testExamples.ctl
	testExamplesNamespace.ctl
	testExpr.ctl
	testFunc.ctl
	testHugeInit.ctl
	testInterpolator.ctl
	testLiterals.ctl
	testLookupTables.ctl
	testLoops.ctl
	testName2.ctl
	testName.ctl
	testNameSpace2.ctl
	testNameSpace.ctl
	testNoName.ctl
	testParse.ctl
	testPathParser.ctl
	testScope2.ctl
	testScope.ctl
	testStdLibrary.ctl
	testStruct.ctl
	testTypes.ctl
	testVarying.ctl
	testVaryingLookup.ctl
	testVaryingReturn.ctl
	testVSArrays.ctl
DESTINATION
	${CMAKE_CURRENT_BINARY_DIR}
)
