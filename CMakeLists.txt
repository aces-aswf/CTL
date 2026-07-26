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

# The interpreter and ctlrender both run std::thread pools; on glibc < 2.34
# that needs an explicit pthread link, which Threads::Threads supplies.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# add_link_options() arrived in CMake 3.13 and CTL still configures on distros
# shipping older releases (ubuntu-18.04 carries 3.10).  Those are all
# single-config generators, so appending to CMAKE_*_LINKER_FLAGS is equivalent
# to what add_link_options would emit.
macro(ctl_add_link_options)
  if(COMMAND add_link_options)
    add_link_options(${ARGN})
  else()
    foreach(_ctl_lflag ${ARGN})
      string(APPEND CMAKE_EXE_LINKER_FLAGS " ${_ctl_lflag}")
      string(APPEND CMAKE_SHARED_LINKER_FLAGS " ${_ctl_lflag}")
      string(APPEND CMAKE_MODULE_LINKER_FLAGS " ${_ctl_lflag}")
    endforeach()
  endif()
endmacro()

# Same, for a flag that should apply to Release only.  The generator
# expression is meaningless on the pre-3.13 path, so it is applied
# unconditionally there -- callers are already inside a Release-only block.
macro(ctl_add_release_link_option _flag)
  if(COMMAND add_link_options)
    add_link_options($<$<CONFIG:Release>:${_flag}>)
  else()
    ctl_add_link_options(${_flag})
  endif()
endmacro()

option(CTL_BUILD_TESTS "Build the unit tests" ON)
option(CTL_BUILD_TOOLS "Build the utility commands (ctlrender, etc)" ON)
option(BUILD_SHARED_LIBS "Build all libraries as shared" OFF )

# Vectorized stdlib transcendentals.  The fast path routes the 4096-lane
# SIMD inner loop through a batched vendor library instead of scalar libm
# per lane.  Precision contract: <=1 ULP vs platform libm per function (<=2
# ULP end-to-end through chained transcendentals, well inside every CTL
# fixture tolerance).
#
# Defaults are platform-picked so a bare `cmake -B build` gives the fast
# path on every tier-1 target:
#
#   APPLE (arm64 or x86_64): CTL_USE_ACCELERATE=ON.  Accelerate ships with
#       the OS; zero extra configure/build cost.
#   non-APPLE (Linux x86_64/arm64, Windows x86_64/arm64, BSDs): CTL_USE_SLEEF=ON.
#       sleef is looked up via find_package first (apt libsleef-dev, brew
#       sleef, vcpkg sleef); if not installed CTL_SLEEF_FETCH=ON clones and
#       builds sleef v3.6.1 from GitHub at configure time (~60s one-off).
#
# Opt out of the fast path with -DCTL_USE_ACCELERATE=OFF / -DCTL_USE_SLEEF=OFF.
# With both OFF the build is bit-exact against a scalar-libm reference on the
# same host -- useful for pipeline reproducibility against historical CTL
# releases.  Internal bit-exactness across -threads/-jobs values is unaffected
# by these flags.
if(APPLE)
  set(_ctl_use_accelerate_default ON)
  set(_ctl_use_sleef_default OFF)
else()
  set(_ctl_use_accelerate_default OFF)
  set(_ctl_use_sleef_default ON)
endif()

option(CTL_USE_ACCELERATE
  "Use Apple Accelerate vForce for vectorized stdlib transcendentals (APPLE only)"
  ${_ctl_use_accelerate_default})
if(CTL_USE_ACCELERATE AND NOT APPLE)
  message(WARNING "CTL_USE_ACCELERATE requested on non-Apple platform; ignored.")
  set(CTL_USE_ACCELERATE OFF CACHE BOOL "" FORCE)
endif()

# sleef (https://sleef.org) is the cross-platform alternative to Accelerate.
# When enabled, routes the same 15 transcendentals through sleef's SIMD
# entry points on x86_64 (SSE2 baseline) and arm64 (NEON).  Works on Linux,
# Windows, and macOS.  On macOS with both options ON, Accelerate takes
# per-function precedence (one backend per function, no runtime branching).
option(CTL_USE_SLEEF
  "Use sleef for vectorized stdlib transcendentals (cross-platform)"
  ${_ctl_use_sleef_default})
set(CTL_SLEEF_ULP "10" CACHE STRING
  "sleef ULP tier: 10 (<=1 ULP, matches Accelerate contract) or 35 (<=3.5 ULP, faster)")
set_property(CACHE CTL_SLEEF_ULP PROPERTY STRINGS "10" "35")
option(CTL_SLEEF_FETCH
  "If sleef is not found via find_package, fetch and build it via FetchContent"
  ON)

if(CTL_USE_SLEEF)
  if(NOT (CTL_SLEEF_ULP STREQUAL "10" OR CTL_SLEEF_ULP STREQUAL "35"))
    message(FATAL_ERROR "CTL_SLEEF_ULP must be \"10\" or \"35\" (got \"${CTL_SLEEF_ULP}\")")
  endif()

  # FetchContent clones with git; probe for it up front so a missing git
  # takes the fallback branch below instead of failing inside ExternalProject.
  find_package(Git QUIET)

  find_package(sleef CONFIG QUIET)
  if(sleef_FOUND)
    message(STATUS "sleef found via find_package (version ${sleef_VERSION})")
  else()
    # CTL_USE_SLEEF defaults ON for every non-Apple host, so a plain
    # `cmake -B build` must not hard-fail when sleef is absent.  Decide whether
    # the FetchContent fallback can actually work here before reaching for it.
    set(_ctl_sleef_fetchable TRUE)
    if(NOT CTL_SLEEF_FETCH)
      set(_ctl_sleef_fetchable FALSE)
      set(_ctl_sleef_why "CTL_SLEEF_FETCH=OFF")
    elseif(NOT Git_FOUND)
      # Minimal build containers routinely ship without git, and offline
      # builds have no network to clone from either.
      set(_ctl_sleef_fetchable FALSE)
      set(_ctl_sleef_why "git is unavailable to fetch it")
    elseif(MSVC)
      # sleef's generated scalar dispatch does not compile under MSVC, so on
      # that toolchain sleef has to arrive prebuilt (vcpkg) or not at all.
      set(_ctl_sleef_fetchable FALSE)
      set(_ctl_sleef_why "building it from source is unsupported under MSVC")
    endif()

    if(_ctl_sleef_fetchable)
      message(STATUS "sleef not found; fetching via FetchContent")
      include(FetchContent)
      set(SLEEF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(SLEEF_BUILD_DFT OFF CACHE BOOL "" FORCE)
      set(SLEEF_BUILD_QUAD OFF CACHE BOOL "" FORCE)
      set(SLEEF_BUILD_GNUABI_LIBS OFF CACHE BOOL "" FORCE)
      set(SLEEF_BUILD_SCALAR_LIB ON CACHE BOOL "" FORCE)
      FetchContent_Declare(
        sleef
        GIT_REPOSITORY https://github.com/shibatch/sleef.git
        GIT_TAG 3.6.1
      )
      FetchContent_MakeAvailable(sleef)
      # The in-tree targets set only PRIVATE include directories, so the
      # generated sleef.h never reaches consumers the way an installed
      # sleef::sleef would.  Hand the directory to them explicitly.
      set(CTL_SLEEF_INCLUDE_DIR "${sleef_BINARY_DIR}/include")
    else()
      # Dropping to scalar libm is the documented opt-out and costs only
      # speed, so warn and carry on rather than failing the configure.
      message(WARNING
        "CTL_USE_SLEEF=ON but sleef was not found and ${_ctl_sleef_why}; "
        "falling back to the scalar libm path.  Install sleef "
        "(apt libsleef-dev / brew sleef / vcpkg sleef) to use the vectorized "
        "transcendentals.")
      # Shadow the cache entry rather than FORCE-writing it, so a later
      # configure re-probes and picks sleef up once it is installed.
      set(CTL_USE_SLEEF OFF)
    endif()
  endif()
endif()

# Resolve the sleef link target name once so every consumer agrees on it.
# An installed sleef exports the namespaced sleef::sleef; the FetchContent
# in-tree build defines only the plain sleef target.  Consumers link
# ${CTL_SLEEF_TARGET} rather than hardcoding either spelling.
if(CTL_USE_SLEEF)
  if(TARGET sleef::sleef)
    set(CTL_SLEEF_TARGET sleef::sleef)
  else()
    set(CTL_SLEEF_TARGET sleef)
  endif()

  if(APPLE AND CTL_USE_ACCELERATE)
    message(STATUS "Both CTL_USE_ACCELERATE and CTL_USE_SLEEF are ON. "
                   "Accelerate takes per-function precedence; sleef paths "
                   "compile in but are unreachable.")
  endif()
endif()

set(CMAKE_CXX_FLAGS_ASAN
  "-fsanitize=address -fno-optimize-sibling-calls -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g -O1"
  CACHE STRING "Flags used by the C++ compiler during AddressSanitizer builds."
  FORCE)

# Release-mode codegen options.  CTL_NATIVE_ARCH and CTL_LTO default ON:
# developer builds and the on-branch benchmark matrix are measured with these
# on.  Turn them off when cutting a portable release tarball that must run on
# older silicon than the build host.  All three are GCC/Clang driver
# spellings and are skipped under MSVC, which spells them differently.
#
#   CTL_NATIVE_ARCH  Emit host-specific ISA extensions (-mcpu=native on
#                    APPLE, -march=native elsewhere).  On Apple Silicon
#                    this picks up the host's NEON/FMA/BF16 feature set;
#                    on x86_64 it enables AVX2/AVX-512 when the build host
#                    supports them.  Unlocks autovectorization of the
#                    SimdUnary/Binary ALU inner loops.
#   CTL_LTO          Link-time optimization (-flto=thin on Clang/AppleClang,
#                    -flto on GCC).  The SIMD interpreter instantiates
#                    SimdUnaryOpInst/SimdBinaryOpInst hundreds of times
#                    across CtlSimdType.cpp; LTO inlines the per-type Op<>
#                    template across TU boundaries.
#   CTL_HIDDEN_VISIBILITY
#                    Build with -fvisibility=hidden and
#                    -fvisibility-inlines-hidden so all symbols default to
#                    hidden unless explicitly marked exported.  Stacks with
#                    CTL_LTO: the combined signal lets the link-time
#                    optimizer see that internal functions have no external
#                    consumers and can be inlined more aggressively or
#                    eliminated entirely.
#
#                    Defaults OFF because it breaks RTTI across library
#                    boundaries.  IlmImfCtl dispatches on image header
#                    attributes with a dynamic_cast chain over
#                    Imf::TypedAttribute<T> specializations, and those
#                    templates are instantiated both in OpenEXR and in CTL.
#                    Hiding CTL's copies makes their type_info module-local,
#                    so a cast against an object constructed inside libIlmImf
#                    no longer matches and every header attribute is reported
#                    as unconvertible.  Turn it on only when nothing outside
#                    the build exchanges RTTI with CTL.
option(CTL_NATIVE_ARCH
  "Emit host-specific ISA (-mcpu=native on Apple, -march=native elsewhere) in Release builds"
  ON)
option(CTL_LTO "Link-time optimization (-flto=thin on Clang, -flto on GCC) in Release builds" ON)
option(CTL_HIDDEN_VISIBILITY
  "-fvisibility=hidden + -fvisibility-inlines-hidden in Release builds (breaks cross-library RTTI)"
  OFF)

# Runtime sanitizer instrumentation.  Replaces the normal Release flags
# with the chosen sanitizer's flags; PGO and LTO are forced off because
# both interact poorly with sanitizer instrumentation (the runtime
# checks fight the optimisation passes).  Configure with:
#
#   cmake -DCTL_SANITIZER=ADDRESS -DCMAKE_BUILD_TYPE=Debug ...
#
# then the standard `ctest` runs the test suite under the sanitizer.
# tools/run_sanitizer_tests.sh wraps the configure+build+ctest cycle.
#
# Catches the class of bugs unit tests can't see directly:
#   ADDRESS   -- heap leaks, use-after-free, double-free, OOB
#   THREAD    -- data races (used during testConcurrentCalls dev)
#   UNDEFINED -- signed integer overflow, misaligned access, etc.
set(CTL_SANITIZER "OFF" CACHE STRING
  "Runtime sanitizer: OFF, ADDRESS, THREAD, UNDEFINED")
set_property(CACHE CTL_SANITIZER PROPERTY STRINGS "OFF" "ADDRESS" "THREAD" "UNDEFINED")

if(NOT CTL_SANITIZER STREQUAL "OFF")
  if(CTL_SANITIZER STREQUAL "ADDRESS")
    set(_san_flag "-fsanitize=address")
  elseif(CTL_SANITIZER STREQUAL "THREAD")
    set(_san_flag "-fsanitize=thread")
  elseif(CTL_SANITIZER STREQUAL "UNDEFINED")
    set(_san_flag "-fsanitize=undefined")
  else()
    message(FATAL_ERROR "CTL_SANITIZER must be OFF, ADDRESS, THREAD, or UNDEFINED (got '${CTL_SANITIZER}').")
  endif()
  message(STATUS "CTL_SANITIZER=${CTL_SANITIZER}: applying ${_san_flag}; forcing CTL_PGO=OFF and CTL_LTO=OFF (sanitizers conflict with both)")
  set(CTL_PGO "OFF" CACHE STRING "" FORCE)
  set(CTL_LTO OFF CACHE BOOL "" FORCE)
  add_compile_options(${_san_flag} -fno-omit-frame-pointer -O1)
  ctl_add_link_options(${_san_flag})
endif()

# Profile-guided optimization (clang -fprofile-generate / -fprofile-use).
# Four-state knob: AUTO, OFF, GENERATE, USE.
#
#   AUTO      (default) If `${CMAKE_SOURCE_DIR}/cmake/pgo/ctl.profdata`
#             exists, behave as USE with that shipped profile.  Else
#             behave as OFF.  Lets `cmake -B build && make` get PGO by
#             default when the repo has a maintainer-captured profile
#             committed, and gracefully degrade to a non-PGO build when
#             the profile is absent (e.g. on a fresh checkout from
#             master before the first profile is committed).
#   OFF       No PGO.
#   GENERATE  Compile + link with -fprofile-generate=<dir>.  The build
#             produces instrumented binaries that write .profraw files
#             to the directory at runtime.  Run ctlrender on a
#             representative workload, merge the profraw files with
#             `xcrun llvm-profdata merge -o ctl.profdata *.profraw`,
#             then reconfigure with CTL_PGO=USE and CTL_PGO_PROFILE=...
#             The `pgo-regenerate` custom target automates this.
#   USE       Compile with -fprofile-use=<CTL_PGO_PROFILE>.  The compiler
#             uses the profile data to inform inlining, block ordering,
#             and spill decisions.  Typical lift on clang with thin-LTO
#             already on: ~10-13% on template-heavy dispatch code
#             (measured on aces_combined 2K-4K).
#
# Regenerating the shipped profile (maintainer flow):
#   cmake -B build-pgo -DCTL_PGO=GENERATE && cmake --build build-pgo
#   cmake --build build-pgo --target pgo-regenerate
# The custom target runs the training workload, merges the profraw
# files, and writes the result to ${CMAKE_SOURCE_DIR}/cmake/pgo/ctl.profdata,
# ready to commit.
#
# The default profile directory for GENERATE builds is
# ${CMAKE_BINARY_DIR}/pgo-profraw.  Override with -DCTL_PGO_PROFILE_DIR.
set(CTL_PGO "AUTO" CACHE STRING
  "Profile-guided optimization: AUTO, OFF, GENERATE (instrument), or USE (consume profile)")
set_property(CACHE CTL_PGO PROPERTY STRINGS "AUTO" "OFF" "GENERATE" "USE")
set(CTL_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profraw" CACHE PATH
  "Directory where GENERATE builds write .profraw files")
set(CTL_PGO_PROFILE "" CACHE FILEPATH
  "Path to the .profdata file for USE builds (produced by llvm-profdata merge)")

# AUTO resolution: pick USE if the shipped profile is present, else OFF.
# Doing this at cache-definition time (before the Release-flags block
# below consumes CTL_PGO) means the Release block sees a concrete value.
if(CTL_PGO STREQUAL "AUTO")
  set(_ctl_pgo_shipped "${CMAKE_SOURCE_DIR}/cmake/pgo/ctl.profdata")
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # The shipped profile is llvm-profdata output.  GCC's -fprofile-use takes
    # a directory of .gcda files instead, so handing it this path makes it
    # search for per-object .gcda under the profile name, find nothing, and
    # warn once per translation unit while optimizing as if PGO were off.
    message(STATUS
      "CTL_PGO=AUTO: shipped profile is llvm-profdata format and the compiler "
      "is ${CMAKE_CXX_COMPILER_ID}; disabling PGO.")
    set(CTL_PGO "OFF")
  elseif(EXISTS "${_ctl_pgo_shipped}")
    message(STATUS "CTL_PGO=AUTO: using shipped profile ${_ctl_pgo_shipped}")
    set(CTL_PGO "USE")
    if(NOT CTL_PGO_PROFILE OR CTL_PGO_PROFILE STREQUAL "")
      set(CTL_PGO_PROFILE "${_ctl_pgo_shipped}")
    endif()
  else()
    message(STATUS "CTL_PGO=AUTO: no shipped profile at ${_ctl_pgo_shipped}; disabling PGO.")
    set(CTL_PGO "OFF")
  endif()
endif()

if(NOT MSVC AND (CMAKE_BUILD_TYPE STREQUAL "Release" OR
   (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)))
  if(CTL_NATIVE_ARCH)
    if(APPLE)
      add_compile_options($<$<CONFIG:Release>:-mcpu=native>)
    else()
      add_compile_options($<$<CONFIG:Release>:-march=native>)

      # -march=native selects whatever the build host supports, and the
      # compiler may then emit instructions that the assembler shipped
      # beside it does not know.  GCC 12 emits AVX512-FP16 (vmovw) on a
      # host that has it, but binutils only learned those mnemonics in
      # 2.38, so a GCC 12 / binutils 2.37 pair -- Fedora 36 ships
      # exactly that -- fails to assemble its own output:
      #
      #   Error: no such instruction: `vmovw 32(%rax),%xmm0'
      #
      # Compile a half-precision expression the way the real build will
      # and see whether it survives the assembler.  If it does not, drop
      # just that extension; the rest of -march=native still applies.
      #
      # The probe also fails on a compiler with no x86 _Float16 at all
      # (GCC before 12).  That costs nothing: such a compiler does not
      # know -mno-avx512fp16 either, so the guard below adds nothing,
      # and it never emits the instructions in the first place.
      include(CheckCXXSourceCompiles)
      include(CheckCXXCompilerFlag)

      set(_ctl_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
      set(CMAKE_REQUIRED_FLAGS "-march=native")
      check_cxx_source_compiles("
          _Float16 add_h (_Float16 a, _Float16 b) { return a + b; }
          int main () { return 0; }
        "
        CTL_NATIVE_ARCH_BUILDS_FP16)
      set(CMAKE_REQUIRED_FLAGS "${_ctl_saved_required_flags}")

      if(NOT CTL_NATIVE_ARCH_BUILDS_FP16)
        check_cxx_compiler_flag("-mno-avx512fp16" CTL_HAS_NO_AVX512FP16)
        if(CTL_HAS_NO_AVX512FP16)
          message(STATUS
            "CTL_NATIVE_ARCH: this toolchain cannot build half-precision "
            "code under -march=native; adding -mno-avx512fp16")
          add_compile_options($<$<CONFIG:Release>:-mno-avx512fp16>)
        endif()
      endif()
    endif()
  endif()
  if(CTL_LTO)
    # -flto=thin is Clang/AppleClang-specific; GCC only understands -flto.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      set(_ctl_lto_flag "-flto=thin")
    else()
      set(_ctl_lto_flag "-flto")
    endif()
    add_compile_options($<$<CONFIG:Release>:${_ctl_lto_flag}>)
    ctl_add_release_link_option(${_ctl_lto_flag})
    # GCC's LTO plugin re-runs symbol resolution at link time and drops the
    # implicit libpthread that the driver would otherwise supply, so
    # std::thread users fail with undefined pthread_create on glibc < 2.34.
    # Put the flag back explicitly.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_USE_PTHREADS_INIT)
      add_compile_options($<$<CONFIG:Release>:-pthread>)
      ctl_add_release_link_option(-pthread)
    endif()
    # A static archive written by plain ar carries no LTO symbol table, so the
    # link-time plugin cannot see the definitions inside it and every reference
    # into that archive comes back undefined.  gcc-ar and gcc-ranlib load the
    # plugin and write an index the linker can actually use.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      find_program(CTL_GCC_AR NAMES gcc-ar)
      find_program(CTL_GCC_RANLIB NAMES gcc-ranlib)
      if(CTL_GCC_AR AND CTL_GCC_RANLIB)
        set(CMAKE_AR "${CTL_GCC_AR}")
        set(CMAKE_RANLIB "${CTL_GCC_RANLIB}")
      else()
        message(WARNING
          "CTL_LTO=ON under GCC but gcc-ar/gcc-ranlib were not found; static "
          "archives will carry no LTO symbol index.  Configure with "
          "-DCTL_LTO=OFF if the link reports undefined references to symbols "
          "this project defines.")
      endif()
    endif()
  endif()
  if(CTL_HIDDEN_VISIBILITY)
    add_compile_options(
      $<$<CONFIG:Release>:-fvisibility=hidden>
      $<$<CONFIG:Release>:-fvisibility-inlines-hidden>)
  endif()
  if(CTL_PGO STREQUAL "GENERATE")
    message(STATUS "CTL_PGO=GENERATE: profraw dir = ${CTL_PGO_PROFILE_DIR}")
    add_compile_options($<$<CONFIG:Release>:-fprofile-generate=${CTL_PGO_PROFILE_DIR}>)
    ctl_add_release_link_option(-fprofile-generate=${CTL_PGO_PROFILE_DIR})
  elseif(CTL_PGO STREQUAL "USE")
    if(NOT CTL_PGO_PROFILE OR NOT EXISTS "${CTL_PGO_PROFILE}")
      message(FATAL_ERROR "CTL_PGO=USE requires CTL_PGO_PROFILE to point at an existing .profdata file (got '${CTL_PGO_PROFILE}'). See CMakeLists.txt for the GENERATE, llvm-profdata merge, then USE workflow.")
    endif()
    message(STATUS "CTL_PGO=USE: consuming profile at ${CTL_PGO_PROFILE}")
    add_compile_options($<$<CONFIG:Release>:-fprofile-use=${CTL_PGO_PROFILE}>)
    # Clang emits a (non-fatal) warning when profile counters are stale
    # (source changed between GENERATE and USE).  Silence that noise
    # during CI runs; still fails hard on a missing profile above.
    add_compile_options($<$<CONFIG:Release>:-Wno-profile-instr-out-of-date>)
    add_compile_options($<$<CONFIG:Release>:-Wno-profile-instr-unprofiled>)
  elseif(NOT CTL_PGO STREQUAL "OFF")
    message(FATAL_ERROR "CTL_PGO must be AUTO, OFF, GENERATE, or USE (got '${CTL_PGO}').")
  endif()
endif()

# pgo-regenerate helper target: assumes the current build is
# CTL_PGO=GENERATE (ctlrender already built as instrumented).  Runs
# the training workload across a representative fixture matrix, merges
# the profraw output, and writes it into the source tree at
# cmake/pgo/ctl.profdata so the next commit picks it up.  Maintainer
# re-runs this when source churn makes the shipped profile stale.
if(CTL_PGO STREQUAL "GENERATE")
  find_program(CTL_LLVM_PROFDATA
    NAMES llvm-profdata
    HINTS /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin
    DOC "llvm-profdata (xcrun -f llvm-profdata on Apple)")
  if(CTL_LLVM_PROFDATA AND CTL_BUILD_TOOLS)
    set(_pgo_train_dir "${CMAKE_BINARY_DIR}/pgo-train")
    set(_pgo_profraw_dir "${CTL_PGO_PROFILE_DIR}")
    set(_pgo_shipped_out "${CMAKE_SOURCE_DIR}/cmake/pgo/ctl.profdata")
    add_custom_target(pgo-regenerate
      COMMAND ${CMAKE_COMMAND} -E remove_directory "${_pgo_profraw_dir}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_pgo_profraw_dir}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_pgo_train_dir}"
      # Training workload: three representative CTL scripts across
      # three input sizes.  Each invocation writes its own profraw
      # (keyed on pid + module hash).  ~30 seconds total on M4 Max.
      COMMAND ${CMAKE_COMMAND} -E env
        "LLVM_PROFILE_FILE=${_pgo_profraw_dir}/%p_%m.profraw"
        $<TARGET_FILE:ctlrender>
          -ctl ${CMAKE_SOURCE_DIR}/benchmarks/fixtures/aces_combined.ctl
          -format exr32 -force
          ${CMAKE_SOURCE_DIR}/benchmarks/inputs/gradient_4k_rgb_exr32.exr
          ${_pgo_train_dir}/aces_4k.exr
      COMMAND ${CMAKE_COMMAND} -E env
        "LLVM_PROFILE_FILE=${_pgo_profraw_dir}/%p_%m.profraw"
        $<TARGET_FILE:ctlrender>
          -ctl ${CMAKE_SOURCE_DIR}/benchmarks/fixtures/aces_combined.ctl
          -format exr32 -force
          ${CMAKE_SOURCE_DIR}/benchmarks/inputs/gradient_4mpx_rgb_exr32.exr
          ${_pgo_train_dir}/aces_4mpx.exr
      COMMAND ${CMAKE_COMMAND} -E env
        "LLVM_PROFILE_FILE=${_pgo_profraw_dir}/%p_%m.profraw"
        $<TARGET_FILE:ctlrender>
          -ctl ${CMAKE_SOURCE_DIR}/benchmarks/fixtures/aces_combined.ctl
          -format exr32 -force
          ${CMAKE_SOURCE_DIR}/benchmarks/inputs/gradient_1mpx_rgb_exr32.exr
          ${_pgo_train_dir}/aces_1mpx.exr
      COMMAND ${CMAKE_COMMAND} -E env
        "LLVM_PROFILE_FILE=${_pgo_profraw_dir}/%p_%m.profraw"
        $<TARGET_FILE:ctlrender>
          -ctl ${CMAKE_SOURCE_DIR}/benchmarks/fixtures/matrix3x3.ctl
          -format exr32 -force
          ${CMAKE_SOURCE_DIR}/benchmarks/inputs/gradient_4k_rgb_exr32.exr
          ${_pgo_train_dir}/matrix_4k.exr
      COMMAND ${CMAKE_COMMAND} -E env
        "LLVM_PROFILE_FILE=${_pgo_profraw_dir}/%p_%m.profraw"
        $<TARGET_FILE:ctlrender>
          -ctl ${CMAKE_SOURCE_DIR}/benchmarks/fixtures/transcendentals.ctl
          -format exr32 -force
          ${CMAKE_SOURCE_DIR}/benchmarks/inputs/gradient_4k_rgb_exr32.exr
          ${_pgo_train_dir}/trans_4k.exr
      # Merge all profraw into one profdata in the source tree.
      COMMAND ${CTL_LLVM_PROFDATA} merge
        -o "${_pgo_shipped_out}"
        "${_pgo_profraw_dir}"
      DEPENDS ctlrender
      COMMENT "PGO regenerate: train ctlrender across a representative "
              "fixture matrix, merge profraw into "
              "cmake/pgo/ctl.profdata (commit the result)"
      VERBATIM)
  endif()
endif()

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

