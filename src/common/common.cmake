set(MESSAGES_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/HttpServer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Messages.cpp
  ${CMAKE_CURRENT_LIST_DIR}/Plast.cpp
  ${CMAKE_CURRENT_LIST_DIR}/WebSocket.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../json11/json11.cpp)

add_library(common ${MESSAGES_SOURCES})

if (APPLE)
  find_library(SECURITY_LIBRARY Security)
  target_link_libraries(common ${SECURITY_LIBRARY})
endif()

# find_library(WSLAY_LIBRARY wslay HINTS ${CMAKE_CURRENT_LIST_DIR}/../lib)
target_link_libraries(common -L${CMAKE_CURRENT_LIST_DIR}/../lib -lwslay -Wl,-rpath,${CMAKE_CURRENT_LIST_DIR}/../lib)

include_directories(${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../json11)
