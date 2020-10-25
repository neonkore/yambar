module_sdk = declare_dependency(dependencies: [pixman, threads, tllist, fcft])

modules = []

alsa = dependency('alsa')
udev = dependency('libudev')
json = dependency('json-c')
mpd = dependency('libmpdclient')
xcb_xkb = dependency('xcb-xkb', required: get_option('backend-x11'))

# Module name -> (source-list, dep-list)
deps = {
  'alsa': [[], [m, alsa]],
  'backlight': [[], [m, udev]],
  'battery': [[], [udev]],
  'clock': [[], []],
  'i3': [['i3-common.c', 'i3-common.h'], [dynlist, json]],
  'label': [[], []],
  'mpd': [[], [mpd]],
  'network': [[], []],
  'removables': [[], [dynlist, udev]],
  'script': [[], []],
  'sway_xkb': [['i3-common.c', 'i3-common.h'], [dynlist, json]],
}

if backend_x11
  deps += {
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

  deps += {
    'river': [[wl_proto_src + wl_proto_headers + river_proto_src + river_proto_headers], []],
  }
endif

foreach mod, data : deps
  sources = data[0]
  dep = data[1]

  if plugs_as_libs
    shared_module(mod, '@0@.c'.format(mod), sources,
                  dependencies: [module_sdk] + dep,
                  name_prefix: 'module_',
                  install: true,
                  install_dir: join_paths(get_option('libdir'), 'yambar'))
  else
    modules += [declare_dependency(
      sources: ['@0@.c'.format(mod)] + sources,
      dependencies: [module_sdk] + dep,
      compile_args: '-DHAVE_PLUGIN_@0@'.format(mod))]
  endif
endforeach
