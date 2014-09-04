cmake_minimum_required(VERSION 2.8.6)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif ()

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstack-protector")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-all")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstack-protector")

set(RCT_STATIC 1)
add_subdirectory(rct)

include(${CMAKE_CURRENT_LIST_DIR}/rct/compiler.cmake)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
  add_definitions("-DPLAST_DEBUG")
endif ()

add_definitions("-Wall")
add_definitions("-DOS_${CMAKE_SYSTEM_NAME}")

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(SYSTEM_LIBS pthread rt crypto m dl)
elseif (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  add_definitions(-D__LONG_LONG_SUPPORTED)
  set(SYSTEM_LIBS pthread crypto)
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
  set(SYSTEM_LIBS crypto curses)
  find_program(SW_VER sw_vers)
  execute_process(COMMAND "${SW_VER}" -productVersion OUTPUT_VARIABLE osver)
  string(REPLACE "\n" "" osver "${osver}")
  if (${osver} VERSION_LESS "10.7")
    message("OS X ${osver} is too old for FSEvents, falling back to kqueue")
    # else ()
    #   set(HAVE_FSEVENTS 1)
  endif ()
endif ()
list(APPEND SYSTEM_LIBS ${LLVM_LDFLAGS})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

add_library(plast Plast.cpp)
target_link_libraries(plast rct)

include_directories(${CMAKE_CURRENT_LIST_DIR}
                    rct/include/
                    rct
                    ${CMAKE_CURRENT_BINARY_DIR}/rct/include/rct
                    ${Readline_INCLUDE_DIR})
                    # ${LibMagic_INCLUDE_DIR})

add_executable(plastc plastc.cpp)
add_dependencies(plastc rct plast)
target_link_libraries(plastc plast rct ${SYSTEM_LIBS})

add_executable(plastd plastd.cpp Daemon.cpp Console.cpp Compiler.cpp)
add_dependencies(plastd rct plast)
target_link_libraries(plastd plast rct ${SYSTEM_LIBS} ${Readline_LIBRARY}) # ${LibMagic_LIBRARY})

add_executable(plasts plasts.cpp Server.cpp Console.cpp)
add_dependencies(plasts rct plast)
target_link_libraries(plasts plast rct ${SYSTEM_LIBS} ${Readline_LIBRARY})

install(CODE "message(\"Installing plast...\")")
install(TARGETS plastc plastd plasts RUNTIME DESTINATION bin COMPONENT plast)
#install(FILES ../man/rc.7 ../man/rdm.7 DESTINATION share/man/man7/)
