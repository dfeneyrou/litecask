cmake_minimum_required(VERSION 3.15.0)

project(Litecask VERSION 1.0.0 DESCRIPTION "Litecask is a key value store based on bitcask")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif(CCACHE_PROGRAM)

# Set canonical output directories
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Type of build" FORCE)
endif()
message("Build type: ${CMAKE_BUILD_TYPE}")

# Options
# =======

if(MSVC)
  add_compile_options(/W4 /permissive-)
  add_compile_options(/wd4996) # Disable the "This function or variable may be unsafe", pushing for
                               # not well supported extensions
  add_compile_options(/wd4324) # Disable the "Structure was padded due to alignment specifier"
else(MSVC)

  add_compile_options(-Wall -Wno-unused-function -Wconversion -Wextra -Wpedantic)
  add_compile_options(-Wno-gnu-zero-variadic-macro-arguments -Wno-sign-conversion)

  if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
    message("Address sanitizer is enabled.")
  endif()

  if(ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
    message("Undefined Behavior sanitizer is enabled.")
  endif()

  if(ENABLE_TSAN)
    # 1) Preferably use clang, which has less false positive:

    # cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=1 -DCMAKE_CXX_COMPILER=/usr/bin/clang++ ..

    # 2) use the provided suppression file

    # TSAN_OPTIONS="suppressions=../ci/tsan_suppression.supp" ./bin/litecask_test

    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
    message("Thread sanitizer is enabled.")
  endif()
endif(MSVC)

# System flags
# ============

# Register our package finders
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

# Multithreading is used (both client and server applications)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Project
# =======

# External dependencies (interfaces)
add_subdirectory(external)

# Header only library and its tests
add_subdirectory(lib)

# Associated tools
add_subdirectory(apps)

# Custom target to call tests
add_custom_target(test ${CMAKE_BINARY_DIR}/bin/litecask_test DEPENDS litecask_test)
