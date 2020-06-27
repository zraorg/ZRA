cmake_minimum_required(VERSION 3.16)
project(ZRA)

if (uppercase_CMAKE_BUILD_TYPE STREQUAL "RELEASE")
    add_compile_definitions(NDEBUG)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    #    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif ()

find_package(zstd REQUIRED)

add_library(ZRA STATIC zra.cpp)
target_link_libraries(ZRA zstd::libzstd_static)

add_executable(ZraTool zratool.cpp)
target_link_libraries(ZraTool ZRA)
