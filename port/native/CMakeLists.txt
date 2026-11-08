# Native build of the GTA2 port: Linux aarch64, SDL2 + GLES2 (see CLAUDE.md).
# Driven by scripts/build_native.sh, which prepares GTA2_CORE_DIR and the case aliases.
cmake_minimum_required(VERSION 3.16)
project(gta2 C CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "clang/clang++ required (GCC has -mms-bitfields on x86 only)")
endif()

set(GTA2_CORE_DIR "" CACHE PATH "Patched engine copy (research/native-work/core)")
set(GTA2_PORT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "The repo's port/ directory")
set(GTA2_CASE_ALIASES "" CACHE PATH "Case-insensitive include aliases (scripts/native/case_aliases.py)")
option(GTA2_SANITIZE "ASan + UBSan" OFF)
option(GTA2_HEADLESS "No window, no sound (jalon 1)" ON)
option(GTA2_64BIT_WARNINGS "Warn about pointer/integer truncation" ON)

if(NOT EXISTS "${GTA2_CORE_DIR}/Source")
  message(FATAL_ERROR "GTA2_CORE_DIR=${GTA2_CORE_DIR} has no Source/: run scripts/build_native.sh")
endif()
get_filename_component(PORT "${GTA2_PORT_ROOT}" ABSOLUTE)
set(NATIVE "${CMAKE_CURRENT_SOURCE_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON) # gnu++17, as the web build

# ---------------------------------------------------------------------------
# Flags shared by the engine and the port's own code
# ---------------------------------------------------------------------------
set(GTA2_REQUIRED_FLAGS
  -fshort-wchar          # 2-byte wide strings, as MSVC
  -mms-bitfields         # MSVC bit-field layout
  -fno-builtin-wcslen    # else "count until 0" loops become glibc's 4-byte wcslen
  -fno-strict-return     # two engine functions fall off the end; MSVC tolerated it
  -fsigned-char          # char is unsigned by default on aarch64
  -fwrapv                # signed overflow wraps, as the MSVC code assumed
  -fno-strict-aliasing   # decompiled code type-puns a lot
)
set(GTA2_COMMON_FLAGS
  ${GTA2_REQUIRED_FLAGS}
  -fms-extensions -fdeclspec
  # The web build silences everything with -w; here everything is off except what
  # the 64-bit work needs to see.
  -Wno-everything
  -include "${PORT}/core/compat/port_prefix.h"
)
if(GTA2_64BIT_WARNINGS)
  list(APPEND GTA2_COMMON_FLAGS
    -Wshorten-64-to-32 -Wint-to-pointer-cast -Wpointer-to-int-cast -Wvoid-pointer-to-int-cast)
endif()

set(GTA2_DEFINES GTA2_PORT=1 GTA2_PORT_NATIVE=1 _CRT_SECURE_NO_WARNINGS)
if(GTA2_HEADLESS)
  list(APPEND GTA2_DEFINES GTA2_NATIVE_HEADLESS=1)
endif()

# Same order as build_port.py, with the native stand-ins (emscripten.h) first.
set(GTA2_INCLUDES
  "${NATIVE}/compat"
  "${PORT}/core/compat"
  "${GTA2_CORE_DIR}/Source"
  "${GTA2_CORE_DIR}"
)
if(GTA2_CASE_ALIASES)
  list(APPEND GTA2_INCLUDES "${GTA2_CASE_ALIASES}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(GTA2_OPT -O1 -g) # as the web debug build; -O1 keeps ASan bearable
endif()

if(GTA2_SANITIZE)
  set(GTA2_SAN -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# ---------------------------------------------------------------------------
# Engine: the gta2_re files the web build compiles (port/core/sources.txt)
# ---------------------------------------------------------------------------
file(STRINGS "${PORT}/core/sources.txt" GTA2_CORE_REL)
set(GTA2_CORE_SOURCES "")
foreach(rel IN LISTS GTA2_CORE_REL)
  string(STRIP "${rel}" rel)
  if(rel)
    list(APPEND GTA2_CORE_SOURCES "${GTA2_CORE_DIR}/${rel}")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PORT}/core/sources.txt")

add_library(gta2_core STATIC ${GTA2_CORE_SOURCES})
target_compile_options(gta2_core PRIVATE ${GTA2_COMMON_FLAGS} ${GTA2_OPT} ${GTA2_SAN})
target_compile_definitions(gta2_core PRIVATE ${GTA2_DEFINES})
target_include_directories(gta2_core PRIVATE ${GTA2_INCLUDES})

# ---------------------------------------------------------------------------
# Port: shared files that compile unchanged (EM_JS bridges implemented natively),
# plus port/native. Left out (browser-only): main_web, savestate_web, dev_hooks,
# map_web, jobs_web, net_web, coop_web, options_web.
# ---------------------------------------------------------------------------
set(GTA2_PORT_SHARED
  "${PORT}/platform/crt_compat.cpp"
  "${PORT}/platform/win32_shim.cpp"
  "${PORT}/platform/dinput_web.cpp"
  "${PORT}/platform/lang_web.cpp"
  "${PORT}/render/exports.cpp"
  "${PORT}/render/frame.cpp"
  "${PORT}/render/gbh_web.cpp"
  "${PORT}/render/vid_web.cpp"
  "${PORT}/audio/bink_web.cpp"
  "${PORT}/audio/miles_web.cpp"
)
file(GLOB GTA2_NATIVE_SOURCES CONFIGURE_DEPENDS
  "${NATIVE}/*.cpp" "${NATIVE}/platform/*.cpp" "${NATIVE}/render/*.cpp" "${NATIVE}/audio/*.cpp")

add_executable(gta2 ${GTA2_PORT_SHARED} ${GTA2_NATIVE_SOURCES})
target_compile_options(gta2 PRIVATE ${GTA2_COMMON_FLAGS} ${GTA2_OPT} ${GTA2_SAN})
target_compile_definitions(gta2 PRIVATE ${GTA2_DEFINES})
target_include_directories(gta2 PRIVATE ${GTA2_INCLUDES} "${PORT}")
target_link_libraries(gta2 PRIVATE gta2_core m dl pthread)
target_link_options(gta2 PRIVATE ${GTA2_SAN})
set_target_properties(gta2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
