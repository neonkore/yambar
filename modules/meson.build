module_sdk = declare_dependency(dependencies: [pixman, threads, tllist, fcft])

modules = []

# Optional deps
alsa = dependency('alsa', required: get_option('plugin-alsa'))
plugin_alsa_enabled = alsa.found()

udev_backlight = dependency('libudev', required: get_option('plugin-backlight'))
plugin_backlight_enabled = udev_backlight.found()

udev_battery = dependency('libudev', required: get_option('plugin-battery'))
plugin_battery_enabled = udev_battery.found()

plugin_clock_enabled = get_option('plugin-clock').allowed()
plugin_cpu_enabled = get_option('plugin-cpu').allowed()
plugin_disk_io_enabled = get_option('plugin-disk-io').allowed()
plugin_dwl_enabled = get_option('plugin-dwl').allowed()
plugin_foreign_toplevel_enabled = backend_wayland and get_option('plugin-foreign-toplevel').allowed()
plugin_mem_enabled = get_option('plugin-mem').allowed()

mpd = dependency('libmpdclient', required: get_option('plugin-mpd'))
plugin_mpd_enabled = mpd.found()

json_i3 = dependency('json-c', required: get_option('plugin-i3'))
plugin_i3_enabled = json_i3.found()

plugin_label_enabled = get_option('plugin-label').allowed()
plugin_network_enabled = get_option('plugin-network').allowed()

pipewire = dependency('libpipewire-0.3', required: get_option('plugin-pipewire'))
json_pipewire = dependency('json-c', required: get_option('plugin-pipewire'))
plugin_pipewire_enabled = pipewire.found() and json_pipewire.found()

pulse = dependency('libpulse', required: get_option('plugin-pulse'))
plugin_pulse_enabled = pulse.found()

udev_removables = dependency('udev', required: get_option('plugin-removables'))
plugin_removables_enabled = udev_removables.found()

plugin_river_enabled = backend_wayland and get_option('plugin-river').allowed()

plugin_script_enabled = get_option('plugin-script').allowed()

json_sway_xkb = dependency('json-c', required: get_option('plugin-sway-xkb'))
plugin_sway_xkb_enabled = json_sway_xkb.found()

xcb_xkb = dependency('xcb-xkb', required: get_option('plugin-xkb'))
plugin_xkb_enabled = backend_x11 and xcb_xkb.found()

plugin_xwindow_enabled = backend_x11 and get_option('plugin-xwindow').allowed()

# Module name -> (source-list, dep-list)
mod_data = {}

if plugin_alsa_enabled
  mod_data += {'alsa': [[], [m, alsa]]}
endif

if plugin_backlight_enabled
  mod_data += {'backlight': [[], [m, udev_backlight]]}
endif

if plugin_battery_enabled
  mod_data += {'battery': [[], [udev_battery]]}
endif

if plugin_clock_enabled
  mod_data += {'clock': [[], []]}
endif

if plugin_cpu_enabled
  mod_data += {'cpu': [[], []]}
endif

if plugin_disk_io_enabled
  mod_data += {'disk-io': [[], [dynlist]]}
endif

if plugin_dwl_enabled
  mod_data += {'dwl': [[], [dynlist]]}
endif

if plugin_mem_enabled
  mod_data += {'mem': [[], []]}
endif

if plugin_mpd_enabled
  mod_data += {'mpd': [[], [mpd]]}
endif

if plugin_i3_enabled
  mod_data += {'i3': [['i3-common.c', 'i3-common.h'], [dynlist, json_i3]]}
endif

if plugin_label_enabled
  mod_data += {'label': [[], []]}
endif

if plugin_network_enabled
  mod_data += {'network': [[], []]}
endif

if plugin_pipewire_enabled
  mod_data += {'pipewire': [[], [pipewire, dynlist, json_pipewire]]}
endif

if plugin_pulse_enabled
  mod_data += {'pulse': [[], [pulse]]}
endif

if plugin_removables_enabled
  mod_data += {'removables': [[], [dynlist, udev_removables]]}
endif

if plugin_script_enabled
  mod_data += {'script': [[], []]}
endif

if plugin_sway_xkb_enabled
  mod_data += {'sway-xkb': [['i3-common.c', 'i3-common.h'], [dynlist, json_sway_xkb]]}
endif

if plugin_xkb_enabled
  mod_data += {'xkb': [[], [xcb_stuff, xcb_xkb]]}
endif

if plugin_xwindow_enabled
  mod_data += {'xwindow': [[], [xcb_stuff]]}
endif

if plugin_river_enabled
  river_proto_headers = []
  river_proto_src = []

  foreach prot : ['../external/river-status-unstable-v1.xml']

    river_proto_headers += custom_target(
      prot.underscorify() + '-client-header',
      output: '@BASENAME@.h',
      input: prot,
      command: [wscanner_prog, 'client-header', '@INPUT@', '@OUTPUT@'])

    river_proto_src += custom_target(
      prot.underscorify() + '-private-code',
      output: '@BASENAME@.c',
      input: prot,
      command: [wscanner_prog, 'private-code', '@INPUT@', '@OUTPUT@'])
  endforeach

  mod_data += {'river': [[wl_proto_src + wl_proto_headers + river_proto_src + river_proto_headers], [dynlist, wayland_client]]}
endif

if plugin_foreign_toplevel_enabled
  ftop_proto_headers = []
  ftop_proto_src = []

  foreach prot : ['../external/wlr-foreign-toplevel-management-unstable-v1.xml']

    ftop_proto_headers += custom_target(
      prot.underscorify() + '-client-header',
      output: '@BASENAME@.h',
      input: prot,
      command: [wscanner_prog, 'client-header', '@INPUT@', '@OUTPUT@'])

    ftop_proto_src += custom_target(
      prot.underscorify() + '-private-code',
      output: '@BASENAME@.c',
      input: prot,
      command: [wscanner_prog, 'private-code', '@INPUT@', '@OUTPUT@'])
  endforeach

  mod_data += {'foreign-toplevel': [[wl_proto_src + wl_proto_headers + ftop_proto_headers + ftop_proto_src], [dynlist, wayland_client]]}
endif

foreach mod, data : mod_data
  sources = data[0]
  deps = data[1]

  if plugs_as_libs
    shared_module(mod, '@0@.c'.format(mod), sources,
                  dependencies: [module_sdk] + deps,
                  name_prefix: 'module_',
                  install: true,
                  install_dir: join_paths(get_option('libdir'), 'yambar'))
  else
    modules += [declare_dependency(
      sources: ['@0@.c'.format(mod)] + sources,
      dependencies: [module_sdk] + deps,
      compile_args: '-DHAVE_PLUGIN_@0@'.format(mod.underscorify()))]
  endif
endforeach
