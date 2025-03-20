cmake_minimum_required(VERSION 3.10)
project(godbc LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Go
find_program(GO_EXECUTABLE go REQUIRED)
message(STATUS "Found Go: ${GO_EXECUTABLE}")

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Build Go bridge
add_custom_target(godbc_bridge ALL
    COMMAND ${GO_EXECUTABLE} build -buildmode=c-archive -o ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.a ${CMAKE_SOURCE_DIR}/bridge/bridge.go
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Go bridge"
)

# Create interface library for header-only C++ wrapper
add_library(godbc INTERFACE)
target_include_directories(godbc INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Link with Go bridge
add_dependencies(godbc godbc_bridge)
target_link_libraries(godbc INTERFACE ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.a)

# Add macOS framework dependencies
if(APPLE)
    target_link_libraries(godbc INTERFACE
        "-framework CoreFoundation"
        "-framework Security"
    )
endif()

# Example executables
add_executable(example examples/example.cpp)
target_link_libraries(example PRIVATE godbc)

add_executable(benchmark examples/benchmark.cpp)
target_link_libraries(benchmark PRIVATE godbc pthread)
