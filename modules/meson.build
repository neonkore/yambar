module_sdk = declare_dependency(dependencies: [cairo, cairo_ft, threads])

modules = []
alsa_mod = static_library(
  'module_alsa', 'alsa.c', dependencies: [module_sdk, dependency('alsa')])
modules += [declare_dependency(
  link_with: alsa_mod, compile_args: '-DHAVE_PLUGIN_alsa')]

udev = dependency('libudev')
backlight_mod = static_library(
  'module_backlight', 'backlight.c', dependencies: [module_sdk, udev])
modules += [declare_dependency(
  link_with: backlight_mod, compile_args: '-DHAVE_PLUGIN_backlight')]

battery_mod = static_library(
  'module_battery', 'battery.c', dependencies: [module_sdk, udev])
modules += [declare_dependency(
  link_with: battery_mod, compile_args: '-DHAVE_PLUGIN_battery')]

clock_mod = static_library(
  'module_clock', 'clock.c', dependencies: [module_sdk])
modules += [declare_dependency(
  link_with: clock_mod, compile_args: '-DHAVE_PLUGIN_clock')]

json = dependency('json-c')
i3_mod = static_library(
  'module_i3', 'i3.c', 'i3-common.c', 'i3-common.h',
  dependencies: [module_sdk, json, dynlist])
modules += [declare_dependency(
  link_with: i3_mod, compile_args: '-DHAVE_PLUGIN_i3')]

label_mod = static_library(
  'module_label', 'label.c', dependencies: [module_sdk])
modules += [declare_dependency(
  link_with: label_mod, compile_args: '-DHAVE_PLUGIN_label')]

mpd = dependency('libmpdclient')
mpd_mod = static_library(
  'module_mpd', 'mpd.c', dependencies: [module_sdk, mpd])
modules += [declare_dependency(
  link_with: mpd_mod, compile_args: '-DHAVE_PLUGIN_mpd')]

network_mod = static_library(
  'module_network', 'network.c', dependencies: [module_sdk])
modules += [declare_dependency(
  link_with: network_mod, compile_args: '-DHAVE_PLUGIN_network')]

removables_mod = static_library(
  'module_removables', 'removables.c', dependencies: [module_sdk, udev, dynlist])
modules += [declare_dependency(
  link_with: removables_mod, compile_args: '-DHAVE_PLUGIN_removables')]

if enable_x11
  xcb_xkb = dependency('xcb-xkb')
  xkb_mod = static_library(
    'module_xkb', 'xkb.c', dependencies: [module_sdk, xcb_stuff, xcb_xkb])
  modules += [declare_dependency(
    link_with: xkb_mod, compile_args: '-DHAVE_PLUGIN_xkb')]

  xwindow_mod = static_library(
    'module_xwindow', 'xwindow.c', dependencies: [module_sdk, xcb_stuff])
  modules += [declare_dependency(
    link_with: xwindow_mod, compile_args: '-DHAVE_MODULE_xwindow')]
endif
