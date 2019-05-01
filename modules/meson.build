module_sdk = declare_dependency(dependencies: [cairo, cairo_ft, threads])

modules = []

alsa = dependency('alsa')
if get_option('core-plugins-as-shared-libraries')
  shared_module(
    'alsa', 'alsa.c', dependencies: [module_sdk, alsa], name_prefix: 'module_')
else
  alsa_mod = static_library(
    'module_alsa', 'alsa.c', dependencies: [module_sdk, alsa])
  modules += [declare_dependency(
    link_with: alsa_mod, compile_args: '-DHAVE_PLUGIN_alsa')]
endif

udev = dependency('libudev')
if get_option('core-plugins-as-shared-libraries')
  shared_module(
    'backlight', 'backlight.c', dependencies: [module_sdk, udev], name_prefix: 'module_')
else
  backlight_mod = static_library(
    'module_backlight', 'backlight.c', dependencies: [module_sdk, udev])
  modules += [declare_dependency(
    link_with: backlight_mod, compile_args: '-DHAVE_PLUGIN_backlight')]
endif

if get_option('core-plugins-as-shared-libraries')
  shared_module(
    'battery', 'battery.c', dependencies: [module_sdk, udev], name_prefix: 'module_')
else
  battery_mod = static_library(
    'module_battery', 'battery.c', dependencies: [module_sdk, udev])
  modules += [declare_dependency(
    link_with: battery_mod, compile_args: '-DHAVE_PLUGIN_battery')]
endif

if get_option('core-plugins-as-shared-libraries')
  shared_module(
    'clock', 'clock.c', dependencies: [module_sdk], name_prefix: 'module_')
else
  clock_mod = static_library(
    'module_clock', 'clock.c', dependencies: [module_sdk])
  modules += [declare_dependency(
    link_with: clock_mod, compile_args: '-DHAVE_PLUGIN_clock')]
endif

json = dependency('json-c')
if get_option('core-plugins-as-shared-libraries')
  shared_module('module_i3', 'i3.c', 'i3-common.c', 'i3-common.h',
                dependencies: [module_sdk, json, dynlist],
                name_prefix: '')
else
  i3_mod = static_library(
    'module_i3', 'i3.c', 'i3-common.c', 'i3-common.h',
    dependencies: [module_sdk, json, dynlist])
  modules += [declare_dependency(
    link_with: i3_mod, compile_args: '-DHAVE_PLUGIN_i3')]
endif

if get_option('core-plugins-as-shared-libraries')
  shared_module(
    'label', 'label.c', dependencies: [module_sdk], name_prefix: 'module_')
else
  label_mod = static_library(
    'module_label', 'label.c', dependencies: [module_sdk])
  modules += [declare_dependency(
    link_with: label_mod, compile_args: '-DHAVE_PLUGIN_label')]
endif

mpd = dependency('libmpdclient')
if get_option('core-plugins-as-shared-libraries')
  shared_module(
    'mpd', 'mpd.c', dependencies: [module_sdk, mpd], name_prefix: 'module_')
else
  mpd_mod = static_library(
    'module_mpd', 'mpd.c', dependencies: [module_sdk, mpd])
  modules += [declare_dependency(
    link_with: mpd_mod, compile_args: '-DHAVE_PLUGIN_mpd')]
endif

if get_option('core-plugins-as-shared-libraries')
  shared_module(
    'network', 'network.c', dependencies: [module_sdk], name_prefix: 'module_')
else
  network_mod = static_library(
    'module_network', 'network.c', dependencies: [module_sdk])
  modules += [declare_dependency(
    link_with: network_mod, compile_args: '-DHAVE_PLUGIN_network')]
endif

if get_option('core-plugins-as-shared-libraries')
  shared_module(
    'removables', 'removables.c', dependencies: [module_sdk, udev, dynlist],
    name_prefix: 'module_')
else
  removables_mod = static_library(
    'module_removables', 'removables.c', dependencies: [module_sdk, udev, dynlist])
  modules += [declare_dependency(
    link_with: removables_mod, compile_args: '-DHAVE_PLUGIN_removables')]
endif

if enable_x11
  xcb_xkb = dependency('xcb-xkb')
  if get_option('core-plugins-as-shared-libraries')
    shared_module(
      'xkb', 'xkb.c', dependencies: [module_sdk, xcb_stuff, xcb_xkb], name_prefix: 'module_')
  else
    xkb_mod = static_library(
      'module_xkb', 'xkb.c', dependencies: [module_sdk, xcb_stuff, xcb_xkb])
    modules += [declare_dependency(
      link_with: xkb_mod, compile_args: '-DHAVE_PLUGIN_xkb')]
  endif

  if get_option('core-plugins-as-shared-libraries')
    shared_module(
      'xwindow', 'xwindow.c', dependencies: [module_sdk, xcb_stuff],
      name_prefix: 'module_')
  else
    xwindow_mod = static_library(
      'module_xwindow', 'xwindow.c', dependencies: [module_sdk, xcb_stuff])
    modules += [declare_dependency(
      link_with: xwindow_mod, compile_args: '-DHAVE_MODULE_xwindow')]
  endif
endif
