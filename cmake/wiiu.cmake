# Wii U build of Super Mario War (included from the top-level CMakeLists.txt; independent of cmake/wii.cmake).
#
# Upstream (supermariowar/) and the Wii U SDL3 fork (SDL3-wut/, dborth/SDL branch wut-port) are git submodules.
# The Wii U code in src/wiiu_*.cpp is linked into upstream's `smw` target; upstream itself is not modified.
#
# Prerequisites: devkitPPC + wut (`dkp-pacman -S wiiu-dev`; already in the libogc2 CI image), and the SDL3 fork already BUILT AND INSTALLED
# (.github/workflows/build.yml has the exact commands).
#
#   cmake -S . -B build-wiiu --toolchain $DEVKITPRO/cmake/WiiU.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-wiiu       # -> build-wiiu/smw.wuhb

# ==== Settings =====================================================================================

# Where everything lives on the SD card (wut mounts it as fs:/vol/external01):
#   /smw/data/   game data (the contents of supermariowar/data)
#   /smw/        settings
set(SMW_WIIU_ROOT "fs:/vol/external01/smw/" CACHE STRING "SD card folder for data and settings (must end in '/')")
if(NOT SMW_WIIU_ROOT MATCHES "/$")
    message(FATAL_ERROR "SMW_WIIU_ROOT must end in '/' (got '${SMW_WIIU_ROOT}')")
endif()

# Options of upstream's own CMake project.
set(NO_NETWORK ON CACHE BOOL "" FORCE)             # netplay not ported yet (ENet on wut sockets is untested)
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

# ==== Wii U glue (src/) ============================================================================
# Each file is its own OBJECT library; the objects are linked into upstream's `smw`.

function(smw_wiiu_glue name source)
    add_library(${name} OBJECT ${source})
    target_compile_definitions(${name} PRIVATE "SMW_ROOT=\"${SMW_WIIU_ROOT}\"")
    target_link_libraries(${name} PRIVATE SDL3::SDL3)   # headers only (OBJECT library)
    target_sources(smw PRIVATE $<TARGET_OBJECTS:${name}>)
endfunction()

# SD card locations.
smw_wiiu_glue(wiiu_paths src/wiiu_paths.cpp)

# Controller hotplug + default bindings. Needs upstream's headers (GameValues.h, input.h).
smw_wiiu_glue(wiiu_input src/wiiu_input.cpp)
target_compile_features(wiiu_input PRIVATE cxx_std_20)
target_include_directories(wiiu_input PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/supermariowar/src/common")

# ---- Hooks into upstream. For each symbol below the linker sends every call to __wrap_<symbol>
# (our code in src/); the original stays reachable as __real_<symbol>.
set(SMW_WRAPPED_SYMBOLS
    # SD card locations: src/wiiu_paths.cpp
    SDL_GetBasePath
    SDL_GetPrefPath
    # controllers: src/wiiu_input.cpp
    SDL_PollEvent
    SDL_WaitEvent
    SDL_GetJoystickNameForID
    SDL_GetJoystickFromPlayerIndex)
foreach(sym IN LISTS SMW_WRAPPED_SYMBOLS)
    target_link_options(smw PRIVATE "-Wl,--wrap=${sym}")
endforeach()

# Upstream strips the ELF (-s) in non-Debug builds; a link map still records every function's address.
target_link_options(smw PRIVATE "-Wl,-Map=${CMAKE_BINARY_DIR}/smw.map")

# ==== smw.wuhb ===========================================================================
# `smw` is created in upstream's directory, so POST_BUILD can't be attached to it from here (this is also why
# wut's wut_create_rpx() can't be used: it is a POST_BUILD wrapper); use separate targets that depend on it.
# WUT_ELF2RPL_EXE / WUT_WUHBTOOL_EXE are found by WiiU.cmake (package wut-tools).
if(NOT WUT_ELF2RPL_EXE)
    message(FATAL_ERROR "elf2rpl not found: install wut-tools (dkp-pacman -S wiiu-dev)")
endif()
add_custom_target(smw_rpx ALL
    COMMAND ${WUT_ELF2RPL_EXE} $<TARGET_FILE:smw> ${CMAKE_BINARY_DIR}/smw.rpx
    BYPRODUCTS ${CMAKE_BINARY_DIR}/smw.rpx
    DEPENDS smw
    COMMENT "Creating smw.rpx"
    VERBATIM)

# .wuhb bundle (Aroma)
# No --content yet: the game data is read from the SD card for now, we're put it into the bundle later
if(WUT_WUHBTOOL_EXE)
    add_custom_target(smw_wuhb ALL
        COMMAND ${WUT_WUHBTOOL_EXE} ${CMAKE_BINARY_DIR}/smw.rpx ${CMAKE_BINARY_DIR}/smw.wuhb
                --name "Super Mario War Wii U" --short-name "Super Mario War" --author "Tantric (Daryl Borth)"
                --icon ${CMAKE_CURRENT_SOURCE_DIR}/meta/icon.png
                --tv-image ${CMAKE_CURRENT_SOURCE_DIR}/meta/tv_splash.png
                --drc-image ${CMAKE_CURRENT_SOURCE_DIR}/meta/drc_splash.png
        BYPRODUCTS ${CMAKE_BINARY_DIR}/smw.wuhb
        DEPENDS smw_rpx
        COMMENT "Creating smw.wuhb"
        VERBATIM)
endif()
