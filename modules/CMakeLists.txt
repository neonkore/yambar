cmake_minimum_required(VERSION 3.13)

add_library(module-sdk INTERFACE)
target_compile_options(module-sdk INTERFACE ${CAIRO_CFLAGS_OTHER})
target_include_directories(module-sdk INTERFACE ${CAIRO_INCLUDE_DIRS})
target_link_libraries(module-sdk INTERFACE ${CMAKE_THREAD_LIBS_INIT})

set(CMAKE_SHARED_MODULE_PREFIX module_)

pkg_check_modules(ALSA REQUIRED alsa)
add_library(alsa MODULE alsa.c)
target_compile_options(alsa PRIVATE ${ALSA_CFLAGS_OTHER})
target_include_directories(alsa PRIVATE ${ALSA_INCLUDE_DIRS})
target_link_libraries(alsa module-sdk ${ALSA_LIBRARIES})

pkg_check_modules(UDEV REQUIRED libudev)
add_library(backlight MODULE backlight.c)
target_compile_options(backlight PRIVATE ${UDEV_CFLAGS_OTHER})
target_include_directories(backlight PRIVATE ${UDEV_INCLUDE_DIRS})
target_link_libraries(backlight module-sdk ${UDEV_LIBRARIES})

add_library(battery MODULE battery.c)
target_compile_options(battery PRIVATE ${UDEV_CFLAGS_OTHER})
target_include_directories(battery PRIVATE ${UDEV_INCLUDE_DIRS})
target_link_libraries(battery module-sdk ${UDEV_LIBRARIES})

add_library(clock MODULE clock.c)
target_link_libraries(clock module-sdk)

pkg_check_modules(JSON REQUIRED json-c)
add_library(i3 MODULE i3.c)
target_compile_options(i3 PRIVATE ${JSON_CFLAGS_OTHER})
target_include_directories(i3 PRIVATE ${JSON_INCLUDE_DIRS})
target_link_libraries(i3 module-sdk ${JSON_LIBRARIES})

add_library(label MODULE label.c)
target_link_libraries(label module-sdk)

pkg_check_modules(MPD REQUIRED libmpdclient)
add_library(mpd MODULE mpd.c)
target_compile_options(mpd PRIVATE ${MPD_CFLAGS_OTHER})
target_include_directories(mpd PRIVATE ${MPD_INCLUDE_DIRS})
target_link_libraries(mpd module-sdk ${MPD_LIBRARIES})

add_library(network MODULE network.c)
target_link_libraries(network module-sdk)

add_library(removables MODULE removables.c)
target_compile_options(removables PRIVATE ${UDEV_CFLAGS_OTHER})
target_include_directories(removables PRIVATE ${UDEV_INCLUDE_DIRS})
target_link_libraries(removables module-sdk ${UDEV_LIBRARIES})

pkg_check_modules(XCB_XKB REQUIRED xcb-xkb)
add_library(xkb MODULE xkb.c)
target_compile_options(xkb PRIVATE ${XCB_XKB_CFLAGS_OTHER})
target_include_directories(xkb PRIVATE ${XCB_XKB_INCLUDE_DIRS})
target_link_libraries(xkb module-sdk ${XCB_XKB_LIBRARIES})

add_library(xwindow MODULE xwindow.c)
target_link_libraries(xwindow module-sdk)

install(
  TARGETS
  alsa
  backlight
  battery
  clock
  i3
  label
  mpd
  network
  removables
  xkb
  xwindow

  DESTINATION lib/f00bar)
