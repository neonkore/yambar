cmake_minimum_required(VERSION 3.13)

add_library(particle-sdk INTERFACE)
target_compile_options(particle-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(particle-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})

add_library(dynlist SHARED dynlist.c dynlist.h)
target_link_libraries(dynlist PRIVATE particle-sdk)

set(CMAKE_SHARED_MODULE_PREFIX particle_)

add_library(empty MODULE empty.c)
target_link_libraries(empty particle-sdk)

add_library(list MODULE list.c)
target_link_libraries(list particle-sdk)

add_library(map MODULE map.c)
target_link_libraries(map particle-sdk)

add_library(progress-bar MODULE progress-bar.c)
target_link_libraries(progress-bar particle-sdk)

add_library(ramp MODULE ramp.c)
target_link_libraries(ramp particle-sdk)

add_library(string MODULE string.c)
target_link_libraries(string particle-sdk ${CAIRO_LIBRARIES})

install(
  TARGETS
  empty
  list
  map
  progress-bar
  ramp
  string

  dynlist

  DESTINATION lib/f00bar)
