module_sdk = declare_dependency(dependencies: [cairo, cairo_ft, threads])

modules = []

alsa = dependency('alsa')
udev = dependency('libudev')
json = dependency('json-c')
mpd = dependency('libmpdclient')
xcb_xkb = dependency('xcb-xkb', required: get_option('backend-x11'))

# Module name -> (source-list, dep-list)
deps = {
  'alsa': [[], [alsa]],
  'backlight': [[], [udev]],
  'battery': [[], [udev]],
  'clock': [[], []],
  'i3': [['i3-common.c', 'i3-common.h'], [dynlist, json]],
  'label': [[], []],
  'mpd': [[], [mpd]],
  'network': [[], []],
  'removables': [[], [dynlist, udev]],
}

if backend_x11
  deps += {
    'xkb': [[], [xcb_stuff, xcb_xkb]],
    'xwindow': [[], [xcb_stuff]],
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
                  install_dir: join_paths(get_option('libdir'), 'f00bar'))
  else
    modules += [declare_dependency(
      sources: ['@0@.c'.format(mod)] + sources,
      dependencies: [module_sdk] + dep,
      compile_args: '-DHAVE_PLUGIN_@0@'.format(mod))]
  endif
endforeach
