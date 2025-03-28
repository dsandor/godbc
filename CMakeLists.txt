cmake_minimum_required(VERSION 3.10)
project(godbc LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable filesystem library
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++17" COMPILER_SUPPORTS_CXX17)
if(COMPILER_SUPPORTS_CXX17)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

# Add compile definitions for filesystem
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

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
if(APPLE)
    add_custom_target(godbc_bridge ALL
        COMMAND ${GO_EXECUTABLE} build -buildmode=c-shared -o ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.dylib ${CMAKE_SOURCE_DIR}/bridge/bridge.go
        COMMAND install_name_tool -id "@executable_path/libgodbc_bridge.dylib" ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.dylib
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bridge/bridge.h ${CMAKE_BINARY_DIR}/include/bridge/bridge.h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building Go bridge"
    )
else()
    add_custom_target(godbc_bridge ALL
        COMMAND ${GO_EXECUTABLE} build -buildmode=c-shared -o ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.so ${CMAKE_SOURCE_DIR}/bridge/bridge.go
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/bridge/bridge.h ${CMAKE_BINARY_DIR}/include/bridge/bridge.h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building Go bridge"
    )
endif()

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
if(APPLE)
    target_link_libraries(godbc INTERFACE ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.dylib)
else()
    target_link_libraries(godbc INTERFACE ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.so)
endif()

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
add_executable(sql_runner examples/sql_runner.cpp)

target_link_libraries(example PRIVATE godbc)
target_link_libraries(benchmark PRIVATE godbc pthread)
target_link_libraries(query_examples PRIVATE godbc)
if(APPLE)
    target_link_libraries(sql_runner PRIVATE godbc pthread)
else()
    target_link_libraries(sql_runner PRIVATE godbc stdc++fs pthread)
endif()

# Add custom command to copy shared library to bin directory
if(APPLE)
    add_custom_command(TARGET query_examples POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.dylib
        ${CMAKE_BINARY_DIR}/bin/libgodbc_bridge.dylib
    )
else()
    add_custom_command(TARGET query_examples POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/lib/libgodbc_bridge.so
        ${CMAKE_BINARY_DIR}/bin/libgodbc_bridge.so
    )
endif()

target_include_directories(example PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
target_include_directories(benchmark PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
target_include_directories(query_examples PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
target_include_directories(sql_runner PRIVATE ${CMAKE_SOURCE_DIR}/cpp)
