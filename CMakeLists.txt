cmake_minimum_required(VERSION 3.24)
project(video-editor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system packages
find_package(Vulkan REQUIRED)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(FFmpeg REQUIRED)

# Fetch third-party dependencies
include(cmake/FetchDependencies.cmake)

# Collect sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(video-editor ${SOURCES})

target_include_directories(video-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${STB_INCLUDE_DIR}
)

target_link_libraries(video-editor PRIVATE
    SDL3::SDL3
    Vulkan::Vulkan
    imgui_lib
    imgui_filedialog
    vma
    FFmpeg::FFmpeg
)

target_compile_options(video-editor PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)
