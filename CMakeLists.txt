cmake_minimum_required(VERSION 3.13)
project(f00bar C)

set(CORE_PLUGINS_AS_SHARED_LIBRARIES 0 CACHE BOOL
  "Compiles modules, particles and decorations as shared libraries, which are loaded on-demand")

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

set_property(DIRECTORY . APPEND PROPERTY COMPILE_DEFINITIONS
  $<$<CONFIG:Debug>:_DEBUG>
  _GNU_SOURCE
  )

if (CORE_PLUGINS_AS_SHARED_LIBRARIES)
  set_property(DIRECTORY . APPEND PROPERTY COMPILE_DEFINITIONS
    CORE_PLUGINS_AS_SHARED_LIBRARIES)
endif ()

set(CMAKE_C_FLAGS "-Wall -Werror ${CMAKE_C_FLAGS}")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(XCB REQUIRED xcb xcb-randr xcb-render xcb-cursor)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
pkg_check_modules(CAIRO REQUIRED cairo cairo-xcb cairo-ft)
pkg_check_modules(YAML REQUIRED yaml-0.1)

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
  plugin.c plugin.h
  tag.c tag.h
  xcb.c xcb.h
  yml.c yml.h
  )

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

set_property(TARGET f00bar PROPERTY INSTALL_RPATH \$ORIGIN/../lib/f00bar)
set_property(TARGET f00bar PROPERTY
  BUILD_RPATH "\$ORIGIN/modules;\$ORIGIN/particles;\$ORIGIN/decorations")

install(TARGETS f00bar DESTINATION bin)

add_subdirectory(modules)
add_subdirectory(particles)
add_subdirectory(decorations)

if (NOT CORE_PLUGINS_AS_SHARED_LIBRARIES)
  target_link_libraries(f00bar background stack underline)
  target_link_libraries(f00bar dynlist empty list map progress-bar ramp string)
  target_link_libraries(f00bar alsa backlight battery clock i3 label mpd network
    removables xkb xwindow)
endif ()
