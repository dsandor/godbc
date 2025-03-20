cmake_minimum_required(VERSION 3.10)
project(godbc LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Go
find_program(GO_EXECUTABLE go REQUIRED)
message(STATUS "Found Go: ${GO_EXECUTABLE}")

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Set RPATH settings
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Build Go bridge
add_custom_target(godbc_bridge ALL
    COMMAND ${GO_EXECUTABLE} build -buildmode=c-shared -o ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.dylib ${CMAKE_SOURCE_DIR}/bridge/bridge.go
    COMMAND install_name_tool -id "@executable_path/libgodbc_bridge.dylib" ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.dylib
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bridge/bridge.h ${CMAKE_BINARY_DIR}/include/bridge/bridge.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building Go bridge"
)

# Create directories
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/bridge)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Create interface library for header-only C++ wrapper
add_library(godbc INTERFACE)
target_include_directories(godbc INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link with Go bridge
add_dependencies(godbc godbc_bridge)
target_link_libraries(godbc INTERFACE ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.dylib)

# Add macOS framework dependencies
if(APPLE)
    target_link_libraries(godbc INTERFACE
        "-framework CoreFoundation"
        "-framework Security"
    )
endif()

# Add example executables
add_executable(example examples/example.cpp)
add_executable(benchmark examples/benchmark.cpp)
add_executable(query_examples examples/query_examples.cpp)

target_link_libraries(example PRIVATE godbc)
target_link_libraries(benchmark PRIVATE godbc pthread)
target_link_libraries(query_examples PRIVATE godbc)

# Add custom command to copy dylib to bin directory
add_custom_command(TARGET query_examples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.dylib
    ${CMAKE_BINARY_DIR}/bin/libgodbc_bridge.dylib
)
