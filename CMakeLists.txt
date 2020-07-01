cmake_minimum_required(VERSION 3.16)
project(ZRA)

include(GenerateExportHeader)

if (uppercase_CMAKE_BUILD_TYPE STREQUAL "RELEASE")
    add_compile_definitions(NDEBUG)
endif ()

if (WIN32 AND NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

find_package(zstd REQUIRED)

include_directories("include")

add_library(zra SHARED source/zra.cpp)
target_link_libraries(zra zstd::libzstd_static)
GENERATE_EXPORT_HEADER(zra)

add_executable(ZraTool zratool.cpp)
target_link_libraries(ZraTool zra)
