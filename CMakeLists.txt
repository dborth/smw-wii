# Wii wrapper for Super Mario War (mmatyas/supermariowar, `master`, SDL3).
# Upstream and the SDL3 ogc fork are submodules
#
#   cmake -S . -B build-wii --toolchain /opt/devkitpro/devkitPPC/cmake/Wii.cmake
#   cmake --build build-wii --target smw_dol      # -> build-wii/boot.dol
cmake_minimum_required(VERSION 3.25)
project(smw-console LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")
    message(FATAL_ERROR "Unsupported system '${CMAKE_SYSTEM_NAME}': configure with devkitPPC's Wii.cmake toolchain")
endif()

# ---- upstream options -------------------------------------------------------
set(NO_NETWORK ON CACHE BOOL "" FORCE)             # no ENet for the proof of concept
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SMW_INSTALL_PORTABLE ON CACHE BOOL "" FORCE)   # data dir = ./data next to the dol

# ---- SDL3: use the copy you built and installed (the libogc2 fork), do NOT build it again.
# Upstream declares SDL3/SDL3_image/SDL3_mixer with FIND_PACKAGE_ARGS, so find_package() is tried
# first -- unless FETCHCONTENT_SOURCE_DIR_SDL3 is set, which forces a from-source build.
# (Do not set that variable.) We only have to make sure the install prefix is searched:
set(SMW_SDL3_PREFIX "$ENV{DEVKITPRO}/libogc2/wii" CACHE PATH "where `make install` put SDL3")
list(APPEND CMAKE_PREFIX_PATH "${SMW_SDL3_PREFIX}")
list(APPEND CMAKE_FIND_ROOT_PATH "${SMW_SDL3_PREFIX}")
find_package(SDL3 REQUIRED CONFIG)   # fail early, with a clear message, if it isn't found
message(STATUS "Using installed SDL3 ${SDL3_VERSION} from ${SDL3_DIR}")

# SDL3_image/SDL3_mixer assume every platform but a short hardcoded list can build shared libs and
# run option(CMAKE_POSITION_INDEPENDENT_CODE ... ON). That leaks -fPIE into every later try_compile
# and target, which is incompatible with devkitPPC's -msdata=sysv (every configure check then fails
# with "'-fPIC' and '-msdata=sysv' are incompatible"). option() keeps an existing cache value, so:
set(CMAKE_POSITION_INDEPENDENT_CODE OFF CACHE BOOL "" FORCE)

# SDL3_image / SDL3_mixer are still built from source (vendored). Keep them minimal:
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

# Upstream assumes it is the top-level project (CMAKE_SOURCE_DIR/cmake)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/supermariowar/cmake")

# EXCLUDE_FROM_ALL: we only want the `smw` target, not the editors/server
add_subdirectory(supermariowar EXCLUDE_FROM_ALL)

# ---- source-level shims without touching upstream --------------------------------
# Game.cpp only includes <sys/types.h>/<sys/stat.h>/<unistd.h> under `#if defined(unix) || __unix__ ||
# __APPLE__`, none of which devkitPPC defines (stat/mkdir/S_ISDIR then fail to compile).
# Force-include exactly what that block would have. (Defining __unix__ instead could flip other code paths.)
target_compile_options(CommonFiles PRIVATE
    "SHELL:-include sys/types.h"
    "SHELL:-include sys/stat.h"
    "SHELL:-include unistd.h")

# ---- where files live on the SD card (hard-coded; see src/wii_hooks.cpp) --------------
set(SMW_WII_ROOT "sd:/smw/" CACHE STRING "SD card folder: data in <root>data/, settings and log.txt in <root>")
option(SMW_WII_LOG "Write stdout/stderr to <root>log.txt" ON)

add_library(wii_hooks OBJECT src/wii_hooks.cpp)
target_compile_definitions(wii_hooks PRIVATE "SMW_WII_ROOT=\"${SMW_WII_ROOT}\"")
if(SMW_WII_LOG)
    target_compile_definitions(wii_hooks PRIVATE SMW_WII_LOG)
endif()
target_link_libraries(wii_hooks PRIVATE SDL3::SDL3)   # headers only (OBJECT library)
target_sources(smw PRIVATE $<TARGET_OBJECTS:wii_hooks>)

# Controller hotplug + default bindings (src/wii_input.cpp). Needs upstream's headers (GameValues.h, input.h),
# which upstream exposes with a directory-level include_directories(), so add the path explicitly.
add_library(wii_input OBJECT src/wii_input.cpp)
target_compile_features(wii_input PRIVATE cxx_std_20)
target_include_directories(wii_input PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/supermariowar/src/common")
target_compile_definitions(wii_input PRIVATE "SMW_WII_ROOT=\"${SMW_WII_ROOT}\"")
target_link_libraries(wii_input PRIVATE SDL3::SDL3)   # headers only (OBJECT library)
target_sources(smw PRIVATE $<TARGET_OBJECTS:wii_input>)

target_link_options(smw PRIVATE
    -Wl,--wrap=SDL_GetBasePath
    -Wl,--wrap=SDL_GetPrefPath
    -Wl,--wrap=SDL_PollEvent
    -Wl,--wrap=SDL_WaitEvent
	-Wl,--wrap=SDL_GetJoystickNameForID
    -Wl,--wrap=SDL_GetJoystickFromPlayerIndex
    -Wl,--wrap=IMG_Load
    # Upstream strips the ELF (-s) in non-Debug builds, so addr2line/nm are useless. A link map still
    # records every function's address: python3 tools/addr2map.py build-wii/smw.map <addr>...
    -Wl,-Map=${CMAKE_BINARY_DIR}/smw.map)

# ---- boot.dol ----------------------------------------------------------------
# `smw` is created in upstream's directory, so POST_BUILD can't be attached to it from here;
# use a separate target that depends on it.
find_program(ELF2DOL elf2dol REQUIRED)
add_custom_target(smw_dol ALL
    COMMAND ${ELF2DOL} $<TARGET_FILE:smw> ${CMAKE_BINARY_DIR}/boot.dol
    DEPENDS smw
    COMMENT "Creating boot.dol"
    VERBATIM)
