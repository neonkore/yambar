module_sdk = declare_dependency(dependencies: [pixman, threads, tllist, fcft])

modules = []

udev = dependency('libudev')
json = dependency('json-c')
xcb_xkb = dependency('xcb-xkb', required: get_option('backend-x11'))

# Optional deps
alsa = dependency('alsa', required: get_option('plugin-alsa'))
plugin_alsa_enabled = alsa.found()

mpd = dependency('libmpdclient', required: get_option('plugin-mpd'))
plugin_mpd_enabled = mpd.found()

pipewire = dependency('libpipewire-0.3', required: get_option('plugin-pipewire'))
plugin_pipewire_enabled = pipewire.found()

pulse = dependency('libpulse', required: get_option('plugin-pulse'))
plugin_pulse_enabled = pulse.found()

plugin_dwl_enabled = get_option('plugin-dwl').allowed()

# Module name -> (source-list, dep-list)
mod_data = {
  'backlight': [[], [m, udev]],
  'battery': [[], [udev]],
  'clock': [[], []],
  'cpu': [[], []],
  'disk-io': [[], [dynlist]],
  'mem': [[], []],
  'i3': [['i3-common.c', 'i3-common.h'], [dynlist, json]],
  'label': [[], []],
  'network': [[], []],
  'removables': [[], [dynlist, udev]],
  'script': [[], []],
  'sway-xkb': [['i3-common.c', 'i3-common.h'], [dynlist, json]],
}

if plugin_alsa_enabled
  mod_data += {'alsa': [[], [m, alsa]]}
endif

if plugin_dwl_enabled
  mod_data += {'dwl': [[], [dynlist]]}
endif

if plugin_mpd_enabled
  mod_data += {'mpd': [[], [mpd]]}
endif
if plugin_pipewire_enabled
  mod_data += {'pipewire': [[], [pipewire, dynlist, json]]}
endif

if plugin_pulse_enabled
  mod_data += {'pulse': [[], [pulse]]}
endif

if backend_x11
  mod_data += {
    'xkb': [[], [xcb_stuff, xcb_xkb]],
    'xwindow': [[], [xcb_stuff]],
  }
endif

if backend_wayland
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

  mod_data += {
    'river': [[wl_proto_src + wl_proto_headers + river_proto_src + river_proto_headers], [dynlist, wayland_client]],
  }

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

  mod_data += {
    'foreign-toplevel': [[wl_proto_src + wl_proto_headers + ftop_proto_headers + ftop_proto_src], [dynlist, wayland_client]],
  }
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
