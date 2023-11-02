cmake_minimum_required(VERSION 3.15.0)

project(libexternal)

add_library(libexternal INTERFACE)
target_include_directories(libexternal INTERFACE ${CMAKE_CURRENT_LIST_DIR})

