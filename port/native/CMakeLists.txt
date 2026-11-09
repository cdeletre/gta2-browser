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
option(GTA2_HEADLESS "No window (jalons 1-2, soak tests): the draw list is dropped" OFF)
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
  -ffp-contract=off      # no fused multiply-add: the same float results as wasm (co-op lockstep) and x87
)
set(GTA2_COMMON_FLAGS
  ${GTA2_REQUIRED_FLAGS}
  -fms-extensions -fdeclspec
  # With --gc-sections at link time: like the web build's linker, unreachable code
  # (force_link() in winmain.cpp, the CD check) does not need its Win32/COM imports.
  -ffunction-sections -fdata-sections
  # The web build silences everything with -w; here everything is off except what
  # the 64-bit work needs to see.
  -Wno-everything
  -include "${PORT}/core/compat/port_prefix.h"
)
if(GTA2_64BIT_WARNINGS)
  list(APPEND GTA2_COMMON_FLAGS
    -Wshorten-64-to-32 -Wint-to-pointer-cast -Wpointer-to-int-cast -Wvoid-pointer-to-int-cast)
  # Note: with -fms-extensions clang accepts (int)pointer and reinterpret_cast<int>(pointer)
  # with no diagnostic at all; scripts/native/scan_pointer_truncation.py finds them.
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
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  # As the web release build: -O2, and no NDEBUG (CMake's default Release flags are
  # -O3 -DNDEBUG). -g does not change the code; build_native.sh moves the debug info to
  # dist/gta2.aarch64.debug and ships a stripped dist/gta2.aarch64.
  set(CMAKE_C_FLAGS_RELEASE "")
  set(CMAKE_CXX_FLAGS_RELEASE "")
  set(GTA2_OPT -O2 -g)
endif()

if(GTA2_SANITIZE)
  # -fno-sanitize=return: two engine functions fall off the end on some paths and their
  # callers ignore the value (PedGroup::MergeWithOtherGroup_4C9B60,
  # ExplosionPool_7A8::FreeLowestPriority_543690); -fno-strict-return keeps the MSVC
  # behaviour, but UBSan's return check stops the program there.
  set(GTA2_SAN -fsanitize=address,undefined -fno-sanitize=return -fno-omit-frame-pointer)
  # Shared ASan runtime: the engine defines its own operator new/delete (crt_stubs.cpp,
  # zero-filled through calloc, which the game relies on). The static runtime defines
  # them too (duplicate symbols); with the shared one the executable's definitions win
  # and ASan still checks every allocation through calloc/free.
  set(GTA2_SAN_LINK -shared-libasan)
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
                  OUTPUT_VARIABLE GTA2_CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
  list(APPEND GTA2_SAN_LINK "-Wl,-rpath,${GTA2_CLANG_RESOURCE_DIR}/lib/linux")
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
# plus port/native. Left out (browser-only): main_web (port/native/main.cpp instead),
# savestate_web (copies the wasm heap), jobs_web (the map screen's job list).
# net_web is the game's NetPlay/Network_20324 over the shell's transport (single player
# goes through it too); map_web, coop_web, dev_hooks and options_web define hooks the
# patched engine calls (HUD arrows, co-op snapshots, sync records, the Options item).
# ---------------------------------------------------------------------------
set(GTA2_PORT_SHARED
  "${PORT}/platform/crt_compat.cpp"
  "${PORT}/platform/win32_shim.cpp"
  "${PORT}/platform/dinput_web.cpp"
  "${PORT}/platform/lang_web.cpp"
  "${PORT}/platform/net_web.cpp"
  "${PORT}/platform/map_web.cpp"
  "${PORT}/platform/coop_web.cpp"
  "${PORT}/platform/dev_hooks.cpp"
  "${PORT}/platform/options_web.cpp"
  "${PORT}/render/exports.cpp"
  "${PORT}/render/frame.cpp"
  "${PORT}/render/gbh_web.cpp"
  "${PORT}/render/vid_web.cpp"
  "${PORT}/audio/bink_web.cpp"
  "${PORT}/audio/miles_web.cpp"
)
file(GLOB GTA2_NATIVE_SOURCES CONFIGURE_DEPENDS
  "${NATIVE}/*.cpp" "${NATIVE}/platform/*.cpp" "${NATIVE}/render/*.cpp" "${NATIVE}/audio/*.cpp")

if(NOT GTA2_HEADLESS)
  # SDL2 window and events, OpenGL ES 2.0 (platform/window_sdl.cpp, render/gles_renderer.cpp)
  # SDL2 is linked by path, not with sdl2.pc's link flags: the build image's sdl2.pc adds
  # -Wl,-rpath,/usr/local/lib, which the binary must not carry onto the console.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  find_library(SDL2_LIBRARY NAMES SDL2 HINTS ${SDL2_LIBRARY_DIRS} REQUIRED)
  find_library(GLESV2_LIBRARY NAMES GLESv2 REQUIRED)
endif()

# One game binary: the solo gta2, or gta2-mp with the network code (port/native/net).
function(gta2_add_game target)
  add_executable(${target} ${GTA2_PORT_SHARED} ${GTA2_NATIVE_SOURCES} ${ARGN})
  target_compile_options(${target} PRIVATE ${GTA2_COMMON_FLAGS} ${GTA2_OPT} ${GTA2_SAN})
  target_compile_definitions(${target} PRIVATE ${GTA2_DEFINES})
  target_include_directories(${target} PRIVATE ${GTA2_INCLUDES} "${PORT}")
  target_link_libraries(${target} PRIVATE gta2_core m dl pthread)
  if(NOT GTA2_HEADLESS)
    target_include_directories(${target} PRIVATE ${SDL2_INCLUDE_DIRS})
    target_compile_options(${target} PRIVATE ${SDL2_CFLAGS_OTHER})
    target_link_libraries(${target} PRIVATE ${SDL2_LIBRARY} ${GLESV2_LIBRARY})
  endif()
  target_link_options(${target} PRIVATE ${GTA2_SAN} ${GTA2_SAN_LINK} -Wl,--gc-sections)
  # No RUNPATH: the console's loader finds SDL2 and GLESv2 in its own directories.
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" SKIP_BUILD_RPATH ON)
  if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT GTA2_SANITIZE)
    # The C++ runtime inside the binary: it then needs only glibc (2.17 at most), SDL2 and
    # GLESv2 from the system, as is usual for PortMaster ports.
    target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
  endif()
endfunction()

gta2_add_game(gta2)

# ---------------------------------------------------------------------------
# Multiplayer (branch native-multiplayer): a separate binary, gta2-mp, so that the solo
# gta2 carries no network code. port/native/net: the lobby (web/net.js's CoopLobby) and
# the transports; gta2_net has no engine code, so gta2-net-test checks it on its own.
# ---------------------------------------------------------------------------
option(GTA2_MULTIPLAYER "Build gta2-mp and gta2-net-test" ON)
if(GTA2_MULTIPLAYER)
  add_library(gta2_net STATIC
    "${NATIVE}/net/json.cpp"
    "${NATIVE}/net/lobby.cpp"
    "${NATIVE}/net/memory_transport.cpp"
    "${NATIVE}/net/ws_transport.cpp")
  target_compile_options(gta2_net PRIVATE ${GTA2_COMMON_FLAGS} ${GTA2_OPT} ${GTA2_SAN})
  target_compile_definitions(gta2_net PRIVATE ${GTA2_DEFINES} GTA2_NATIVE_NET=1)

  gta2_add_game(gta2-mp "${NATIVE}/net/coop_native.cpp" "${NATIVE}/net/net_main.cpp" "${NATIVE}/net/lobby_screen.cpp" "${NATIVE}/net/hud_net.cpp")
  target_compile_definitions(gta2-mp PRIVATE GTA2_NATIVE_NET=1)
  target_link_libraries(gta2-mp PRIVATE gta2_net)

  add_executable(gta2-net-test "${NATIVE}/net/tests/net_test.cpp")
  target_compile_options(gta2-net-test PRIVATE ${GTA2_COMMON_FLAGS} ${GTA2_OPT} ${GTA2_SAN})
  target_compile_definitions(gta2-net-test PRIVATE GTA2_NATIVE_NET=1)
  target_include_directories(gta2-net-test PRIVATE "${NATIVE}/net")
  target_link_libraries(gta2-net-test PRIVATE gta2_net)
  target_link_options(gta2-net-test PRIVATE ${GTA2_SAN} ${GTA2_SAN_LINK})
  set_target_properties(gta2-net-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
