# Super Mario War for the Wii: a thin wrapper around upstream (mmatyas/supermariowar, SDL3).
#
# Upstream (supermariowar/) and the libogc2 SDL3 fork (SDL3-libogc2/) are git submodules. The Wii-specific
# code in src/ is linked into upstream's `smw` target; upstream itself is not modified.
#
# Prerequisites: devkitPPC, libogc2 and libogc2-libwiisocket, and the SDL3 fork already BUILT AND INSTALLED
# (.github/workflows/build.yml has the exact commands).
#
#   cmake -S . -B build-wii --toolchain $DEVKITPRO/cmake/Wii.cmake \
#         -DDKP_OGC_PLATFORM_LIBRARY=libogc2 -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-wii --target smw_dol      # -> build-wii/boot.dol
cmake_minimum_required(VERSION 3.25)
project(smw-console LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")
    message(FATAL_ERROR "Unsupported system '${CMAKE_SYSTEM_NAME}': configure with devkitPPC's Wii.cmake toolchain")
endif()

# ==== Settings =====================================================================================

# Where everything lives on the SD card (see src/wii_paths.cpp and src/wii_crashlog.cpp):
#   <root>data/         game data (the contents of supermariowar/data)
#   <root>              settings
#   <root>smw-log.txt   crash log, written only when the game ends abnormally
#   <root>log.txt       live log, only in SMW_WII_LOG builds (below)
set(SMW_WII_ROOT "sd:/smw/" CACHE STRING "SD card folder for data, settings and the crash log (must end in '/')")
if(NOT SMW_WII_ROOT MATCHES "/$")
    message(FATAL_ERROR "SMW_WII_ROOT must end in '/' (got '${SMW_WII_ROOT}')")
endif()

option(SMW_WII_LOG "Also append every log line to <root>log.txt as it happens (opens/appends/closes the file per line: for bug-report builds only)" OFF)

# Options of upstream's own CMake project.
set(NO_NETWORK OFF CACHE BOOL "" FORCE)            # netplay via ENet over libwiisocket (see src/wii_net.cpp)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SMW_INSTALL_PORTABLE ON CACHE BOOL "" FORCE)   # data dir = ./data next to the dol

# ==== Dependencies =================================================================================

# ---- SDL3: use the copy you built and installed (the libogc2 fork), do NOT build it again.
# Upstream declares SDL3/SDL3_image/SDL3_mixer with FIND_PACKAGE_ARGS, so find_package() is tried
# first -- unless FETCHCONTENT_SOURCE_DIR_SDL3 is set, which forces a from-source build.
# (Do not set that variable.) We only have to make sure the install prefix is searched:
set(SMW_SDL3_PREFIX "$ENV{DEVKITPRO}/libogc2/wii" CACHE PATH "where `make install` put SDL3")
list(APPEND CMAKE_PREFIX_PATH "${SMW_SDL3_PREFIX}")
list(APPEND CMAKE_FIND_ROOT_PATH "${SMW_SDL3_PREFIX}")
find_package(SDL3 REQUIRED CONFIG)   # fail early, with a clear message, if it isn't found
message(STATUS "Using installed SDL3 ${SDL3_VERSION} from ${SDL3_DIR}")

# ---- Networking: ENet (fetched by upstream) on libwiisocket.
# libwiisocket (pacman: libogc2-libwiisocket) provides BSD sockets on top of libogc2's network stack.
# It installs into the same prefix as SDL3 above. It must NOT be mixed with libogc's <network.h>.
find_library(WIISOCKET_LIB wiisocket REQUIRED HINTS "${SMW_SDL3_PREFIX}/lib")
message(STATUS "Using libwiisocket: ${WIISOCKET_LIB}")

# ENet's configure checks probe for functions by linking a test program, which would need wiisocket
# on the link line in the right order. Seed the results instead. libwiisocket has getaddrinfo,
# getnameinfo, inet_pton/ntop, fcntl and poll, but only the non-reentrant gethostby*() (so
# HAS_GETHOSTBY*_R stay unset), and no sendmsg/recvmsg or struct msghdr: src/wii_sockshim.* supply them.
foreach(v HAS_FCNTL HAS_POLL HAS_GETADDRINFO HAS_GETNAMEINFO HAS_INET_PTON HAS_INET_NTOP HAS_MSGHDR_FLAGS)
    set(${v} 1 CACHE INTERNAL "")
endforeach()
set(HAVE_HAS_SOCKLEN_T 1 CACHE INTERNAL "")
set(HAS_SOCKLEN_T 4 CACHE INTERNAL "")

# ---- SDL3_image / SDL3_mixer: built from source (vendored by upstream). Keep them minimal.
# They assume every platform but a short hardcoded list can build shared libs and run
# option(CMAKE_POSITION_INDEPENDENT_CODE ... ON). That leaks -fPIE into every later try_compile
# and target, which is incompatible with devkitPPC's -msdata=sysv (every configure check then fails
# with "'-fPIC' and '-msdata=sysv' are incompatible"). option() keeps an existing cache value, so:
set(CMAKE_POSITION_INDEPENDENT_CODE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
# Ogg Vorbis via the built-in stb_vorbis, so libogg/libvorbis are never configured
# (their configure-time checks fail under this cross toolchain).
set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
foreach(c AIFF VOC AU FLAC GME MOD MP3 MIDI OPUS WAVPACK)
    set(SDLMIXER_${c} OFF CACHE BOOL "" FORCE)
endforeach()

# ---- toml11: same pin as upstream, minus thread_local.
# libogc2 has no TLS (r2 is the SDA2 base, not a thread pointer), but toml11 v4.4.0 builds its grammar
# caches as `static thread_local`. That corrupts .sdata2/.rodata and crashes in toml::parse.
# Must come BEFORE add_subdirectory(supermariowar): FetchContent keeps the first declaration it sees.
include(FetchContent)
FetchContent_Declare(
    toml11
    GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
    GIT_TAG v4.4.0
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
    PATCH_COMMAND sed -i "s/static thread_local/static/" include/toml11/impl/syntax_impl.hpp
    SYSTEM
)

# ==== Upstream =====================================================================================

# Upstream assumes it is the top-level project (CMAKE_SOURCE_DIR/cmake)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/supermariowar/cmake")

# EXCLUDE_FROM_ALL: we only want the `smw` target, not the editors/server
add_subdirectory(supermariowar EXCLUDE_FROM_ALL)

# ENet needs sendmsg()/recvmsg(), which libwiisocket lacks: build the shim into the enet target itself.
get_target_property(_enet_imported enet IMPORTED)
if(TARGET enet AND NOT _enet_imported)
    target_sources(enet PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/wii_sockshim.c")
    target_compile_options(enet PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/wii_sockshim.h")
    # Static archives are scanned once, left to right: enet is what references socket()/htons()/..., so
    # wiisocket has to be a dependency of enet (CMake then emits it after libenet.a), not just of smw.
    # Plain signature to match how upstream links its targets. Needs CMP0079 NEW (3.13+, we require 3.25).
    target_link_libraries(enet "${WIISOCKET_LIB}")
else()
    message(FATAL_ERROR "Built-from-source ENet target not found (NO_NETWORK must be OFF, and no system ENet may be picked up)")
endif()

# ==== Wii glue (src/) ==============================================================================
# Each file is its own OBJECT library, because they can't share one set of headers: upstream's and
# libogc's don't mix (see the comments in each file). The objects are then linked into upstream's `smw`.

function(smw_wii_glue name source)
    add_library(${name} OBJECT ${source})
    target_compile_definitions(${name} PRIVATE "SMW_WII_ROOT=\"${SMW_WII_ROOT}\"")
    target_sources(smw PRIVATE $<TARGET_OBJECTS:${name}>)
endfunction()

# Crash log + boot checks. libogc headers only.
smw_wii_glue(wii_crashlog src/wii_crashlog.cpp)
if(SMW_WII_LOG)
    target_compile_definitions(wii_crashlog PRIVATE SMW_WII_LOG)
endif()

# SD card locations.
smw_wii_glue(wii_paths src/wii_paths.cpp)
target_link_libraries(wii_paths PRIVATE SDL3::SDL3)   # headers only (OBJECT library)

# Controller hotplug + default bindings. Needs upstream's headers (GameValues.h, input.h),
# which upstream exposes with a directory-level include_directories(), so add the path explicitly.
smw_wii_glue(wii_input src/wii_input.cpp)
target_compile_features(wii_input PRIVATE cxx_std_20)
target_include_directories(wii_input PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/supermariowar/src/common")
target_link_libraries(wii_input PRIVATE SDL3::SDL3)   # headers only (OBJECT library)

# Network bring-up (src/wii_net.cpp): started in the background at boot, re-checked each time the player enters
# netplay or hosts a game. Never blocks startup. (Doesn't use SMW_WII_ROOT.)
smw_wii_glue(wii_net src/wii_net.cpp)
target_include_directories(wii_net PRIVATE "${SMW_SDL3_PREFIX}/include")   # wiisocket.h

# wiisocket must come before libogc on the link line (its README). Upstream links smw with the plain
# target_link_libraries() signature, so this must be plain too (mixing with PRIVATE is a CMake error).
target_link_libraries(smw "${WIISOCKET_LIB}")

# ---- Hooks into upstream. For each symbol below the linker sends every call to __wrap_<symbol>
# (our code in src/); the original stays reachable as __real_<symbol>.
set(SMW_WRAPPED_SYMBOLS
    # crash log and boot checks: src/wii_crashlog.cpp
    SDL_RunApp
    SDL_ShowSimpleMessageBox
    exit
    abort
    # SD card locations: src/wii_paths.cpp
    SDL_GetBasePath
    SDL_GetPrefPath
    # controllers: src/wii_input.cpp
    SDL_PollEvent
    SDL_WaitEvent
    SDL_GetJoystickNameForID
    SDL_GetJoystickFromPlayerIndex
    # network bring-up: src/wii_net.cpp
    enet_initialize
    enet_deinitialize
    enet_host_create
    enet_host_connect)
foreach(sym IN LISTS SMW_WRAPPED_SYMBOLS)
    target_link_options(smw PRIVATE "-Wl,--wrap=${sym}")
endforeach()

# Upstream strips the ELF (-s) in non-Debug builds, so addr2line/nm are useless. A link map still records
# every function's address: look up the addresses printed in smw-log.txt ("called from 0x...") in smw.map.
target_link_options(smw PRIVATE "-Wl,-Map=${CMAKE_BINARY_DIR}/smw.map")

# ==== boot.dol =====================================================================================
# `smw` is created in upstream's directory, so POST_BUILD can't be attached to it from here;
# use a separate target that depends on it.
find_program(ELF2DOL elf2dol REQUIRED)
add_custom_target(smw_dol ALL
    COMMAND ${ELF2DOL} $<TARGET_FILE:smw> ${CMAKE_BINARY_DIR}/boot.dol
    DEPENDS smw
    COMMENT "Creating boot.dol"
    VERBATIM)
