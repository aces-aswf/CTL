if(NOT DEFINED CTLRENDER_PATH)
    set(CTLRENDER_PATH "${CMAKE_BINARY_DIR}/ctlrender/ctlrender")
    message("ctlrender test executable: ${CTLRENDER_PATH}")
endif()

find_package(TIFF)
if ( TARGET TIFF::TIFF AND OpenEXR_FOUND )
    message(STATUS "LibTIFF found, OpenEXR found, including TIF and EXR files in ctlrender unit tests")
elseif( TARGET TIFF::TIFF AND NOT OpenEXR_FOUND )
    message(WARNING "LibTIFF found, OpenEXR not found, including TIF files in ctlrender unit tests")
elseif( NOT TARGET TIFF::TIFF AND OpenEXR_FOUND )
    message(WARNING "LibTIFF not found, OpenEXR found, including EXR files in ctlrender unit tests")
elseif( NOT TARGET TIFF::TIFF AND NOT OpenEXR_FOUND )
    message(WARNING "LibTIFF not found, OpenEXR not found, not including TIF or EXR files in ctlrender unit tests")
endif()

find_package(AcesContainer)
if( AcesContainer_FOUND )
	message( STATUS "DEBUG: find_package(AcesContainer) AcesContainer_FOUND is true")
    message( STATUS "DEBUG: including ACES ctlrender unit tests")
endif()

set(CTLRENDER_OUTPUT_FOLDER "${CMAKE_BINARY_DIR}/unittest/ctlrender/output")
message("ctlrender test output folder will be created at: ${CTLRENDER_OUTPUT_FOLDER}")
add_custom_target(ctlrender-build-time-make-directory ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CTLRENDER_OUTPUT_FOLDER})

set(TEST_FILES "${PROJECT_SOURCE_DIR}/unittest/ctlrender")

# Helper: emits a deterministic gradient EXR of arbitrary size.  Used by
# the parallel float->half coverage tests below to materialise a fixture
# above the parallel-path threshold without checking in a binary blob.
if(OpenEXR_FOUND)
    add_executable(gen_large_fixture gen_large_fixture.cpp)
    target_link_libraries(gen_large_fixture
        PRIVATE
            $<$<TARGET_EXISTS:OpenEXR::OpenEXR>:OpenEXR::OpenEXR>
            $<$<TARGET_EXISTS:Imath::Imath>:Imath::Imath>
            $<$<TARGET_EXISTS:Imath::Half>:Imath::Half>
            $<$<TARGET_EXISTS:OpenEXR::IlmImf>:OpenEXR::IlmImf>
            $<$<TARGET_EXISTS:IlmBase::Imath>:IlmBase::Imath>
            $<$<TARGET_EXISTS:IlmBase::Half>:IlmBase::Half>
            # OpenEXR 2.x splits Iex/IlmThread out of IlmImf and its static
            # archives do not carry the dependency, so iex_debugTrap and the
            # thread pool have to be named here explicitly.
            $<$<TARGET_EXISTS:IlmBase::IlmThread>:IlmBase::IlmThread>
            $<$<TARGET_EXISTS:IlmBase::Iex>:IlmBase::Iex>
            # OpenEXR 2.x's compressors call zlib directly; 3.x pulls it in
            # itself, 2.x leaves it to the consumer.
            $<$<TARGET_EXISTS:ZLIB::ZLIB>:ZLIB::ZLIB>)

    # Reference generator for the parallel float->half tests.  Runs an
    # unambiguously-serial conversion in a separate process, so the
    # result is independent of exr_file.cc's parallel code path.
    add_executable(gen_serial_half_ref gen_serial_half_ref.cpp)
    target_link_libraries(gen_serial_half_ref
        PRIVATE
            $<$<TARGET_EXISTS:OpenEXR::OpenEXR>:OpenEXR::OpenEXR>
            $<$<TARGET_EXISTS:Imath::Imath>:Imath::Imath>
            $<$<TARGET_EXISTS:Imath::Half>:Imath::Half>
            $<$<TARGET_EXISTS:OpenEXR::IlmImf>:OpenEXR::IlmImf>
            $<$<TARGET_EXISTS:IlmBase::Imath>:IlmBase::Imath>
            $<$<TARGET_EXISTS:IlmBase::Half>:IlmBase::Half>
            # OpenEXR 2.x splits Iex/IlmThread out of IlmImf and its static
            # archives do not carry the dependency, so iex_debugTrap and the
            # thread pool have to be named here explicitly.
            $<$<TARGET_EXISTS:IlmBase::IlmThread>:IlmBase::IlmThread>
            $<$<TARGET_EXISTS:IlmBase::Iex>:IlmBase::Iex>
            # OpenEXR 2.x's compressors call zlib directly; 3.x pulls it in
            # itself, 2.x leaves it to the consumer.
            $<$<TARGET_EXISTS:ZLIB::ZLIB>:ZLIB::ZLIB>)
endif()

foreach(J tiff8 tiff16 tiff32 dpx8 dpx10 dpx12 dpx16)
    foreach(I  bars_cinepaint_10.dpx bars_nuke_10_be.dpx bars_nuke_10_le.dpx bars_nuke_12_be.dpx bars_nuke_12_le.dpx bars_nuke_16_be.dpx bars_nuke_16_le.dpx bars_nuke_8_be.dpx bars_nuke_8_le.dpx bars_photoshop_16_be_interleaved.tif bars_photoshop_16_be_planar.tif bars_photoshop_16_le_interleaved.tif bars_photoshop_16_le_planar.tif bars_photoshop_32_be_interleaved.tif bars_photoshop_32_be_planar.tif bars_photoshop_32_le_interleaved.tif bars_photoshop_32_le_planar.tif bars_photoshop_8_be_interleaved.tif bars_photoshop_8_be_planar.tif bars_photoshop_8_le_interleaved.tif bars_photoshop_8_le_planar.tif)
        # Get extension from ${J}
        string(REGEX REPLACE "[0-9]+" "" EXTENSION "${J}")
        # Get filename from ${I}
        string(REGEX REPLACE "\\..+" "" FILE_NAME "${I}")
        # Get file extension from ${I}
        string(REGEX REPLACE ".+\\." "" FILE_NAME_EXTENSION "${I}")
        if (NOT TARGET TIFF::TIFF AND ${EXTENSION} STREQUAL "tiff")
            message("Skipping ${J} because tiff support was not detected")
        elseif(NOT TARGET TIFF::TIFF AND ${FILE_NAME_EXTENSION} STREQUAL "tif")
            message("Skipping ${J} because tiff support was not detected")
        else()
            add_test(NAME "ctlrender-${I}->${J}_unity" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format ${J} -force "${TEST_FILES}/${I}" "${CTLRENDER_OUTPUT_FOLDER}/${FILE_NAME}_${J}.${EXTENSION}")
        endif()
    endforeach(I)

    # Test tiff32 conversions
    if(TARGET TIFF::TIFF)
        # convert TIFF32 to other format
        add_test(NAME "ctlrender-tiff32->${J}" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format ${J} -force "${TEST_FILES}/bars_photoshop_32_be_planar.tif" "${CTLRENDER_OUTPUT_FOLDER}/bars_tiff32_${J}.${EXTENSION}")
    endif()
endforeach(J)

if(OpenEXR_FOUND)
    # Test EXR->EXR conversions
    add_test(NAME "ctlrender-EXR->EXR" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr -force "${TEST_FILES}/bars_photoshop.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_exr_exr.exr")
    add_test(NAME "ctlrender-EXR->EXR16" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force "${TEST_FILES}/bars_photoshop.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_exr_exr16.exr")
    add_test(NAME "ctlrender-EXR->EXR32" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr32 -force "${TEST_FILES}/bars_photoshop.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_exr_exr32.exr")

    # RGB Tests
    add_test(NAME "ctlrender-EXR16->EXR16" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16.exr")
    add_test(NAME "ctlrender-EXR16->EXR32" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr32 -force "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr32.exr")
    add_test(NAME "ctlrender-EXR32->EXR16" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr32_to_exr16.exr")
    add_test(NAME "ctlrender-EXR32->EXR32" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr32 -force "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr32_to_exr32.exr")

    # RGBA Tests
    add_test(NAME "ctlrender-EXR_RGBA16->EXR_RGBA_16" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity_with_alpha.ctl" -format exr16 -force "${TEST_FILES}/colorbars_nuke_rgba_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgba_exr16_to_exr16.exr")
    add_test(NAME "ctlrender-EXR_RGBA16->EXR_RGBA_32" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity_with_alpha.ctl" -format exr32 -force "${TEST_FILES}/colorbars_nuke_rgba_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgba_exr16_to_exr32.exr")
    add_test(NAME "ctlrender-EXR_RGBA32->EXR_RGBA_16" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity_with_alpha.ctl" -format exr16 -force "${TEST_FILES}/colorbars_nuke_rgba_exr32.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgba_exr32_to_exr16.exr")
    add_test(NAME "ctlrender-EXR_RGBA32->EXR_RGBA_32" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity_with_alpha.ctl" -format exr32 -force "${TEST_FILES}/colorbars_nuke_rgba_exr32.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgba_exr32_to_exr32.exr")

    # EXR compression tests
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-NONE" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression none "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_none.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-RLE" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression rle "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_rle.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-ZIPS" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression zips "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_zips.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-ZIP" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression zip "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_zip.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-PIZ" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression piz "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_piz.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-PXR24" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression pxr24 "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_pxr24.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-B44" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression b44 "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_b44.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-B44A" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression b44a "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_b44a.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-DWAA" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression dwaa "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_dwaa.exr")
    add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-DWAB" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression dwab "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_dwab.exr")
    if((OPENEXR_VERSION_MAJOR GREATER_EQUAL 3 AND OPENEXR_VERSION_MINOR GREATER_EQUAL 4) OR (OPENEXR_VERSION_MAJOR GREATER_EQUAL 4))
        add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-HTJ2K256" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression htj2k256 "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_htj2k256.exr")
        add_test(NAME "ctlrender-EXR16->EXR16-COMPRESSION-HTJ2K32" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr16 -force -compression htj2k32 "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_exr16_compression_htj2k32.exr")
    endif()

endif()

#ACES tests
if (OpenEXR_FOUND AND AcesContainer_FOUND)
    add_test(NAME "ctlrender-EXR16->ACES" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format aces -force "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_aces.aces")
endif()

# test TIFF32 to EXR and EXR to TIFF32 support
if (OpenEXR_FOUND AND TARGET TIFF::TIFF)
    foreach(J exr exr16 exr32 tiff32)
        foreach(I bars_photoshop_32_be_interleaved.tif bars_photoshop_32_be_planar.tif bars_photoshop_32_le_interleaved.tif bars_photoshop_32_le_planar.tif bars_photoshop.exr)
             # Get extension from ${J}
            string(REGEX REPLACE "[0-9]+" "" EXTENSION "${J}")
            # Get filename from ${I}
            string(REGEX REPLACE "\\..+" "" FILE_NAME "${I}")
            # Get file extension from ${I}
            string(REGEX REPLACE ".+\\." "" FILE_NAME_EXTENSION "${I}")
            if(${J} MATCHES "exr.*" AND FILE_NAME_EXTENSION STREQUAL "exr")
                continue()
            endif()
            if(${J} MATCHES "tif.*" AND FILE_NAME_EXTENSION STREQUAL "tif")
                continue()
            endif()
            add_test(NAME "ctlrender-EXR<->TIF_${I}->${J}" COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format ${J} -force "${TEST_FILES}/${I}" "${CTLRENDER_OUTPUT_FOLDER}/${FILE_NAME}_${J}_exr_tif_conversion_test.${EXTENSION}")
        endforeach()
    endforeach()
endif()

add_test(NAME "ctlrender-ctl-function-name-is-filename" COMMAND ctlrender -force -format exr32 -ctl "${TEST_FILES}/functionname_is_filename.ctl" "${TEST_FILES}/bars_photoshop.exr" "${CTLRENDER_OUTPUT_FOLDER}/functionname_is_filename.exr")

add_test(NAME "ctlrender-ctl-function-name-is-incorrect" COMMAND ctlrender -force -format exr32 -ctl "${TEST_FILES}/incorrect_function_name.ctl" "${TEST_FILES}/bars_photoshop.exr" "${CTLRENDER_OUTPUT_FOLDER}/incorrect_function_name.exr")
set_tests_properties("ctlrender-ctl-function-name-is-incorrect" PROPERTIES WILL_FAIL TRUE)

# Test for invalid input file names
add_test(NAME "ctlrender-invalid-input-filename-dpx" COMMAND ctlrender -force -format dpx -ctl "${TEST_FILES}/unity.ctl" "${TEST_FILES}/invalid_filename.dpx" "${CTLRENDER_OUTPUT_FOLDER}/invalid_filename_out.dpx")
set_tests_properties("ctlrender-invalid-input-filename-dpx" PROPERTIES WILL_FAIL TRUE)
if(TARGET TIFF::TIFF)
    add_test(NAME "ctlrender-invalid-input-filename-tiff" COMMAND ctlrender -force -format tiff -ctl "${TEST_FILES}/unity.ctl" "${TEST_FILES}/invalid_filename.tif" "${CTLRENDER_OUTPUT_FOLDER}/invalid_filename_out.tif")
    set_tests_properties("ctlrender-invalid-input-filename-tiff" PROPERTIES WILL_FAIL TRUE)
endif()
if(OpenEXR_FOUND)
    add_test(NAME "ctlrender-invalid-input-filename-exr" COMMAND ctlrender -force -format exr -ctl "${TEST_FILES}/unity.ctl" "${TEST_FILES}/invalid_filename.exr" "${CTLRENDER_OUTPUT_FOLDER}/invalid_filename_out.exr")
    set_tests_properties("ctlrender-invalid-input-filename-exr" PROPERTIES WILL_FAIL TRUE)
endif()

# Test for invalid output file names
add_test(NAME "ctlrender-invalid-output-filename-dpx" COMMAND ctlrender -force -format dpx -ctl "${TEST_FILES}/unity.ctl" "${TEST_FILES}/bars_nuke_8_be.dpx" "${CTLRENDER_OUTPUT_FOLDER}/missing_folder/invalid_filename_out.dpx")
set_tests_properties("ctlrender-invalid-output-filename-dpx" PROPERTIES WILL_FAIL TRUE)
if(TARGET TIFF::TIFF)
    add_test(NAME "ctlrender-invalid-output-filename-tiff" COMMAND ctlrender -force -format tiff -ctl "${TEST_FILES}/unity.ctl" "${TEST_FILES}/bars_photoshop_8_be_interleaved.tif" "${CTLRENDER_OUTPUT_FOLDER}/missing_folder/invalid_filename_out.tif")
    set_tests_properties("ctlrender-invalid-output-filename-tiff" PROPERTIES WILL_FAIL TRUE)
endif()
if(OpenEXR_FOUND)
    add_test(NAME "ctlrender-invalid-output-filename-exr" COMMAND ctlrender -force -format exr -ctl "${TEST_FILES}/unity.ctl" "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/missing_folder/invalid_filename_out.exr")
    set_tests_properties("ctlrender-invalid-output-filename-exr" PROPERTIES WILL_FAIL TRUE)
endif()

# test for maxInstCount exception
add_test(NAME "ctlrender-maxInstCount-exception" COMMAND ctlrender -force -ctl "${PROJECT_SOURCE_DIR}/resources/test/ctl/throw_maxInstCount_exception.ctl" "${PROJECT_SOURCE_DIR}/resources/test/exr/colorbars_nuke_rgb_exr16.exr" "${CTLRENDER_OUTPUT_FOLDER}/maxInstCount_exception.exr")
set_tests_properties("ctlrender-maxInstCount-exception" PROPERTIES WILL_FAIL TRUE)

# -pixel mode: one pixel on CLI, result to stdout, no file I/O.
#
# Input RGBA = (0.5, 0.5, 0.5, 1.0); pixel_chain_a.ctl adds 0.25 to R;
# pixel_chain_b.ctl multiplies G by 2 and halves A.  Expected:
#   single a:    R=0.75 G=0.5  B=0.5 A=1.0
#   chain a to b:   R=0.75 G=1.0  B=0.5 A=0.5
# Tests assert via PASS_REGULAR_EXPRESSION on stdout.
add_test(NAME "ctlrender-pixel-rgba-single"
    COMMAND ctlrender
        -ctl "${TEST_FILES}/pixel_chain_a.ctl"
        -pixel 0.5,0.5,0.5,1.0)
set_tests_properties("ctlrender-pixel-rgba-single" PROPERTIES
    PASS_REGULAR_EXPRESSION "R=0\\.7500000[ \\t]+G=0\\.5000000[ \\t]+B=0\\.5000000[ \\t]+A=1\\.0000000")

add_test(NAME "ctlrender-pixel-rgba-chain"
    COMMAND ctlrender
        -ctl "${TEST_FILES}/pixel_chain_a.ctl"
        -ctl "${TEST_FILES}/pixel_chain_b.ctl"
        -pixel 0.5,0.5,0.5,1.0)
set_tests_properties("ctlrender-pixel-rgba-chain" PROPERTIES
    PASS_REGULAR_EXPRESSION "R=0\\.7500000[ \\t]+G=1\\.0000000[ \\t]+B=0\\.5000000[ \\t]+A=0\\.5000000")

# RGB (3 values) depth-3 input; expected stdout has no 'A=' component.
add_test(NAME "ctlrender-pixel-rgb-only"
    COMMAND ctlrender
        -ctl "${TEST_FILES}/pixel_rgb_only.ctl"
        -pixel 0.1,0.2,0.3)
set_tests_properties("ctlrender-pixel-rgb-only" PROPERTIES
    PASS_REGULAR_EXPRESSION "^R=0\\.2000000[ \\t]+G=0\\.4000000[ \\t]+B=0\\.6000000[ \\t]*\n?$")

# Error: -pixel without -ctl must fail.
add_test(NAME "ctlrender-pixel-no-ctl-errors"
    COMMAND ctlrender -pixel 0.1,0.2,0.3,1.0)
set_tests_properties("ctlrender-pixel-no-ctl-errors" PROPERTIES WILL_FAIL TRUE)

# Error: -pixel mixed with a positional input file must fail.
add_test(NAME "ctlrender-pixel-positional-conflict-errors"
    COMMAND ctlrender
        -ctl "${TEST_FILES}/pixel_chain_a.ctl"
        -pixel 0.1,0.2,0.3,1.0
        "${TEST_FILES}/colorbars_nuke_rgba_exr32.exr")
set_tests_properties("ctlrender-pixel-positional-conflict-errors" PROPERTIES WILL_FAIL TRUE)

# Error: non-numeric value in -pixel must fail.
add_test(NAME "ctlrender-pixel-malformed-errors"
    COMMAND ctlrender -ctl "${TEST_FILES}/pixel_chain_a.ctl" -pixel 0.1,abc,0.3)
set_tests_properties("ctlrender-pixel-malformed-errors" PROPERTIES WILL_FAIL TRUE)

# Error: too few values (needs 3 or 4).
add_test(NAME "ctlrender-pixel-too-few-errors"
    COMMAND ctlrender -ctl "${TEST_FILES}/pixel_chain_a.ctl" -pixel 0.1,0.2)
set_tests_properties("ctlrender-pixel-too-few-errors" PROPERTIES WILL_FAIL TRUE)

# -pixel + -param1: uniform parameter override on the single-pixel path.
# pixel_with_param.ctl scales R by `scale` (default 1.0).  Default test
# verifies the default propagates; override test verifies -param1 reaches
# the pixel path the same way it reaches the image path.
add_test(NAME "ctlrender-pixel-param-default"
    COMMAND ctlrender
        -ctl "${TEST_FILES}/pixel_with_param.ctl"
        -pixel 0.5,0.6,0.7,1.0)
set_tests_properties("ctlrender-pixel-param-default" PROPERTIES
    PASS_REGULAR_EXPRESSION "R=0\\.5000000[ \\t]+G=0\\.6000000[ \\t]+B=0\\.7000000[ \\t]+A=1\\.0000000")

add_test(NAME "ctlrender-pixel-param-override"
    COMMAND ctlrender
        -ctl "${TEST_FILES}/pixel_with_param.ctl"
        -param1 scale 2.0
        -pixel 0.5,0.6,0.7,1.0)
set_tests_properties("ctlrender-pixel-param-override" PROPERTIES
    PASS_REGULAR_EXPRESSION "R=1\\.0000000[ \\t]+G=0\\.6000000[ \\t]+B=0\\.7000000[ \\t]+A=1\\.0000000")

# -pixel HDR-range inputs: negative and large positive values must pass
# through the SIMD interpreter unclipped.
add_test(NAME "ctlrender-pixel-hdr-negative"
    COMMAND ctlrender
        -ctl "${TEST_FILES}/pixel_chain_a.ctl"
        -pixel -0.5,0.6,0.7,1.0)
set_tests_properties("ctlrender-pixel-hdr-negative" PROPERTIES
    PASS_REGULAR_EXPRESSION "R=-0\\.2500000[ \\t]+G=0\\.6000000[ \\t]+B=0\\.7000000[ \\t]+A=1\\.0000000")

add_test(NAME "ctlrender-pixel-hdr-large"
    COMMAND ctlrender
        -ctl "${TEST_FILES}/pixel_chain_a.ctl"
        -pixel 10.0,0.6,0.7,1.0)
set_tests_properties("ctlrender-pixel-hdr-large" PROPERTIES
    PASS_REGULAR_EXPRESSION "R=10\\.2500000[ \\t]+G=0\\.6000000[ \\t]+B=0\\.7000000[ \\t]+A=1\\.0000000")

# Error: -param1 before any -ctl must fail (parser invariant).
add_test(NAME "ctlrender-pixel-param-before-ctl-errors"
    COMMAND ctlrender
        -param1 scale 2.0
        -ctl "${TEST_FILES}/pixel_with_param.ctl"
        -pixel 0.5,0.6,0.7,1.0)
set_tests_properties("ctlrender-pixel-param-before-ctl-errors" PROPERTIES WILL_FAIL TRUE)

# Phase B: tile-parallel dispatch must produce byte-identical output for
# any -threads N on a fixture that spans multiple tiles (maxSamples=4096,
# so >4096 pixels). Runs the same transform at several thread counts and
# compares the resulting EXRs byte-for-byte against the N=1 reference.
if(OpenEXR_FOUND)
    set(THREADED_PARITY_REF "${CTLRENDER_OUTPUT_FOLDER}/threaded_parity_ref.exr")
    add_test(NAME "ctlrender-threaded-parity-ref"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                -format exr32 -force -threads 1
                "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr"
                "${THREADED_PARITY_REF}")
    foreach(N 2 4 8 12)
        set(OUT "${CTLRENDER_OUTPUT_FOLDER}/threaded_parity_t${N}.exr")
        add_test(NAME "ctlrender-threaded-parity-t${N}-run"
            COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                    -format exr32 -force -threads ${N}
                    "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr"
                    "${OUT}")
        add_test(NAME "ctlrender-threaded-parity-t${N}-cmp"
            COMMAND ${CMAKE_COMMAND} -E compare_files
                    "${THREADED_PARITY_REF}" "${OUT}")
        set_tests_properties("ctlrender-threaded-parity-t${N}-run"
            PROPERTIES DEPENDS "ctlrender-threaded-parity-ref")
        set_tests_properties("ctlrender-threaded-parity-t${N}-cmp"
            PROPERTIES DEPENDS "ctlrender-threaded-parity-t${N}-run")
    endforeach()
endif()

# exr16 (half) output threaded parity: the float->half conversion in
# exr_file.cc has its own work-stealing parallelization.  The fixture is
# below the kMinToThread threshold so the parallel path itself only fires
# on larger inputs, but this matrix still pins serial-vs-threaded byte
# parity through the whole exr16 write pipeline (interpreter threading,
# format conversion, OpenEXR compression).
if(OpenEXR_FOUND)
    set(EXR16_PARITY_REF "${CTLRENDER_OUTPUT_FOLDER}/exr16_parity_ref.exr")
    add_test(NAME "ctlrender-exr16-threaded-parity-ref"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                -format exr16 -force -threads 1
                "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr"
                "${EXR16_PARITY_REF}")
    foreach(N 2 4 8)
        set(OUT "${CTLRENDER_OUTPUT_FOLDER}/exr16_parity_t${N}.exr")
        add_test(NAME "ctlrender-exr16-threaded-parity-t${N}-run"
            COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                    -format exr16 -force -threads ${N}
                    "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr"
                    "${OUT}")
        add_test(NAME "ctlrender-exr16-threaded-parity-t${N}-cmp"
            COMMAND ${CMAKE_COMMAND} -E compare_files
                    "${EXR16_PARITY_REF}" "${OUT}")
        set_tests_properties("ctlrender-exr16-threaded-parity-t${N}-run"
            PROPERTIES DEPENDS "ctlrender-exr16-threaded-parity-ref")
        set_tests_properties("ctlrender-exr16-threaded-parity-t${N}-cmp"
            PROPERTIES DEPENDS "ctlrender-exr16-threaded-parity-t${N}-run")
    endforeach()
endif()

# -threads x -compression interaction: each compression codec sees a
# different chunk-size / encoder, and the per-tile pixel batches arrive
# from worker threads.  Verify byte-identical output across N=1 vs N=4.
#
# Lossless codecs only (block and wavelet).  Byte-identity here is a
# statement about the pixels ctlrender hands to OpenEXR, and a lossless
# codec turns any difference in those pixels into a difference in the file.
# dwaa was in this list, but a lossy DCT encoder is not required to be
# reproducible between two separate invocations, and on armv7 it is not:
# the file differs while zip, zips and piz all match, which is precisely
# the evidence that the pixels are identical and the variation is inside
# OpenEXR.  DWAA itself stays covered by the codec test above.
if(OpenEXR_FOUND)
    foreach(C zip zips piz)
        set(REF "${CTLRENDER_OUTPUT_FOLDER}/threads_compression_${C}_ref.exr")
        set(OUT "${CTLRENDER_OUTPUT_FOLDER}/threads_compression_${C}_t4.exr")
        add_test(NAME "ctlrender-threads-compression-${C}-ref"
            COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                    -format exr32 -force -threads 1 -compression ${C}
                    "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr"
                    "${REF}")
        add_test(NAME "ctlrender-threads-compression-${C}-t4-run"
            COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                    -format exr32 -force -threads 4 -compression ${C}
                    "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr"
                    "${OUT}")
        add_test(NAME "ctlrender-threads-compression-${C}-t4-cmp"
            COMMAND ${CMAKE_COMMAND} -E compare_files "${REF}" "${OUT}")
        set_tests_properties("ctlrender-threads-compression-${C}-t4-run"
            PROPERTIES DEPENDS "ctlrender-threads-compression-${C}-ref")
        set_tests_properties("ctlrender-threads-compression-${C}-t4-cmp"
            PROPERTIES DEPENDS "ctlrender-threads-compression-${C}-t4-run")
    endforeach()
endif()

# Error-path tests for CLI flag parsing in ctlrender/main.cc.  Each
# verifies that the parser exits non-zero on a specific malformed
# invocation -- they exercise distinct error branches that were
# previously unreachable from any other test.
if(OpenEXR_FOUND)
    set(_FX "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr")
    set(_OUT "${CTLRENDER_OUTPUT_FOLDER}")

    foreach(EBASE
            "input-scale-no-arg;-input_scale"
            "input-scale-malformed;-input_scale;abc;-ctl;${TEST_FILES}/unity.ctl;${_FX};${_OUT}/x.exr"
            "output-scale-malformed;-output_scale;abc;-ctl;${TEST_FILES}/unity.ctl;${_FX};${_OUT}/x.exr"
            "jobs-no-arg;-jobs"
            "jobs-malformed;-jobs;abc;-ctl;${TEST_FILES}/unity.ctl;${_FX};${_OUT}/x.exr"
            "format-unknown;-format;mystery;-ctl;${TEST_FILES}/unity.ctl;${_FX};${_OUT}/x.mystery"
            "global-param1-no-arg;-global_param1"
            "unknown-option;-nonsense;-ctl;${TEST_FILES}/unity.ctl;${_FX};${_OUT}/x.exr")
        # list(POP_FRONT) needs CMake 3.15 and this project declares 3.12.
        list(GET EBASE 0 _NAME)
        list(REMOVE_AT EBASE 0)
        add_test(NAME "ctlrender-${_NAME}-errors" COMMAND ${CTLRENDER_PATH} ${EBASE})
        set_tests_properties("ctlrender-${_NAME}-errors" PROPERTIES WILL_FAIL TRUE)
    endforeach()

    # Semantic verification by byte-parity: every flag below is checked
    # against an equivalent reference invocation that produces the same
    # output through a different code path.  A regression that silently
    # ignored or mis-applied the flag would change the bytes and the
    # compare_files step would fail.

    # -input_scale 0.5 + unity is mathematically equivalent to
    # image_with_param.ctl with gain=0.5 and no input scaling.  Both
    # multiply every input lane by 0.5 before output; output should be
    # byte-identical exr32.
    add_test(NAME "ctlrender-input-scale-ref"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/image_with_param.ctl" -p1 gain 0.5
                -format exr32 -force "${_FX}" "${_OUT}/scale_ref.exr")
    add_test(NAME "ctlrender-input-scale-run"
        COMMAND ${CTLRENDER_PATH} -input_scale 0.5 -ctl "${TEST_FILES}/unity.ctl"
                -format exr32 -force "${_FX}" "${_OUT}/scale_run.exr")
    add_test(NAME "ctlrender-input-scale-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/scale_ref.exr" "${_OUT}/scale_run.exr")
    set_tests_properties("ctlrender-input-scale-run" PROPERTIES DEPENDS "ctlrender-input-scale-ref")
    set_tests_properties("ctlrender-input-scale-cmp" PROPERTIES DEPENDS "ctlrender-input-scale-run")

    # -input_scale through a NON-unity chain: verifies the scale is
    # applied to the input BEFORE the CTL chain runs (not after, not
    # silently ignored).  -input_scale 0.5 then gain=2.0 cancels out to
    # identity, so the output must be byte-equal to running unity on
    # the unscaled input.  A bug applying input_scale at the wrong
    # pipeline point (or dropping it through chained transforms) would
    # change the bytes.
    add_test(NAME "ctlrender-input-scale-nonunity-ref"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                -format exr32 -force "${_FX}" "${_OUT}/scale_nu_ref.exr")
    add_test(NAME "ctlrender-input-scale-nonunity-run"
        COMMAND ${CTLRENDER_PATH} -input_scale 0.5
                -ctl "${TEST_FILES}/image_with_param.ctl" -p1 gain 2.0
                -format exr32 -force "${_FX}" "${_OUT}/scale_nu_run.exr")
    add_test(NAME "ctlrender-input-scale-nonunity-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/scale_nu_ref.exr" "${_OUT}/scale_nu_run.exr")
    set_tests_properties("ctlrender-input-scale-nonunity-run"
        PROPERTIES DEPENDS "ctlrender-input-scale-nonunity-ref")
    set_tests_properties("ctlrender-input-scale-nonunity-cmp"
        PROPERTIES DEPENDS "ctlrender-input-scale-nonunity-run")

    # -output_scale: implemented in exr_write as `pixel / scale` (the
    # factor "denormalises" pixel values on the way out, mirroring
    # input_scale's `pixel *= scale` on the way in).  So pairing
    # in-chain gain=0.5 with -output_scale 0.5 gives (R * 0.5) / 0.5 = R,
    # i.e. byte-identical to a unity-equivalent reference.  Uses the
    # float-typed fixture so intermediate values stay in float -- half
    # quantization between ops would break byte parity even for
    # algebraically equivalent compositions.
    add_test(NAME "ctlrender-scale-float-ref"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/image_with_param_float.ctl" -p1 gain 1.0
                -format exr32 -force "${_FX}" "${_OUT}/scale_f_ref.exr")
    add_test(NAME "ctlrender-output-scale-run"
        COMMAND ${CTLRENDER_PATH} -output_scale 0.5
                -ctl "${TEST_FILES}/image_with_param_float.ctl" -p1 gain 0.5
                -format exr32 -force "${_FX}" "${_OUT}/oscale_run.exr")
    add_test(NAME "ctlrender-output-scale-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/scale_f_ref.exr" "${_OUT}/oscale_run.exr")
    set_tests_properties("ctlrender-output-scale-run"
        PROPERTIES DEPENDS "ctlrender-scale-float-ref")
    set_tests_properties("ctlrender-output-scale-cmp"
        PROPERTIES DEPENDS "ctlrender-output-scale-run")

    # Both scales together: -input_scale 0.5 + chain -p1 gain 4.0 +
    # -output_scale 2.0 == identity:  ((R * 0.5) * 4.0) / 2.0 = R.
    # Pins the interaction between input_scale (read-side multiply),
    # in-chain math, and output_scale (write-side divide) all at once.
    add_test(NAME "ctlrender-both-scales-run"
        COMMAND ${CTLRENDER_PATH} -input_scale 0.5 -output_scale 2.0
                -ctl "${TEST_FILES}/image_with_param_float.ctl" -p1 gain 4.0
                -format exr32 -force "${_FX}" "${_OUT}/both_scales_run.exr")
    add_test(NAME "ctlrender-both-scales-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/scale_f_ref.exr" "${_OUT}/both_scales_run.exr")
    set_tests_properties("ctlrender-both-scales-run"
        PROPERTIES DEPENDS "ctlrender-scale-float-ref")
    set_tests_properties("ctlrender-both-scales-cmp"
        PROPERTIES DEPENDS "ctlrender-both-scales-run")

    # -global_param1 gain V applies to every -ctl in the chain.  With a
    # single -ctl image_with_param.ctl, it should be byte-equivalent to
    # the same chain with -p1 gain V (per-operation override).
    add_test(NAME "ctlrender-global-param1-ref"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/image_with_param.ctl" -p1 gain 2.0
                -format exr32 -force "${_FX}" "${_OUT}/gp1_ref.exr")
    add_test(NAME "ctlrender-global-param1-run"
        COMMAND ${CTLRENDER_PATH} -global_param1 gain 2.0 -ctl "${TEST_FILES}/image_with_param.ctl"
                -format exr32 -force "${_FX}" "${_OUT}/gp1_run.exr")
    add_test(NAME "ctlrender-global-param1-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/gp1_ref.exr" "${_OUT}/gp1_run.exr")
    set_tests_properties("ctlrender-global-param1-run" PROPERTIES DEPENDS "ctlrender-global-param1-ref")
    set_tests_properties("ctlrender-global-param1-cmp" PROPERTIES DEPENDS "ctlrender-global-param1-run")

    # -p1 alias: byte-identical to -param1 dispatch.
    add_test(NAME "ctlrender-p1-alias-ref"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/image_with_param.ctl" -param1 gain 0.75
                -format exr32 -force "${_FX}" "${_OUT}/p1_ref.exr")
    add_test(NAME "ctlrender-p1-alias-run"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/image_with_param.ctl" -p1 gain 0.75
                -format exr32 -force "${_FX}" "${_OUT}/p1_run.exr")
    add_test(NAME "ctlrender-p1-alias-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/p1_ref.exr" "${_OUT}/p1_run.exr")
    set_tests_properties("ctlrender-p1-alias-run" PROPERTIES DEPENDS "ctlrender-p1-alias-ref")
    set_tests_properties("ctlrender-p1-alias-cmp" PROPERTIES DEPENDS "ctlrender-p1-alias-run")

    # Image-chain mode: unity.ctl + image_with_param.ctl(gain=1.5) is
    # mathematically equivalent to a single image_with_param.ctl(gain=1.5).
    # If the chain executor is broken (drops a step, double-applies),
    # the bytes differ.
    add_test(NAME "ctlrender-image-multi-ctl-ref"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/image_with_param.ctl" -p1 gain 1.5
                -format exr32 -force "${_FX}" "${_OUT}/chain_ref.exr")
    add_test(NAME "ctlrender-image-multi-ctl-run"
        COMMAND ${CTLRENDER_PATH}
                -ctl "${TEST_FILES}/unity.ctl"
                -ctl "${TEST_FILES}/image_with_param.ctl" -p1 gain 1.5
                -format exr32 -force "${_FX}" "${_OUT}/chain_run.exr")
    add_test(NAME "ctlrender-image-multi-ctl-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/chain_ref.exr" "${_OUT}/chain_run.exr")
    set_tests_properties("ctlrender-image-multi-ctl-run" PROPERTIES DEPENDS "ctlrender-image-multi-ctl-ref")
    set_tests_properties("ctlrender-image-multi-ctl-cmp" PROPERTIES DEPENDS "ctlrender-image-multi-ctl-run")

    # -threads autoselect (-threads 0) and -threads >> tiles must be
    # byte-identical to -threads 1 -- same code semantically, different
    # number of workers.  These pin transform.cc's autoselect resolution
    # and the worker-count > tile-count cap path against silent drift.
    add_test(NAME "ctlrender-threads-autoselect-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/threaded_parity_ref.exr" "${_OUT}/threads_auto.exr")
    add_test(NAME "ctlrender-threads-autoselect-run"
        COMMAND ${CTLRENDER_PATH} -threads 0 -ctl "${TEST_FILES}/unity.ctl"
                -format exr32 -force "${_FX}" "${_OUT}/threads_auto.exr")
    set_tests_properties("ctlrender-threads-autoselect-run"
        PROPERTIES DEPENDS "ctlrender-threaded-parity-ref")
    set_tests_properties("ctlrender-threads-autoselect-cmp"
        PROPERTIES DEPENDS "ctlrender-threads-autoselect-run")

    add_test(NAME "ctlrender-threads-more-than-tiles-run"
        COMMAND ${CTLRENDER_PATH} -threads 64 -ctl "${TEST_FILES}/unity.ctl"
                -format exr32 -force "${_FX}" "${_OUT}/threads_64.exr")
    add_test(NAME "ctlrender-threads-more-than-tiles-cmp"
        COMMAND ${CMAKE_COMMAND} -E compare_files
                "${_OUT}/threaded_parity_ref.exr" "${_OUT}/threads_64.exr")
    set_tests_properties("ctlrender-threads-more-than-tiles-run"
        PROPERTIES DEPENDS "ctlrender-threaded-parity-ref")
    set_tests_properties("ctlrender-threads-more-than-tiles-cmp"
        PROPERTIES DEPENDS "ctlrender-threads-more-than-tiles-run")

    # Multi-input batch + -jobs N: each output file must be byte-identical
    # to the same input processed individually with -jobs 1.  Catches per-
    # job state-bleed regressions and missing output files.
    set(_MULTI_OUT "${_OUT}/multi_in")
    add_test(NAME "ctlrender-multi-input-mkdir"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_MULTI_OUT}")
    foreach(IN_FILE
            colorbars_nuke_rgb_exr16
            colorbars_nuke_rgb_exr32
            bars_photoshop)
        add_test(NAME "ctlrender-multi-input-${IN_FILE}-ref"
            COMMAND ${CTLRENDER_PATH} -jobs 1 -ctl "${TEST_FILES}/unity.ctl" -format exr32 -force
                    "${TEST_FILES}/${IN_FILE}.exr" "${_OUT}/single_${IN_FILE}.exr")
    endforeach()
    add_test(NAME "ctlrender-multi-input-jobs2-batch"
        COMMAND ${CTLRENDER_PATH} -jobs 2 -ctl "${TEST_FILES}/unity.ctl" -format exr32 -force
                "${TEST_FILES}/colorbars_nuke_rgb_exr16.exr"
                "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr"
                "${TEST_FILES}/bars_photoshop.exr"
                "${_MULTI_OUT}")
    set_tests_properties("ctlrender-multi-input-jobs2-batch"
        PROPERTIES DEPENDS "ctlrender-multi-input-mkdir")
    foreach(IN_FILE
            colorbars_nuke_rgb_exr16
            colorbars_nuke_rgb_exr32
            bars_photoshop)
        add_test(NAME "ctlrender-multi-input-${IN_FILE}-cmp"
            COMMAND ${CMAKE_COMMAND} -E compare_files
                    "${_OUT}/single_${IN_FILE}.exr" "${_MULTI_OUT}/${IN_FILE}.exr")
        set_tests_properties("ctlrender-multi-input-${IN_FILE}-cmp"
            PROPERTIES DEPENDS "ctlrender-multi-input-jobs2-batch;ctlrender-multi-input-${IN_FILE}-ref")
    endforeach()

    # Force-overwrite negative test: without -force, attempting to
    # overwrite an existing file must error out.  Depends on the
    # autoselect run above to guarantee the target file already exists.
    add_test(NAME "ctlrender-write-existing-no-force-errors"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl" -format exr32
                "${_FX}" "${_OUT}/threads_auto.exr")
    set_tests_properties("ctlrender-write-existing-no-force-errors"
        PROPERTIES DEPENDS "ctlrender-threads-autoselect-run" WILL_FAIL TRUE)

    # Property-based smoke test: 8 randomly-generated CTL programs run
    # through ctlrender at -threads 1 vs -threads 4 must produce byte-
    # identical EXR output.  The full sweep (--count 100+) is meant to
    # be invoked manually via the script directly; this CI smoke entry
    # uses a small fixed-seed count so it runs in <2s.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME "ctlrender-property-vector-parity-smoke"
            COMMAND ${Python3_EXECUTABLE}
                "${PROJECT_SOURCE_DIR}/tools/property_test_simd_parity.py"
                # Not CTLRENDER_PATH: that is a hardcoded path with neither the
                # per-config directory nor the .exe suffix Windows needs.  ctest
                # fills those in when it launches a command itself, but this
                # path is an argument that Python hands to a child process.
                --ctlrender "$<TARGET_FILE:ctlrender>"
                --input    "${TEST_FILES}/colorbars_nuke_rgb_exr32.exr"
                --workdir  "${_OUT}/property_smoke"
                --count 8
                --seed 0)

        # Parallel float->half coverage.  exr_file.cc's parallel
        # conversion only fires above kMinToThread = 128 KiB of floats
        # (~131072 elements).  None of the checked-in fixtures are
        # that large, so the parallel path was untested in CI.  Build
        # a small generator (linked against OpenEXR) that emits a 512x512x3
        # (786K float) fixture at test time.
        set(_LARGE_EXR "${_OUT}/large_512x512.exr")
        add_test(NAME "ctlrender-large-fixture-gen"
            COMMAND $<TARGET_FILE:gen_large_fixture>
                "${_LARGE_EXR}" 512 512)

        # Reference: SERIAL float->half conversion done in a separate
        # binary that doesn't go through exr_file.cc's parallel code
        # path at all.  This is the only way to get a true-serial
        # reference: ctlrender's `-threads 1` flag controls the
        # tile-parallel layer, NOT the parallel float->half code, which
        # always uses hardware_concurrency() workers when the input is
        # above kMinToThread.  Using compression=none so the byte-
        # parity comparison sees raw pixel bytes (no codec smoothing
        # over single-pixel differences).
        add_test(NAME "ctlrender-parallel-half-ref"
            COMMAND $<TARGET_FILE:gen_serial_half_ref>
                "${_LARGE_EXR}"
                "${_OUT}/large_half_serial.exr"
                "none")
        set_tests_properties("ctlrender-parallel-half-ref"
            PROPERTIES DEPENDS "ctlrender-large-fixture-gen")

        # ctlrender's parallel float->half path must produce byte-
        # identical output to the serial reference above.  -compression
        # none so any single-pixel divergence in the parallel path is
        # observable at the byte level.
        add_test(NAME "ctlrender-parallel-half-run-t8"
            COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                    -format exr16 -force -compression none -threads 8
                    "${_LARGE_EXR}"
                    "${_OUT}/large_half_t8.exr")
        add_test(NAME "ctlrender-parallel-half-cmp-t8"
            COMMAND ${CMAKE_COMMAND} -E compare_files
                    "${_OUT}/large_half_serial.exr"
                    "${_OUT}/large_half_t8.exr")
        set_tests_properties("ctlrender-parallel-half-run-t8"
            PROPERTIES DEPENDS "ctlrender-large-fixture-gen")
        set_tests_properties("ctlrender-parallel-half-cmp-t8"
            PROPERTIES DEPENDS
                "ctlrender-parallel-half-ref;ctlrender-parallel-half-run-t8")

        # Same at -threads 4: different ctlrender tile-parallelism but
        # same parallel float->half (always hardware_concurrency()).
        # Must still match the serial reference byte-for-byte.
        add_test(NAME "ctlrender-parallel-half-run-t4"
            COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                    -format exr16 -force -compression none -threads 4
                    "${_LARGE_EXR}"
                    "${_OUT}/large_half_t4.exr")
        add_test(NAME "ctlrender-parallel-half-cmp-t4"
            COMMAND ${CMAKE_COMMAND} -E compare_files
                    "${_OUT}/large_half_serial.exr"
                    "${_OUT}/large_half_t4.exr")
        set_tests_properties("ctlrender-parallel-half-run-t4"
            PROPERTIES DEPENDS "ctlrender-large-fixture-gen")
        set_tests_properties("ctlrender-parallel-half-cmp-t4"
            PROPERTIES DEPENDS
                "ctlrender-parallel-half-ref;ctlrender-parallel-half-run-t4")
    endif()
endif()

# ACES round-trip: ACES to EXR via unity.  The existing test produces an
# .aces file from an EXR input; this reads that .aces back through the
# pipeline as a cross-check that aces_file.cc's read path works on its
# own writer's output.
if (OpenEXR_FOUND AND AcesContainer_FOUND)
    add_test(NAME "ctlrender-ACES->EXR-roundtrip"
        COMMAND ${CTLRENDER_PATH} -ctl "${TEST_FILES}/unity.ctl"
                -format exr32 -force
                "${CTLRENDER_OUTPUT_FOLDER}/bars_rgb_exr16_to_aces.aces"
                "${CTLRENDER_OUTPUT_FOLDER}/aces_roundtrip.exr")
    set_tests_properties("ctlrender-ACES->EXR-roundtrip"
        PROPERTIES DEPENDS "ctlrender-EXR16->ACES")
endif()
