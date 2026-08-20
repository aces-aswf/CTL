cmake_minimum_required(VERSION 3.12)
project(CTL VERSION 1.5.5 
  DESCRIPTION "Color Transformation Language (CTL) reference implementation"
  LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_MODULE_PATH
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
	${CMAKE_MODULE_PATH}
)

add_definitions( -DPACKAGE="${PROJECT_NAME}" -DVERSION="${PROJECT_VERSION}" )

option(CTL_BUILD_TESTS "Build the unit tests" ON)
option(CTL_BUILD_TOOLS "Build the utility commands (ctlrender, etc)" ON)
option(CTL_BUILD_MODULETEST "Build ctltest, the module-level test framework" ON)
option(BUILD_SHARED_LIBS "Build all libraries as shared" OFF )

set(CMAKE_CXX_FLAGS_ASAN
  "-fsanitize=address -fno-optimize-sibling-calls -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g -O1"
  CACHE STRING "Flags used by the C++ compiler during AddressSanitizer builds."
  FORCE)

if( NOT DEFINED CMAKE_BUILD_TYPE )
  MESSAGE( STATUS "To use Release mode, use \"cmake .. -DCMAKE_BUILD_TYPE=Release\"" )
  MESSAGE( STATUS "To use AddressSanitizer, use \"cmake .. -DCMAKE_BUILD_TYPE=asan\"" )
elseif( CMAKE_BUILD_TYPE STREQUAL "Release" )
  MESSAGE( STATUS "Using Release mode - CMAKE_BUILD_TYPE : \"${CMAKE_BUILD_TYPE}\"")
elseif( CMAKE_BUILD_TYPE STREQUAL "asan" )
  MESSAGE( STATUS "Using AddressSanitizer - CMAKE_BUILD_TYPE : \"${CMAKE_BUILD_TYPE}\"")
else()
  MESSAGE( STATUS "CMAKE_BUILD_TYPE : \"${CMAKE_BUILD_TYPE}\"")
  MESSAGE( STATUS "To use Release mode, use \"cmake .. -DCMAKE_BUILD_TYPE=Release\"" )
  MESSAGE( STATUS "To use AddressSanitizer, use \"cmake .. -DCMAKE_BUILD_TYPE=asan\"" )
endif()

# Set a default build type if none was specified
set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# ZLIB is automatically added by OpenEXR 3.x but needs to be added for OpenEXR 2.x
find_package(ZLIB)
if(TARGET ZLIB::ZLIB)
	message( STATUS "found ZLIB" )
	message( STATUS "	ZLIB_VERSION : ${ZLIB_VERSION}" )
	message( STATUS "	ZLIB_INCLUDE_DIRS : ${ZLIB_INCLUDE_DIRS}" )
	message( STATUS "	ZLIB_LIBRARIES : ${ZLIB_LIBRARIES}" )
	message( STATUS "	ZLIB_INCLUDE_DIR : ${ZLIB_INCLUDE_DIR}" )
	message( STATUS "	ZLIB_LIBRARY : ${ZLIB_LIBRARY}" )
else()
  message( STATUS "ZLIB was not found")
  message( STATUS " ZLIB is automatically linked when using OpenEXR 3.x but CTL needs to link to ZLIB if using OpenEXR 2.x" )
endif()

# find OpenEXR
find_package(OpenEXR 3 CONFIG QUIET)
if(OpenEXR_FOUND)
  set(OpenEXR_FOUND_WITH_CONFIG TRUE BOOL "found OpenEXR with Config")
else()
  message(STATUS "OpenEXR v3 not found, looking for IlmBase and OpenEXR v2")
  find_package(IlmBase REQUIRED)
  find_package(OpenEXR 2 REQUIRED)
endif()
 
if(OpenEXR_FOUND)
  message(STATUS "Found OpenEXR ${OpenEXR_VERSION}")
  set(USE_OPENEXR TRUE CACHE BOOL "Add OpenEXR support")
  set(OPENEXR_VERSION_MAJOR ${OpenEXR_VERSION_MAJOR} CACHE STRING "OpenEXR major version")
  set(OPENEXR_VERSION_MINOR ${OpenEXR_VERSION_MINOR} CACHE STRING "OpenEXR minor version")
  set(OPENEXR_VERSION_PATCH ${OpenEXR_VERSION_PATCH} CACHE STRING "OpenEXR patch version")
  if(OpenEXR_FOUND_WITH_CONFIG)
    message(STATUS " found OpenEXR with Config")
  else(OpenEXR_FOUND_WITH_CONFIG)
    message(STATUS " found OpenEXR with FindModule")
  endif(OpenEXR_FOUND_WITH_CONFIG)
else(OpenEXR_FOUND)
  message(ERROR "OpenEXR is a required dependency but it has not been found")
endif(OpenEXR_FOUND)

if(IlmBase_FOUND)
  message(STATUS "IlmBase info:")
  message(STATUS "       IlmBase_VERSION = ${IlmBase_VERSION}")
  message(STATUS "   IlmBase_INCLUDE_DIR = ${IlmBase_INCLUDE_DIR}")
  message(STATUS "  IlmBase_INCLUDE_DIRS = ${IlmBase_INCLUDE_DIRS}")
  message(STATUS "       IlmBase_LIBRARY = ${IlmBase_LIBRARY}")
  message(STATUS "     IlmBase_LIBRARIES = ${IlmBase_LIBRARIES}")
endif(IlmBase_FOUND)

if(OpenEXR_FOUND)
  message(STATUS "OpenEXR info:")
  message(STATUS "       OpenEXR_VERSION = ${OpenEXR_VERSION}")
  if( NOT OpenEXR_FOUND_WITH_CONFIG)
    message(STATUS "   OpenEXR_INCLUDE_DIR = ${OpenEXR_INCLUDE_DIR}")
    message(STATUS "  OpenEXR_INCLUDE_DIRS = ${OpenEXR_INCLUDE_DIRS}")
    message(STATUS "       OpenEXR_LIBRARY = ${OpenEXR_LIBRARY}")
    message(STATUS "     OpenEXR_LIBRARIES = ${OpenEXR_LIBRARIES}")
  endif(NOT OpenEXR_FOUND_WITH_CONFIG)
endif(OpenEXR_FOUND)

add_subdirectory(lib)
add_subdirectory(doc)
if (CTL_BUILD_TOOLS)
  add_subdirectory(ctlrender)
  add_subdirectory(OpenEXR_CTL)
endif()

if (CTL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(unittest)
endif()

if (CTL_BUILD_MODULETEST)
  if (NOT CTL_BUILD_TESTS)
    enable_testing()
  endif()
  add_subdirectory(moduletest)
endif()

