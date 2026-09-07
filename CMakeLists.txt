cmake_minimum_required(VERSION 3.16)

# This project can be built stand-alone or pulled in with add_subdirectory()
# from a parent project (e.g. ld-decode-tools). Only take ownership of the
# global settings when we are the top-level project.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(vfs-tools VERSION 1.0.0 LANGUAGES CXX)

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

# Code shared by every tool in this repository
file(GLOB COMMON_SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp)

add_library(vfs-common STATIC ${COMMON_SRC_FILES})

target_compile_features(vfs-common PUBLIC cxx_std_17)

target_include_directories(vfs-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_link_libraries(vfs-common PUBLIC
    spdlog::spdlog
    fmt::fmt
)

# Enable common warnings (kept PRIVATE so they do not leak into a parent project)
if(MSVC)
    set(VFS_WARNING_FLAGS /W4)
else()
    set(VFS_WARNING_FLAGS -Wall -Wextra)
endif()

target_compile_options(vfs-common PRIVATE ${VFS_WARNING_FLAGS})

# Each tool is one executable built from its own subdirectory of src/
function(add_vfs_tool TARGET_NAME SOURCE_DIR)
    file(GLOB_RECURSE TOOL_SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_DIR}/*.cpp)

    add_executable(${TARGET_NAME} ${TOOL_SRC_FILES})

    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
    target_compile_options(${TARGET_NAME} PRIVATE ${VFS_WARNING_FLAGS})

    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_DIR}
    )

    target_link_libraries(${TARGET_NAME} PRIVATE vfs-common)

    install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endfunction()

# vfs-verifier - checks one image against its bad sector map
add_vfs_tool(vfs-verifier src/verifier)

# vfs-stacker - combines the good sectors of several images into one
add_vfs_tool(vfs-stacker src/stacker)
