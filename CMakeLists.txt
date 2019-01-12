cmake_minimum_required(VERSION 3.13)
project(f00bar C)

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

set_property(DIRECTORY . APPEND PROPERTY COMPILE_DEFINITIONS
  $<$<CONFIG:Debug>:_DEBUG>
  )

set(CMAKE_C_FLAGS "-Wall -Werror ${CMAKE_C_FLAGS}")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(XCB REQUIRED xcb xcb-randr xcb-render xcb-cursor) # Core
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)                   # Core
pkg_check_modules(CAIRO REQUIRED cairo cairo-xcb cairo-ft)          # Core
pkg_check_modules(YAML REQUIRED yaml-0.1)                           # Core (configuration)


add_executable(f00bar
  bar.c bar.h
  config.c config.h
  config-verify.c config-verify.h
  decoration.h
  font.c font.h
  log.c log.h
  main.c
  module.c module.h
  particle.c particle.h
  tag.c tag.h
  xcb.c xcb.h
  yml.c yml.h

  decorations/background.c decorations/background.h
  decorations/stack.c decorations/stack.h
  decorations/underline.c decorations/underline.h

  particles/dynlist.c particles/dynlist.h
  particles/empty.c particles/empty.h
  particles/list.c particles/list.h
  particles/map.c particles/map.h
  particles/progress-bar.c particles/progress-bar.h
  particles/ramp.c particles/ramp.h
  particles/string.c particles/string.h
  )

# TODO: directory global
target_compile_definitions(f00bar PRIVATE _GNU_SOURCE)

# Make global symbols in f00bar visible to dlopen:ed plugins
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

target_compile_options(f00bar PRIVATE
  ${XCB_CFLAGS_OTHER}
  ${FONTCONFIG_CFLAGS_OTHER}
  ${CAIRO_CFLAGS_OTHER}
  ${YAML_CFLAGS_OTHER}
  )

target_include_directories(f00bar PRIVATE
  ${XCB_INCLUDE_DIRS}
  ${FONTCONFIG_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  ${YAML_INCLUDE_DIRS}
  )

target_link_libraries(f00bar
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}

  ${XCB_LIBRARIES}
  ${FONTCONFIG_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${YAML_LIBRARIES}
  )

add_library(module-sdk INTERFACE)
target_compile_definitions(module-sdk INTERFACE _GNU_SOURCE)
target_compile_options(module-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(module-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})
target_link_libraries(module-sdk INTERFACE ${CMAKE_THREAD_LIBS_INIT})

add_subdirectory(modules)
