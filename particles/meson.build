particle_sdk = declare_dependency(dependencies: [cairo, cairo_ft])

particles = []
foreach particle : ['empty', 'list', 'map', 'progress-bar', 'ramp', 'string']
  lib = static_library(
    'particle_@0@'.format(particle), '@0@.c'.format(particle),
    dependencies: particle_sdk)
  particles += [declare_dependency(
    link_with: lib,
    compile_args: '-DHAVE_PLUGIN_@0@'.format(particle.underscorify()))]
endforeach

dynlist_lib = static_library(
  'dynlist', 'dynlist.c', 'dynlist.h', dependencies: particle_sdk)
dynlist = declare_dependency(link_with: dynlist_lib)
