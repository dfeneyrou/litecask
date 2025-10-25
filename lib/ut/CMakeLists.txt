# Test executable
# ===============

add_executable(litecask_test)
target_sources(
  litecask_test
  PRIVATE test_main.cpp
          test_basic.cpp
          test_cache.cpp
          test_threading.cpp
          test_benchmark.cpp
          test_keydir.cpp
          test_malloc.cpp
          test_ttl.cpp
          test_index.cpp)
target_link_libraries(litecask_test PRIVATE libexternal litecask Threads::Threads)

# Display some build information
add_custom_command(
  TARGET litecask_test POST_BUILD COMMAND ${CMAKE_COMMAND} -E echo
                                          "Using ${CMAKE_CXX_COMPILER} [${CMAKE_BUILD_TYPE}]"
  VERBATIM)
