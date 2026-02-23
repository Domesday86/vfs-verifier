set(TARGET_NAME vfs-verifier)

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

# Add include directories
target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(${TARGET_NAME} PRIVATE 
    spdlog::spdlog
    fmt::fmt
)