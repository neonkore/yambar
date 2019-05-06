bar_backends = []

if backend_x11
  bar_x11 = declare_dependency(sources: ['xcb.c', 'xcb.h'],
                               dependencies: [xcb_stuff, cairo, cairo_ft])
  bar_backends += [bar_x11]
endif

if backend_wayland
  wayland_protocols = dependency('wayland-protocols')
  wayland_protocols_datadir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')

  wscanner = dependency('wayland-scanner', native: true)
  wscanner_prog = find_program(
    wscanner.get_pkgconfig_variable('wayland_scanner'), native: true)

  wayland_protocol_header = generator(
    wscanner_prog,
    output: '@BASENAME@.h',
    arguments: ['client-header', '@INPUT@', '@OUTPUT@'])
  wayland_protocol_source = generator(
    wscanner_prog,
    output: '@BASENAME@.c',
    arguments: ['private-code', '@INPUT@', '@OUTPUT@'])

  generated_wayland_protocols = []
  foreach prot : [
    '../external/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml',
    wayland_protocols_datadir + '/stable/xdg-shell/xdg-shell.xml',
    wayland_protocols_datadir + '/unstable/xdg-output/xdg-output-unstable-v1.xml']

    generated_wayland_protocols += [
      wayland_protocol_header.process(prot),
      wayland_protocol_source.process(prot)]
  endforeach

  bar_wayland = declare_dependency(
    sources: ['wayland.c', 'wayland.h'] + generated_wayland_protocols,
    dependencies: [wayland_client, wayland_cursor, cairo, cairo_ft])

  bar_backends += [bar_wayland]
endif

bar = declare_dependency(
  sources: ['bar.c', 'bar.h', 'private.h', 'backend.h'],
  dependencies: bar_backends + [threads])

install_headers('bar.h', subdir: 'f00bar/bar')
