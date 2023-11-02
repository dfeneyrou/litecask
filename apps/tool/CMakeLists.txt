project(litecask_tool)

add_executable(litecask_tool)
target_sources(litecask_tool PRIVATE main.cpp)
target_link_libraries(litecask_tool PRIVATE libexternal litecask Threads::Threads)
