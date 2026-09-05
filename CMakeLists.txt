cmake_minimum_required(VERSION 3.16)

set(TARGET_NAME vfs-verifier)

# This project can be built stand-alone or pulled in with add_subdirectory()
# from a parent project (e.g. ld-decode-tools). Only take ownership of the
# global settings when we are the top-level project.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(vfs-verifier VERSION 1.0.0 LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    include(GNUInstallDirs)
endif()

if(NOT TARGET spdlog::spdlog)
    find_package(spdlog REQUIRED)
endif()

if(NOT TARGET fmt::fmt)
    find_package(fmt REQUIRED)
endif()

# Add all source files from subdirectories
file(GLOB_RECURSE SRC_FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

# Create the executable target
add_executable(${TARGET_NAME} ${SRC_FILES})

target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)

# Enable common warnings (kept PRIVATE so they do not leak into a parent project)
if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /W4)
else()
    target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra)
endif()

# Add include directories
target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(${TARGET_NAME} PRIVATE 
    spdlog::spdlog
    fmt::fmt
)

install(TARGETS ${TARGET_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
