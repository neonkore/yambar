cmake_minimum_required(VERSION 3.7)
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

pkg_check_modules(XCB REQUIRED xcb xcb-aux xcb-cursor xcb-event xcb-ewmh
  xcb-randr xcb-render)
pkg_check_modules(XCB_ERRORS xcb-errors)
pkg_check_modules(WAYLAND REQUIRED wayland-client wlroots)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
pkg_check_modules(CAIRO REQUIRED cairo cairo-xcb cairo-ft)
pkg_check_modules(YAML REQUIRED yaml-0.1)

add_library(xcb-stuff STATIC EXCLUDE_FROM_ALL xcb.c xcb.h)

add_library(bar-xcb STATIC EXCLUDE_FROM_ALL bar/xcb.c bar/xcb.h)

target_compile_options(bar-xcb PRIVATE
  ${XCB_CFLAGS_OTHER}
  ${XCB_ERRORS_CFLAGS_OTHER}
  ${CAIRO_CFLAGS_OTHER}
  )

target_include_directories(bar-xcb PRIVATE
  ${XCB_INCLUDE_DIRS}
  ${XCB_ERRORS_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  )

target_link_libraries(bar-xcb xcb-stuff ${XCB_LIBRARIES} ${XCB_ERRORS_LIBRARIES})

add_custom_command(
  OUTPUT wlr-layer-shell-unstable-v1.c
  COMMAND wayland-scanner private-code < /home/daniel/AUR/wlroots-git/src/wlroots-git/protocol/wlr-layer-shell-unstable-v1.xml > wlr-layer-shell-unstable-v1.c
  VERBATIM
  MAIN_DEPENDENCY /home/daniel/AUR/wlroots-git/src/wlroots-git/protocol/wlr-layer-shell-unstable-v1.xml
  )
add_custom_command(
  OUTPUT wlr-layer-shell-unstable-v1-client.h
  COMMAND wayland-scanner client-header < /home/daniel/AUR/wlroots-git/src/wlroots-git/protocol/wlr-layer-shell-unstable-v1.xml > wlr-layer-shell-unstable-v1-client.h
  VERBATIM
  MAIN_DEPENDENCY /home/daniel/AUR/wlroots-git/src/wlroots-git/protocol/wlr-layer-shell-unstable-v1.xml
  )
add_custom_command(
  OUTPUT xdg-shell.c
  COMMAND wayland-scanner private-code < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > xdg-shell.c
  VERBATIM
  MAIN_DEPENDENCY /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
  )

add_library(wayland-protocols STATIC EXCLUDE_FROM_ALL
  wlr-layer-shell-unstable-v1-client.h
  wlr-layer-shell-unstable-v1.c
  xdg-shell.c
  )
target_include_directories(wayland-protocols PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

add_library(bar-wayland STATIC EXCLUDE_FROM_ALL bar/wayland.c bar/wayland.h)
target_compile_definitions(bar-wayland PRIVATE _GNU_SOURCE)
target_compile_options(bar-wayland PRIVATE
  ${WAYLAND_CFLAGS_OTHER}
  ${CAIRO_CFLAGS_OTHER}
  )

target_include_directories(bar-wayland PRIVATE
  ${CURRENT_BINARY_DIR}
  ${WAYLAND_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  )

target_link_libraries(bar-wayland wayland-protocols ${WAYLAND_LIBRARIES})


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

  bar.h
  bar/bar.c bar/private.h bar/backend.h
  )

target_link_libraries(f00bar xcb-stuff bar-xcb bar-wayland)

# Make global symbols in f00bar visible to dlopen:ed plugins
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

if (XCB_ERRORS_FOUND)
  target_compile_definitions(f00bar PRIVATE HAVE_XCB_ERRORS)
endif ()

target_compile_options(f00bar PRIVATE
  ${FONTCONFIG_CFLAGS_OTHER}
  ${CAIRO_CFLAGS_OTHER}
  ${YAML_CFLAGS_OTHER}
  )

target_include_directories(f00bar PRIVATE
  ${FONTCONFIG_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  ${YAML_INCLUDE_DIRS}
  )

target_link_libraries(f00bar
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}

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
