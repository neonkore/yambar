cmake_minimum_required(VERSION 3.7)
project(f00bar C)

set(CORE_PLUGINS_AS_SHARED_LIBRARIES 0 CACHE BOOL
  "Compiles modules, particles and decorations as shared libraries, which are loaded on-demand")

set(ENABLE_X11 1 CACHE BOOL "Enables support for the XCB (X11) backend")
set_property(DIRECTORY . APPEND PROPERTY
  COMPILE_DEFINITIONS $<$<BOOL:${ENABLE_X11}>:ENABLE_X11>)

set(ENABLE_WAYLAND 1 CACHE BOOL "Enables support for the wayland backend")
set_property(DIRECTORY . APPEND PROPERTY
  COMPILE_DEFINITIONS $<$<BOOL:${ENABLE_WAYLAND}>:ENABLE_WAYLAND>)

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

pkg_check_modules(fontconfig REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(cairo REQUIRED IMPORTED_TARGET cairo cairo-ft)
pkg_check_modules(yaml REQUIRED IMPORTED_TARGET yaml-0.1)

if (ENABLE_X11)
  pkg_check_modules(xcb REQUIRED IMPORTED_TARGET
    xcb xcb-aux xcb-cursor xcb-event xcb-ewmh xcb-randr xcb-render cairo-xcb)
  pkg_check_modules(xcb-errors IMPORTED_TARGET xcb-errors)

  add_library(xcb-stuff STATIC EXCLUDE_FROM_ALL xcb.c xcb.h)
  target_link_libraries(xcb-stuff PkgConfig::xcb)
  if (XCB_ERRORS_FOUND)
    target_compile_definitions(xcb-stuff PRIVATE HAVE_XCB_ERRORS)
    target_link_libraries(xcb-stuff PkgConfig::xcb-errors)
  endif ()

endif ()

if (ENABLE_WAYLAND)
  pkg_check_modules(wayland REQUIRED IMPORTED_TARGET
    wayland-client wayland-cursor wlroots)
endif ()

add_subdirectory(bar)

add_executable(f00bar
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
  yml.c yml.h
  )

target_link_libraries(f00bar
  bar
  PkgConfig::cairo PkgConfig::fontconfig PkgConfig::yaml
  ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS})

# Make global symbols in f00bar visible to dlopen:ed plugins
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

set_property(TARGET f00bar PROPERTY INSTALL_RPATH \$ORIGIN/../lib/f00bar)
set_property(TARGET f00bar PROPERTY
  BUILD_RPATH "\$ORIGIN/modules;\$ORIGIN/particles;\$ORIGIN/decorations")

install(TARGETS f00bar DESTINATION bin)

set(enabled_modules "")
set(enabled_particles "")
set(enabled_decorations "")

add_subdirectory(modules)
add_subdirectory(particles)
add_subdirectory(decorations)

if (NOT CORE_PLUGINS_AS_SHARED_LIBRARIES)
  target_link_libraries(f00bar ${enabled_decorations})
  target_link_libraries(f00bar ${enabled_particles})
  target_link_libraries(f00bar ${enabled_modules})

  foreach (plug ${enabled_decorations} ${enabled_particles} ${enabled_modules})
    string(REPLACE "-" "_" fixed "${plug}")
    target_compile_definitions(f00bar PRIVATE "HAVE_PLUGIN_${fixed}")
  endforeach ()
endif ()
