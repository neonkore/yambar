project('yambar', 'c',
        version: '1.4.0',
        license: 'MIT',
        meson_version: '>=0.53.0',
        default_options: ['c_std=c18',
                          'warning_level=1',
                          'werror=true',
                          'b_ndebug=if-release'])

is_debug_build = get_option('buildtype').startswith('debug')
plugs_as_libs = get_option('core-plugins-as-shared-libraries')

cc = meson.get_compiler('c')

# Compute the relative path used by compiler invocations.
source_root = meson.current_source_dir().split('/')
build_root = meson.build_root().split('/')
relative_dir_parts = []
i = 0
in_prefix = true
foreach p : build_root
  if i >= source_root.length() or not in_prefix or p != source_root[i]
    in_prefix = false
    relative_dir_parts += '..'
  endif
  i += 1
endforeach
i = 0
in_prefix = true
foreach p : source_root
  if i >= build_root.length() or not in_prefix or build_root[i] != p
    in_prefix = false
    relative_dir_parts += p
  endif
  i += 1
endforeach
relative_dir = join_paths(relative_dir_parts) + '/'

if cc.has_argument('-fmacro-prefix-map=/foo=')
  add_project_arguments('-fmacro-prefix-map=@0@='.format(relative_dir), language: 'c')
endif

# Common dependencies
dl = cc.find_library('dl')
threads = dependency('threads')
pixman = dependency('pixman-1')
yaml = dependency('yaml-0.1')

# X11/XCB dependencies
xcb_aux = dependency('xcb-aux', required: get_option('backend-x11'))
xcb_cursor = dependency('xcb-cursor', required: get_option('backend-x11'))
xcb_event = dependency('xcb-event', required: get_option('backend-x11'))
xcb_ewmh = dependency('xcb-ewmh', required: get_option('backend-x11'))
xcb_randr = dependency('xcb-randr', required: get_option('backend-x11'))
xcb_render = dependency('xcb-render', required: get_option('backend-x11'))
xcb_errors = dependency('xcb-errors', required: false)
backend_x11 = xcb_aux.found() and xcb_cursor.found() and xcb_event.found() and \
              xcb_ewmh.found() and xcb_randr.found() and xcb_render.found()

# Wayland dependencies
wayland_client = dependency('wayland-client', required: get_option('backend-wayland'))
wayland_cursor = dependency('wayland-cursor', required: get_option('backend-wayland'))
backend_wayland = wayland_client.found() and wayland_cursor.found()

# "My" dependencies, fallback to subproject
tllist = dependency('tllist', version: '>=1.0.1', fallback: 'tllist')
fcft = dependency('fcft', version: ['>=2.0.0', '<3.0.0'], fallback: 'fcft')

add_project_arguments(
  ['-D_GNU_SOURCE'] +
  (is_debug_build ? ['-D_DEBUG'] : []) +
  (backend_x11 ? ['-DENABLE_X11'] : []) +
  (backend_wayland ? ['-DENABLE_WAYLAND'] : []) +
  (plugs_as_libs ? ['-DCORE_PLUGINS_AS_SHARED_LIBRARIES'] : []),
  language: 'c',
)

if backend_x11
  xcb_stuff_lib = static_library(
    'xcb-stuff', 'xcb.c', 'xcb.h',
    dependencies: [xcb_aux, xcb_cursor, xcb_event, xcb_ewmh, xcb_randr,
                   xcb_render, xcb_errors],
    c_args: xcb_errors.found() ? '-DHAVE_XCB_ERRORS' : [],
    pic: plugs_as_libs)

  xcb_stuff = declare_dependency(link_with: xcb_stuff_lib)
  install_headers('xcb.h', subdir: 'yambar')
endif

subdir('completions')
subdir('doc')
subdir('bar')
subdir('decorations')
subdir('particles')
subdir('modules')

generate_version_sh = files('generate-version.sh')
version = custom_target(
  'generate_version',
  build_always_stale: true,
  output: 'version.h',
  command: [generate_version_sh, meson.project_version(), '@SOURCE_DIR@', '@OUTPUT@'])

yambar = executable(
  'yambar',
  'color.h',
  'config-verify.c', 'config-verify.h',
  'config.c', 'config.h',
  'decoration.h',
  'log.c', 'log.h',
  'main.c',
  'module.c', 'module.h',
  'particle.c', 'particle.h',
  'plugin.c', 'plugin.h',
  'tag.c', 'tag.h',
  'yml.c', 'yml.h',
  version,
  dependencies: [bar, pixman, yaml, threads, dl, tllist, fcft] +
                decorations + particles + modules,
  build_rpath: '$ORIGIN/modules:$ORIGIN/decorations:$ORIGIN/particles',
  export_dynamic: true,
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir') + '/yambar')

install_data(
  'LICENSE', 'README.md',
  install_dir: join_paths(get_option('datadir'), 'doc', 'yambar'))
install_data('yambar.desktop', install_dir: join_paths(get_option('datadir'), 'applications'))

subdir('test')

install_headers(
  'color.h',
  'config.h',
  'config-verify.h',
  'decoration.h',
  'log.h',
  'module.h',
  'particle.h',
  'stride.h',
  'tag.h',
  'yml.h',
  subdir: 'yambar')

summary(
  {
    'Build type': get_option('buildtype'),
    'XCB backend': backend_x11,
    'Wayland backend': backend_wayland,
    'Core modules as plugins': plugs_as_libs,
  },
  bool_yn: true
)
