cmake_minimum_required(VERSION 3.13)

pkg_check_modules(ALSA REQUIRED alsa)
add_library(alsa MODULE alsa/alsa.c alsa/alsa.h)
target_compile_options(alsa PRIVATE ${ALSA_CFLAGS_OTHER})
target_include_directories(alsa PRIVATE ${ALSA_INCLUDE_DIRS})
target_link_libraries(alsa module-sdk ${ALSA_LIBRARIES})

pkg_check_modules(UDEV REQUIRED libudev)
add_library(backlight MODULE backlight/backlight.c backlight/backlight.h)
target_compile_options(backlight PRIVATE ${UDEV_CFLAGS_OTHER})
target_include_directories(backlight PRIVATE ${UDEV_INCLUDE_DIRS})
target_link_libraries(backlight module-sdk ${UDEV_LIBRARIES})

add_library(battery MODULE battery/battery.c battery/battery.h)
target_compile_options(battery PRIVATE ${UDEV_CFLAGS_OTHER})
target_include_directories(battery PRIVATE ${UDEV_INCLUDE_DIRS})
target_link_libraries(battery module-sdk ${UDEV_LIBRARIES})

add_library(clock MODULE clock/clock.c clock/clock.h)
target_link_libraries(clock module-sdk)

pkg_check_modules(JSON REQUIRED json-c)
add_library(i3 MODULE i3/i3.c i3/i3.h)
target_compile_options(i3 PRIVATE ${JSON_CFLAGS_OTHER})
target_include_directories(i3 PRIVATE ${JSON_INCLUDE_DIRS})
target_link_libraries(i3 module-sdk ${JSON_LIBRARIES})


add_library(label MODULE label/label.c label/label.h)
target_link_libraries(label module-sdk)

pkg_check_modules(MPD REQUIRED libmpdclient)
add_library(mpd MODULE mpd/mpd.c mpd/mpd.h)
target_compile_options(mpd PRIVATE ${MPD_CFLAGS_OTHER})
target_include_directories(mpd PRIVATE ${MPD_INCLUDE_DIRS})
target_link_libraries(mpd module-sdk ${MPD_LIBRARIES})

add_library(network MODULE network/network.c network/network.h)
target_link_libraries(network module-sdk)

add_library(removables MODULE removables/removables.c removables/removables.h)
target_compile_options(removables PRIVATE ${UDEV_CFLAGS_OTHER})
target_include_directories(removables PRIVATE ${UDEV_INCLUDE_DIRS})
target_link_libraries(removables module-sdk ${UDEV_LIBRARIES})

pkg_check_modules(XCB_XKB REQUIRED xcb-xkb)
add_library(xkb MODULE xkb/xkb.c xkb/xkb.h)
target_compile_options(xkb PRIVATE ${XCB_XKB_CFLAGS_OTHER})
target_include_directories(xkb PRIVATE ${XCB_XKB_INCLUDE_DIRS})
target_link_libraries(xkb module-sdk ${XCB_XKB_LIBRARIES})

add_library(xwindow MODULE xwindow/xwindow.c xwindow/xwindow.h)
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
