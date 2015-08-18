cmake_minimum_required(VERSION 2.8)

include_directories(${CMAKE_CURRENT_LIST_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

set(SOURCES
    CompilerArgs.cpp
    CompilerVersion.cpp
    Daemon.cpp
    Http.cpp
    Job.cpp
    Local.cpp
    Preprocessor.cpp
    ProcessPool.cpp
    Remote.cpp
    plastd.cpp)

find_package(CURL)
if(CURL_FOUND)
  include_directories(${CURL_INCLUDE_DIR})
else()
  message(FATAL_ERROR "Could not find the CURL library and development files.")
endif()

add_executable(plastd ${SOURCES})
target_link_libraries(plastd rct common ${CURL_LIBRARIES})
