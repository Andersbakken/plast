cmake_minimum_required(VERSION 2.8)

set(RCT_STATIC 1)
set(RCT_RTTI_ENABLED 1)

include(rct/rct.cmake)
include(common/common.cmake)

if (NOT PLAST_DATA_PREFIX)
  if (CMAKE_INSTALL_PREFIX STREQUAL "/usr" OR CMAKE_INSTALL_PREFIX STREQUAL "/usr/")
    set(PLAST_DATA_PREFIX "/")
  else ()
    set(PLAST_DATA_PREFIX ${CMAKE_INSTALL_PREFIX})
  endif ()
endif ()
add_definitions(-DPLAST_DATA_PREFIX="${PLAST_DATA_PREFIX}")
add_subdirectory(plasts)
add_subdirectory(plastd)
add_subdirectory(plastc)

include(ExternalProject)
ExternalProject_Add(
  wslay
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/wslay
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/wslay
  UPDATE_COMMAND autoreconf -i && automake && autoconf
  CONFIGURE_COMMAND <SOURCE_DIR>/configure CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} "CXX_FLAGS=${EXTERNAL_CXX_FLAGS}" "LDFLAGS=${EXTERNAL_LD_FLAGS}" --prefix=${CMAKE_CURRENT_BINARY_DIR}/wslay-install --enable-shared=false
  BUILD_COMMAND make -C lib -j
  INSTALL_COMMAND make -C lib -j install
  )

add_dependencies(common wslay)
add_dependencies(plasts rct common)
add_dependencies(plastd rct common)
add_dependencies(plastc rct common)
