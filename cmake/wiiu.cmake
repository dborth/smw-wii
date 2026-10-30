# Wii U build of Super Mario War (included from the top-level CMakeLists.txt; independent of cmake/wii.cmake).
#
# Upstream (supermariowar/) and the Wii U SDL3 fork (SDL3-wut/, dborth/SDL branch wut-port) are git submodules.
# The Wii U code in src/wiiu_*.cpp is linked into upstream's `smw` target; upstream itself is not modified.
#
# Prerequisites: devkitPPC + wut (`dkp-pacman -S wiiu-dev`; already in the libogc2 CI image), and the SDL3 fork already BUILT AND INSTALLED
# (.github/workflows/build.yml has the exact commands).
#
#   cmake -S . -B build-wiiu --toolchain $DEVKITPRO/cmake/WiiU.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-wiiu       # -> build-wiiu/SuperMarioWar-WiiU.wuhb (game data bundled as its /vol/content)

# ==== Settings =====================================================================================

# Where the files live (wut mounts the SD card as fs:/vol/external01; a .wuhb's content as fs:/vol/content):
#   <content dir>data/   game data: the contents of supermariowar/data, bundled into SuperMarioWar-WiiU.wuhb (read-only)
#   <save dir>           settings and controls, on the SD card (writable); smw-log.txt after a crash
set(SMW_WIIU_CONTENT_DIR "fs:/vol/content/" CACHE STRING "where the .wuhb's bundled content is mounted (must end in '/')")
set(SMW_WIIU_SAVE_DIR "fs:/vol/external01/smw/" CACHE STRING "SD card folder for settings and the crash log (must end in '/')")
foreach(v SMW_WIIU_CONTENT_DIR SMW_WIIU_SAVE_DIR)
    if(NOT ${v} MATCHES "/$")
        message(FATAL_ERROR "${v} must end in '/' (got '${${v}}')")
    endif()
endforeach()

option(SMW_WIIU_LOG "Also append every log line to <save dir>log.txt as it happens (opens/appends/closes the file per line: for bug-report builds only)" OFF)

# Options of upstream's own CMake project.
set(NO_NETWORK OFF CACHE BOOL "" FORCE)            # netplay via ENet over wut's BSD sockets (see src/wiiu_net.cpp)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SMW_INSTALL_PORTABLE ON CACHE BOOL "" FORCE)   # data dir = ./data next to the rpx

# ==== Dependencies =================================================================================

# ---- SDL3: use the copy we built and installed (the wut fork), do NOT build it again.
# Upstream declares SDL3/SDL3_image/SDL3_mixer with FIND_PACKAGE_ARGS, so find_package() is tried
# first -- unless FETCHCONTENT_SOURCE_DIR_SDL3 is set, which forces a from-source build. (Do not set it.)
set(SMW_SDL3_PREFIX "$ENV{DEVKITPRO}/portlibs/wiiu" CACHE PATH "where `cmake --install` put SDL3")
list(APPEND CMAKE_PREFIX_PATH "${SMW_SDL3_PREFIX}")
list(APPEND CMAKE_FIND_ROOT_PATH "${SMW_SDL3_PREFIX}")
find_package(SDL3 REQUIRED CONFIG)   # fail early, with a clear message, if it isn't found
message(STATUS "Using installed SDL3 ${SDL3_VERSION} from ${SDL3_DIR}")

# ---- Networking: ENet (fetched by upstream) on wut's sockets.
# libwut implements BSD sockets on top of nsysnet and brings them up at startup (__init_wut_socket), so there
# is nothing to link or initialise here. It has fcntl, poll, getaddrinfo, getnameinfo, inet_pton/ntop and the
# non-reentrant gethostby*() only (so HAS_GETHOSTBY*_R stay unset), but no sendmsg/recvmsg: src/wiiu_sockshim.*
# supply them.
#
# ENet's configure checks probe for functions by linking a test program; seed the results instead so they
# don't depend on how the toolchain links try_compile projects.
foreach(v HAS_FCNTL HAS_POLL HAS_GETADDRINFO HAS_GETNAMEINFO HAS_INET_PTON HAS_INET_NTOP HAS_MSGHDR_FLAGS)
    set(${v} 1 CACHE INTERNAL "")
endforeach()
set(HAVE_HAS_SOCKLEN_T 1 CACHE INTERNAL "")
set(HAS_SOCKLEN_T 4 CACHE INTERNAL "")

# Does the toolchain's <sys/socket.h> already define struct msghdr / struct iovec? (The Wii's newlib doesn't;
# unverified for the Wii U.) If not, the shim header provides them.
include(CheckCSourceCompiles)
check_c_source_compiles("
#include <sys/types.h>
#include <sys/socket.h>
int main(void) { struct msghdr m; struct iovec v; m.msg_iov = &v; m.msg_iovlen = 1; m.msg_flags = 0; return m.msg_iovlen; }
" SMW_WIIU_HAS_MSGHDR)
message(STATUS "Wii U toolchain has struct msghdr/iovec: ${SMW_WIIU_HAS_MSGHDR}")

# ---- SDL3_image / SDL3_mixer: built from source (vendored by upstream). Keep them minimal.
# They assume every platform but a short hardcoded list can build shared libs and run
# option(CMAKE_POSITION_INDEPENDENT_CODE ... ON), which leaks -fPIE into every later try_compile and target
# (incompatible with devkitPPC's -msdata=sysv). option() keeps an existing cache value, so:
set(CMAKE_POSITION_INDEPENDENT_CODE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
# Ogg Vorbis via the built-in stb_vorbis, so libogg/libvorbis are never configured.
set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
foreach(c AIFF VOC AU FLAC GME MOD MP3 MIDI OPUS WAVPACK)
    set(SDLMIXER_${c} OFF CACHE BOOL "" FORCE)
endforeach()

# ---- toml11: same pin as upstream, minus thread_local (needed on the Wii; unknown whether wut needs it, so
# kept until someone tests without). Must come BEFORE add_subdirectory(supermariowar): FetchContent keeps the
# first declaration it sees.
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

# ENet needs sendmsg()/recvmsg(), which libwut lacks: build the shim into the enet target itself.
get_target_property(_enet_imported enet IMPORTED)
if(TARGET enet AND NOT _enet_imported)
    target_sources(enet PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/wiiu_sockshim.c")
    target_compile_options(enet PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/wiiu_sockshim.h")
    if(NOT SMW_WIIU_HAS_MSGHDR)
        target_compile_definitions(enet PRIVATE SMW_SHIM_TYPES)
    endif()
else()
    message(FATAL_ERROR "Built-from-source ENet target not found (NO_NETWORK must be OFF, and no system ENet may be picked up)")
endif()

# ==== Wii U glue (src/) ============================================================================
# Each file is its own OBJECT library; the objects are linked into upstream's `smw`.

function(smw_wiiu_glue name source)
    add_library(${name} OBJECT ${source})
    target_compile_definitions(${name} PRIVATE
        "SMW_CONTENT_DIR=\"${SMW_WIIU_CONTENT_DIR}\""
        "SMW_SAVE_DIR=\"${SMW_WIIU_SAVE_DIR}\"")
    target_link_libraries(${name} PRIVATE SDL3::SDL3)   # headers only (OBJECT library)
    target_sources(smw PRIVATE $<TARGET_OBJECTS:${name}>)
endfunction()

# Crash log + boot checks. No SDL or upstream headers.
smw_wiiu_glue(wiiu_crashlog src/wiiu_crashlog.cpp)
if(SMW_WIIU_LOG)
    target_compile_definitions(wiiu_crashlog PRIVATE SMW_WIIU_LOG)
endif()

# Content and SD card locations.
smw_wiiu_glue(wiiu_paths src/wiiu_paths.cpp)

# Controller hotplug + default bindings. Needs upstream's headers (GameValues.h, input.h).
smw_wiiu_glue(wiiu_input src/wiiu_input.cpp)
target_compile_features(wiiu_input PRIVATE cxx_std_20)
target_include_directories(wiiu_input PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/supermariowar/src/common")

# Network readiness (src/wiiu_net.cpp): logs the state at boot, waits for the connection when the player
# enters netplay or hosts a game. Never blocks startup. No SDL or upstream headers.
smw_wiiu_glue(wiiu_net src/wiiu_net.cpp)

# ---- Hooks into upstream. For each symbol below the linker sends every call to __wrap_<symbol>
# (our code in src/); the original stays reachable as __real_<symbol>.
set(SMW_WRAPPED_SYMBOLS
    # crash log and boot checks: src/wiiu_crashlog.cpp
    SDL_RunApp
    SDL_ShowSimpleMessageBox
    exit
    abort
    # content and SD card locations: src/wiiu_paths.cpp
    SDL_GetBasePath
    SDL_GetPrefPath
    # controllers: src/wiiu_input.cpp
    SDL_PollEvent
    SDL_WaitEvent
    SDL_GetJoystickNameForID
    SDL_GetJoystickFromPlayerIndex
    # network readiness: src/wiiu_net.cpp
    enet_initialize
    enet_host_create
    enet_host_connect)
foreach(sym IN LISTS SMW_WRAPPED_SYMBOLS)
    target_link_options(smw PRIVATE "-Wl,--wrap=${sym}")
endforeach()

# Link map: records every function's address (look up the "called from 0x..." addresses in smw-log.txt).
target_link_options(smw PRIVATE "-Wl,-Map=${CMAKE_BINARY_DIR}/smw.map")

# ==== SuperMarioWar-WiiU.wuhb ======================================================================
# `smw` is created in upstream's directory, so POST_BUILD can't be attached to it from here (this is also why
# wut's wut_create_rpx()/wut_create_wuhb() can't be used); use separate targets that depend on it.
# smw.rpx is only an intermediate for wuhbtool.
# WUT_ELF2RPL_EXE / WUT_WUHBTOOL_EXE are set by WiiU.cmake.
add_custom_target(smw_rpx ALL
    COMMAND ${WUT_ELF2RPL_EXE} $<TARGET_FILE:smw> ${CMAKE_BINARY_DIR}/smw.rpx
    BYPRODUCTS ${CMAKE_BINARY_DIR}/smw.rpx
    DEPENDS smw
    COMMENT "Creating smw.rpx"
    VERBATIM)

# The game data becomes the .wuhb's /vol/content. Upstream looks for "data/" under the base path (see
# src/wiiu_paths.cpp), so the content folder holds a data/ folder rather than being the data folder itself.
# Staged as a copy (wuhbtool is not known to follow symlinks); a `.git` file in the submodule is not content.
set(SMW_WIIU_CONTENT_STAGE "${CMAKE_BINARY_DIR}/content")
add_custom_target(smw_content ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${SMW_WIIU_CONTENT_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/supermariowar/data" "${SMW_WIIU_CONTENT_STAGE}/data"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${SMW_WIIU_CONTENT_STAGE}/data/.git"
    COMMENT "Staging game data for the wuhb"
    VERBATIM)

add_custom_target(smw_wuhb ALL
    COMMAND ${WUT_WUHBTOOL_EXE} ${CMAKE_BINARY_DIR}/smw.rpx ${CMAKE_BINARY_DIR}/SuperMarioWar-WiiU.wuhb
            --content ${SMW_WIIU_CONTENT_STAGE}
            --name "Super Mario War Wii U" --short-name "Super Mario War" --author "Tantric (Daryl Borth)"
            --icon ${CMAKE_CURRENT_SOURCE_DIR}/meta/icon.png
            --tv-image ${CMAKE_CURRENT_SOURCE_DIR}/meta/tv_splash.png
            --drc-image ${CMAKE_CURRENT_SOURCE_DIR}/meta/drc_splash.png
    BYPRODUCTS ${CMAKE_BINARY_DIR}/SuperMarioWar-WiiU.wuhb
    DEPENDS smw_rpx smw_content
    COMMENT "Creating SuperMarioWar-WiiU.wuhb"
    VERBATIM)
