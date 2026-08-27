# Compile-time Protobuf via C++26 P2996 Reflection
#
# Header-only library. The .proto file is #embed-ed at compile time; the
# parser runs at compile time; the struct is spliced at compile time.
# No code generation step, no protoc dependency.

cmake_minimum_required(VERSION 3.25)
project(ctpb LANGUAGES CXX)

# C++26 — required for #embed (P1967) and static reflection (P2996).
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-freflection" CTPB_HAS_FREFLECTION_FLAG)
if(CTPB_HAS_FREFLECTION_FLAG)
    add_compile_options(-freflection)
endif()

option(CTPB_BUILD_TESTS "Build ctpb tests" ON)
option(CTPB_BUILD_BENCHMARKS "Build ctpb benchmarks" OFF)
option(CTPB_BUILD_PAPER "Build ctpb paper (requires LaTeX)" OFF)

# Header-only target. Consumers just `target_link_libraries(... ctpb::ctpb)`.
add_library(ctpb INTERFACE)
add_library(ctpb::ctpb ALIAS ctpb)
target_include_directories(ctpb INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(ctpb INTERFACE cxx_std_26)

if(CTPB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CTPB_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

add_subdirectory(examples)

if(CTPB_BUILD_PAPER)
    add_subdirectory(paper)
    add_subdirectory(presentation)
endif()
