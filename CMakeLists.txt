cmake_minimum_required(VERSION 2.8)

set(RCT_STATIC 1)

include(rct/rct.cmake)
include(common/common.cmake)

add_subdirectory(scheduler)
add_subdirectory(daemon)
add_subdirectory(client)

include(ExternalProject)
ExternalProject_Add(
  wslay
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/wslay
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND autoreconf -i && automake && autoconf && ./configure CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} "CXX_FLAGS=${EXTERNAL_CXX_FLAGS}" "LDFLAGS=${EXTERNAL_LD_FLAGS}" --prefix=${CMAKE_CURRENT_BINARY_DIR}
  BUILD_COMMAND make -C lib -j
  INSTALL_COMMAND make -C lib -j install
  )

add_dependencies(common wslay)
add_dependencies(scheduler rct common)
add_dependencies(daemon rct common)
add_dependencies(plastc rct common)

if (EXISTS "rules.ninja")
  add_custom_target(release COMMAND cmake -GNinja -DCMAKE_BUILD_TYPE=Release . WORKING_DIRECTORY .)
  add_custom_target(debug COMMAND cmake -GNinja -DCMAKE_BUILD_TYPE=Debug . WORKING_DIRECTORY .)
else()
  add_custom_target(release COMMAND cmake -DCMAKE_BUILD_TYPE=Release . WORKING_DIRECTORY)
  add_custom_target(debug COMMAND cmake -DCMAKE_BUILD_TYPE=Debug . WORKING_DIRECTORY)
endif()
