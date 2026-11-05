# moduletest/ - ctltest framework for user-authored CTL modules
#
# Layout:
#   lib/         ctltest_core static library (case model, loader, runner, oracles)
#   driver/      ctltest_run_one, internal binary invoked by ctest
#   cli/         ctltest, installed alongside ctlrender (v0.4+)
#   cmake/       CtlTestDiscover.cmake, exposes ctltest_add_yaml(path)
#   tests/       manifest.txt + self-test YAMLs + CTL fixtures
#
# Dependency: yaml-cpp (find_package preferred, FetchContent fallback).

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CtlTestDiscover.cmake)

option(CTLTEST_FETCH_YAML_CPP
  "If yaml-cpp is not found via find_package, fetch and build it via FetchContent"
  ON)

# FetchContent clones with git; probe for it up front so a missing git takes
# the skip branch below instead of failing inside ExternalProject.
find_package(Git QUIET)

find_package(yaml-cpp CONFIG QUIET)
if(yaml-cpp_FOUND OR TARGET yaml-cpp::yaml-cpp OR TARGET yaml-cpp)
  message(STATUS "yaml-cpp found via find_package")
elseif(CTLTEST_FETCH_YAML_CPP AND Git_FOUND)
  message(STATUS "yaml-cpp not found; fetching via FetchContent")
  include(FetchContent)
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
  # 0.9.0 rather than 0.8.0: the older release uses uint16_t and uint32_t in
  # emitterutils.cpp without including <cstdint>, which newer GCC no longer
  # supplies transitively, and it declares cmake_minimum_required(VERSION 3.4),
  # below the floor CMake 4 accepts.  0.9.0 fixes both.
  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
  )
  FetchContent_MakeAvailable(yaml-cpp)
else()
  # ctltest cannot be built without yaml-cpp, and CTL_BUILD_MODULETEST defaults
  # ON, so an unavailable dependency must not take the whole configure down
  # with it: minimal build containers routinely ship without git, and offline
  # builds cannot clone at all.  Skip the framework and let the rest of CTL
  # build, which is what every packaging job here actually wants.
  if(NOT CTLTEST_FETCH_YAML_CPP)
    set(_ctltest_why "CTLTEST_FETCH_YAML_CPP=OFF")
  else()
    set(_ctltest_why "git is unavailable to fetch it")
  endif()
  message(WARNING
    "CTL_BUILD_MODULETEST=ON but yaml-cpp was not found and ${_ctltest_why}; "
    "skipping the module test framework.  Install yaml-cpp "
    "(apt libyaml-cpp-dev / brew yaml-cpp / vcpkg yaml-cpp) to build it.")
  # Shadow the cache entry rather than FORCE-writing it, so a later configure
  # re-probes and picks yaml-cpp up once it is installed.
  set(CTL_BUILD_MODULETEST OFF PARENT_SCOPE)
  return()
endif()

# Pick whichever target name the installed/fetched yaml-cpp provides.
if(TARGET yaml-cpp::yaml-cpp)
  set(CTLTEST_YAML_CPP_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
  set(CTLTEST_YAML_CPP_TARGET yaml-cpp)
else()
  message(FATAL_ERROR "yaml-cpp was located but exports no recognizable target")
endif()

add_subdirectory(lib)
add_subdirectory(driver)
add_subdirectory(cli)
add_subdirectory(unittest)

# Manifest-driven ctest registration.
# Adding a test = appending one line to tests/manifest.txt (git-tracked).
# No file(GLOB) at configure time to no silent reconfiguration surprises.
set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/tests/manifest.txt")
if(EXISTS "${_manifest}")
  file(STRINGS "${_manifest}" _manifest_entries)
  foreach(_entry IN LISTS _manifest_entries)
    string(STRIP "${_entry}" _entry)
    if(_entry AND NOT _entry MATCHES "^#")
      ctltest_add_yaml("${_entry}")
    endif()
  endforeach()
endif()
