cmake_minimum_required(VERSION 3.21)

project(starwarsvp VERSION 0.1.0 LANGUAGES C CXX)

# Builds (see README.md):
#   cmake --preset linux && cmake --build --preset linux   # development, native Linux
#   cmake --preset pc    && cmake --build --preset pc      # Windows .exe (WSL2, MinGW)
#   cmake --preset pi    && cmake --build --preset pi      # the cabinet (Pi 5, arm64)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- the VectorPie SDK, unmodified, as a submodule --------------------------

set(SWVP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/VectorPie/sdk"
    CACHE PATH "VectorPie SDK source directory")
if(NOT EXISTS "${SWVP_SDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "VectorPie SDK not found in ${SWVP_SDK_DIR}: "
                        "git submodule update --init")
endif()
set(VP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(VP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SWVP_SDK_DIR}" vectorpie EXCLUDE_FROM_ALL)

# --- third party ------------------------------------------------------------

# miniz (MIT): reads the MAME ROM zip.
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_options(miniz PRIVATE -w)

# --- the game core: no SDK, no SDL, testable on its own ----------------------

add_library(swcore STATIC
    src/core/sched.c
    src/rom/romset.c
    src/avg/avg.c
    src/mbox/mbox.c
    src/mbox/mbox_programs.c
)
target_include_directories(swcore PUBLIC src)
target_link_libraries(swcore PUBLIC miniz)
target_compile_options(swcore PRIVATE -Wall -Wextra)
if(UNIX)
    target_link_libraries(swcore PUBLIC m)
endif()

# --- output: AVG display lists to the VectorPie device layer ----------------

add_library(swout STATIC
    src/render/dvgmap.c
    src/render/config.c
    src/platform/app.c
)
target_link_libraries(swout PUBLIC swcore vectorpie)
target_compile_options(swout PRIVATE -Wall -Wextra)

# --- programs -----------------------------------------------------------------

add_executable(starwarsvp src/main.c)
target_link_libraries(starwarsvp PRIVATE swout)
vp_program(starwarsvp)

# Plays back display lists recorded from MAME (tools/mame/swcapture.lua)
# on the board or in a window: checks the AVG interpreter and the monitor
# calibration before any game code exists.
add_executable(avgreplay tools/avgreplay/main.c src/capture/capture.c)
target_link_libraries(avgreplay PRIVATE swout)
vp_program(avgreplay)

# --- tests --------------------------------------------------------------------

include(CTest)
if(BUILD_TESTING)
    add_executable(test_avg tests/test_avg.c)
    target_link_libraries(test_avg PRIVATE swcore)
    add_test(NAME avg COMMAND test_avg)

    add_executable(test_sched tests/test_sched.c)
    target_link_libraries(test_sched PRIVATE swcore)
    add_test(NAME sched COMMAND test_sched)

    add_executable(test_mbox tests/test_mbox.c)
    target_link_libraries(test_mbox PRIVATE swcore)
    target_include_directories(test_mbox PRIVATE tests)
    add_test(NAME mbox COMMAND test_mbox)

    add_executable(test_capture tests/test_capture.c src/capture/capture.c)
    target_link_libraries(test_capture PRIVATE swcore)
    add_test(NAME capture COMMAND test_capture)
endif()

# --- package files next to the programs (vp_asset_path looks there) ---------

configure_file(package/starwarsvp.ini ${CMAKE_CURRENT_BINARY_DIR}/starwarsvp.ini COPYONLY)
configure_file(package/game.ini ${CMAKE_CURRENT_BINARY_DIR}/game.ini COPYONLY)
