cmake_minimum_required(VERSION 3.15.0)

project(litecask)

add_library(litecask INTERFACE)
target_include_directories(litecask INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Include the tests
add_subdirectory(ut)
