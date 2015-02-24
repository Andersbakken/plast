cmake_minimum_required(VERSION 2.8)

include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

set(SOURCES
    plasts.cpp
    Peer.cpp
    Scheduler.cpp)

add_executable(plasts ${SOURCES})
target_link_libraries(plasts rct common)

add_custom_command(TARGET plasts POST_BUILD
  COMMAND ln -sf ${CMAKE_CURRENT_LIST_DIR}/stats ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/stats
)
