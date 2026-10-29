# Super Mario War for the Wii and the Wii U: thin wrappers around upstream (mmatyas/supermariowar, SDL3).
#
# The two consoles share nothing but this dispatcher. Each has its own CMake file, SDL3 fork submodule, glue
# sources and CI job, and duplication between them is deliberate:
#
#   Wii    cmake/wii.cmake    src/wii_*.cpp     SDL3-libogc2   Wii.cmake toolchain
#   Wii U  cmake/wiiu.cmake   src/wiiu_*.cpp    SDL3-wut       WiiU.cmake toolchain
cmake_minimum_required(VERSION 3.25)
project(smw-console LANGUAGES C CXX)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")
    include(cmake/wii.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "CafeOS")   # set by devkitPro's WiiU.cmake
    include(cmake/wiiu.cmake)
else()
    message(FATAL_ERROR "Unsupported system '${CMAKE_SYSTEM_NAME}': configure with devkitPPC's Wii.cmake or WiiU.cmake toolchain")
endif()
