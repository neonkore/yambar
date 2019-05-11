project('f00bar', 'c',
        version: '1.0.3',
        license: 'MIT',
        meson_version: '>=0.48.0',
        default_options: ['c_std=c11',
                          'warning_level=1',
                          'werror=true',
                          'b_ndebug=if-release'])

plugs_as_libs = get_option('core-plugins-as-shared-libraries')

version = '"@0@"'.format(meson.project_version())

sh = find_program('sh', native: true)
git = find_program('git', required: false, native: true)

if git.found()
  commit_hash = run_command(
    [sh.path(), '-c',
     '@0@ --git-dir="$MESON_SOURCE_ROOT/.git" describe --always --tags'.format(
       git.path())])

  branch = run_command(
    [sh.path(), '-c',
     '@0@ "--git-dir=$MESON_SOURCE_ROOT/.git" rev-parse --abbrev-ref HEAD'.format(
       git.path())])

  if commit_hash.returncode() == 0 and branch.returncode() == 0
    version = '"@0@ (" __DATE__ ", branch \'@1@\')"'.format(
      commit_hash.stdout().strip(), branch.stdout().strip())
  endif
endif

# Common dependencies
cc = meson.get_compiler('c')
dl = cc.find_library('dl')
threads = dependency('threads')
fontconfig = dependency('fontconfig')
cairo = dependency('cairo')
cairo_ft = dependency('cairo-ft')
yaml = dependency('yaml-0.1')

# X11/XCB dependencies
xcb_aux = dependency('xcb-aux', required: get_option('backend-x11'))
xcb_cursor = dependency('xcb-cursor', required: get_option('backend-x11'))
xcb_event = dependency('xcb-event', required: get_option('backend-x11'))
xcb_ewmh = dependency('xcb-ewmh', required: get_option('backend-x11'))
xcb_randr = dependency('xcb-randr', required: get_option('backend-x11'))
xcb_render = dependency('xcb-render', required: get_option('backend-x11'))
cairo_xcb = dependency('cairo-xcb', required: get_option('backend-x11'))
xcb_errors = dependency('xcb-errors', required: false)
backend_x11 = xcb_aux.found() and xcb_cursor.found() and xcb_event.found() and \
              xcb_ewmh.found() and xcb_randr.found() and xcb_render.found() and \
              cairo_xcb.found()

# Wayland dependencies
wayland_client = dependency('wayland-client', required: get_option('backend-wayland'))
wayland_cursor = dependency('wayland-cursor', required: get_option('backend-wayland'))
wlroots = dependency('wlroots', required: get_option('backend-wayland'))
backend_wayland = wayland_client.found() and wayland_cursor.found() and wlroots.found()

add_project_arguments(
  ['-D_GNU_SOURCE',
   '-DF00BAR_VERSION=@0@'.format(version)] +
  (backend_x11 ? ['-DENABLE_X11'] : []) +
  (backend_wayland ? ['-DENABLE_WAYLAND'] : []) +
  (plugs_as_libs ? ['-DCORE_PLUGINS_AS_SHARED_LIBRARIES'] : []),
  language: 'c',
)

if backend_x11
  xcb_stuff_lib = static_library(
    'xcb-stuff', 'xcb.c', 'xcb.h',
    dependencies: [xcb_aux, xcb_cursor, xcb_event, xcb_ewmh, xcb_randr,
                   xcb_render, cairo_xcb, xcb_errors],
    c_args: xcb_errors.found() ? '-DHAVE_XCB_ERRORS' : [],
    pic: plugs_as_libs)

  xcb_stuff = declare_dependency(link_with: xcb_stuff_lib)
  install_headers('xcb.h', subdir: 'f00bar')
endif

subdir('doc')
subdir('bar')
subdir('decorations')
subdir('particles')
subdir('modules')

f00bar = executable(
  'f00bar',
  'color.h',
  'config-verify.c', 'config-verify.h',
  'config.c', 'config.h',
  'decoration.h',
  'font.c', 'font.h',
  'log.c', 'log.h',
  'main.c',
  'module.c', 'module.h',
  'particle.c', 'particle.h',
  'plugin.c', 'plugin.h',
  'tag.c', 'tag.h',
  'tllist.h',
  'yml.c', 'yml.h',
  dependencies: [bar, cairo, cairo_ft, fontconfig, yaml, threads, dl] +
                decorations + particles + modules,
  build_rpath: '$ORIGIN/modules:$ORIGIN/decorations:$ORIGIN/particles',
  export_dynamic: true,
  install: true,
  install_rpath: '$ORIGIN/../' + get_option('libdir') + '/f00bar')

subdir('test')

install_headers(
  'color.h',
  'config.h',
  'config-verify.h',
  'decoration.h',
  'font.h',
  'log.h',
  'module.h',
  'particle.h',
  'tag.h',
  'tllist.h',
  'yml.h',
  subdir: 'f00bar')

message('')
message('Build type:              @0@'.format(get_option('buildtype')))
message('XCB backend:             @0@'.format(backend_x11))
message('Wayland backend:         @0@'.format(backend_wayland))
message('Core modules as plugins: @0@'.format(plugs_as_libs))
message('')
