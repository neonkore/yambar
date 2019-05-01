deco_sdk = declare_dependency(dependencies: [cairo, cairo_ft])

decorations = []
foreach deco : ['background', 'stack', 'underline']
  if get_option('core-plugins-as-shared-libraries')
    shared_module('@0@'.format(deco), '@0@.c'.format(deco),
                  dependencies: deco_sdk,
                  name_prefix: 'decoration_',
                  install: true,
                  install_dir: get_option('libdir') + '/f00bar')
  else
    lib = static_library(
      'decoration_@0@'.format(deco), '@0@.c'.format(deco), dependencies: deco_sdk)

    decorations += [declare_dependency(
      link_with: lib,
      compile_args: '-DHAVE_PLUGIN_@0@'.format(deco.underscorify()))]
  endif
endforeach
