deco_sdk = declare_dependency(dependencies: [cairo, cairo_ft])

decorations = []
foreach deco : ['background', 'stack', 'underline']
  lib = static_library(
    'decoration_@0@'.format(deco), '@0@.c'.format(deco), dependencies: deco_sdk)
  decorations += [declare_dependency(
    link_with: lib,
    compile_args: '-DHAVE_PLUGIN_@0@'.format(deco.underscorify()))]
endforeach
