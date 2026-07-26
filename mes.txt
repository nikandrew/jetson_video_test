cmake_minimum_required(VERSION 3.10)

project(jetson_camera_viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)

add_executable(jetson_camera_viewer src/main.cpp)
target_link_libraries(jetson_camera_viewer PRIVATE PkgConfig::GSTREAMER)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(jetson_camera_viewer PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS jetson_camera_viewer RUNTIME DESTINATION bin)
