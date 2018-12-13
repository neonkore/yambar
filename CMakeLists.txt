cmake_minimum_required(VERSION 3.9)
project(foobar C)

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "-Wall -Werror ${CMAKE_C_FLAGS}")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(XCB REQUIRED xcb xcb-randr xcb-render)
pkg_check_modules(CAIRO REQUIRED cairo cairo-xcb)
pkg_check_modules(YAML REQUIRED yaml-0.1)

add_executable(foobar
  bar.c bar.h
  config.c config.h
  font.c font.h
  main.c
  module.c module.h
  particle.c particle.h
  tag.c tag.h
  xcb.c xcb.h
  yml.c yml.h

  particles/string.c particles/string.h
  particles/list.c particles/list.h
  particles/map.c particles/map.h

  modules/label/label.c modules/label/label.h
  modules/clock/clock.c modules/clock/clock.h
  modules/xwindow/xwindow.c modules/xwindow/xwindow.h
  )

target_compile_definitions(foobar PRIVATE _GNU_SOURCE)

target_compile_options(foobar PRIVATE
  ${XCB_CFLAGS_OTHER}
  ${CAIRO_CFLAGS_OTHER}
  ${YAML_CFLAGS_OTHER}
  )

target_include_directories(foobar PRIVATE
  ${XCB_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  ${YAML_INCLUDE_DIRS}
  )

target_link_libraries(foobar
  ${CMAKE_THREAD_LIBS_INIT}
  ${XCB_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${YAML_LIBRARIES}
  )
