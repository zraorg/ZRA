cmake_minimum_required(VERSION 3.16)
project(ZRA)

include(GenerateExportHeader)

if (uppercase_CMAKE_BUILD_TYPE STREQUAL "RELEASE")
    add_compile_definitions(NDEBUG)
endif ()

if (WIN32 AND NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
    set(LINK_OPTIONS "${LINK_OPTIONS} --allow-multiple-definition")
endif ()

if ((CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU") OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-unknown-pragmas")
endif()

option(OUT_DIRECTORY "If to output to a fixed 'out' directory" FALSE)

if (OUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/out/")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/out/")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/")
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT ON CACHE BOOL "" FORCE)

add_subdirectory(submodule/zstd/build/cmake)

include_directories("include")

add_library(zra SHARED source/zra.cpp)
target_link_libraries(zra PRIVATE libzstd_static)
target_compile_definitions(zra PUBLIC ZRA_EXPORT_HEADER=true)
target_include_directories(zra PUBLIC ${CMAKE_BINARY_DIR}/exports PRIVATE submodule/zstd/lib PRIVATE submodule/CRCpp/inc)
GENERATE_EXPORT_HEADER(zra EXPORT_FILE_NAME ${CMAKE_BINARY_DIR}/exports/zra_export.h)

option(BUILD_ZRATOOL "If ZraTool is build or not" FALSE)

if (BUILD_ZRATOOL)
    add_executable(ZraTool zratool.cpp)
    target_link_libraries(ZraTool zra)
endif ()