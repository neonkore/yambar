cmake_minimum_required(VERSION 3.13)

add_library(particle-sdk INTERFACE)
target_compile_definitions(particle-sdk INTERFACE _GNU_SOURCE)
target_compile_options(particle-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(particle-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})

set(CMAKE_SHARED_MODULE_PREFIX particle_)

add_library(empty MODULE empty.c empty.h)
target_link_libraries(empty particle-sdk)

add_library(list MODULE list.c list.h)
target_link_libraries(list particle-sdk)

add_library(map MODULE map.c map.h)
target_link_libraries(map particle-sdk)

add_library(progress-bar MODULE progress-bar.c progress-bar.h)
target_link_libraries(progress-bar particle-sdk)

add_library(ramp MODULE ramp.c ramp.h)
target_link_libraries(ramp particle-sdk)

add_library(string MODULE string.c string.h)
target_link_libraries(string particle-sdk)

install(
  TARGETS
  empty
  list
  map
  progress-bar
  ramp
  string

  DESTINATION lib/f00bar)
