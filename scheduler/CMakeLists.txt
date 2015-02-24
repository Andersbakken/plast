cmake_minimum_required(VERSION 2.8)

include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

set(SOURCES
    main.cpp
    Peer.cpp
    Scheduler.cpp)

add_executable(scheduler ${SOURCES})
target_link_libraries(scheduler rct common)
