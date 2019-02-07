cmake_minimum_required(VERSION 3.13)

add_library(module-sdk INTERFACE)
target_link_libraries(
  module-sdk INTERFACE PkgConfig::cairo ${CMAKE_THREAD_LIBS_INIT})

if (CORE_PLUGINS_AS_SHARED_LIBRARIES)
  set(lib_type MODULE)
else ()
  set(lib_type STATIC)
endif ()

set(CMAKE_SHARED_${lib_type}_PREFIX module_)

set(enabled "")

pkg_check_modules(alsa REQUIRED IMPORTED_TARGET alsa)
add_library(alsa ${lib_type} alsa.c)
target_link_libraries(alsa module-sdk PkgConfig::alsa)
list(APPEND enabled alsa)

pkg_check_modules(udev REQUIRED IMPORTED_TARGET libudev)
add_library(backlight ${lib_type} backlight.c)
target_link_libraries(backlight module-sdk PkgConfig::udev)
list(APPEND enabled backlight)

add_library(battery ${lib_type} battery.c)
target_link_libraries(battery module-sdk PkgConfig::udev)
list(APPEND enabled battery)

add_library(clock ${lib_type} clock.c)
target_link_libraries(clock module-sdk)
list(APPEND enabled clock)

pkg_check_modules(json REQUIRED IMPORTED_TARGET json-c)
find_file(I3_IPC_H i3/ipc.h)
if (NOT I3_IPC_H)
  message(FATAL_ERROR "cannot find header file: i3/ipc.h")
endif ()
add_library(i3 ${lib_type} i3.c)
target_link_libraries(i3 module-sdk dynlist PkgConfig::json)
if (ENABLE_X11)
  target_link_libraries(i3 xcb-stuff)
endif ()
list(APPEND enabled i3)

add_library(label ${lib_type} label.c)
target_link_libraries(label module-sdk)
list(APPEND enabled label)

pkg_check_modules(mpd REQUIRED IMPORTED_TARGET libmpdclient)
add_library(mpd ${lib_type} mpd.c)
target_link_libraries(mpd module-sdk PkgConfig::mpd)
list(APPEND enabled mpd)

add_library(network ${lib_type} network.c)
target_link_libraries(network module-sdk)
list(APPEND enabled network)

add_library(removables ${lib_type} removables.c)
target_link_libraries(removables module-sdk dynlist PkgConfig::udev)
list(APPEND enabled removables)

if (ENABLE_X11)
  pkg_check_modules(xkb REQUIRED IMPORTED_TARGET xcb-xkb)
  add_library(xkb ${lib_type} xkb.c)
  target_link_libraries(xkb module-sdk PkgConfig::xcb PkgConfig::xkb)
  list(APPEND enabled xkb)
endif ()

if (ENABLE_X11)
  add_library(xwindow ${lib_type} xwindow.c)
  target_link_libraries(xwindow module-sdk xcb-stuff)
  list(APPEND enabled xwindow)
endif ()

if (CORE_PLUGINS_AS_SHARED_LIBRARIES)
  install(TARGETS ${enabled} DESTINATION lib/f00bar)
endif ()

set(enabled_modules ${enabled} PARENT_SCOPE)
