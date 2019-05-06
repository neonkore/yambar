particle_sdk = declare_dependency(dependencies: [cairo, cairo_ft])

particles = []
foreach particle : ['empty', 'list', 'map', 'progress-bar', 'ramp', 'string']
  if plugs_as_libs
    shared_module('@0@'.format(particle), '@0@.c'.format(particle),
                  dependencies: particle_sdk,
                  name_prefix: 'particle_',
                  install: true,
                  install_dir: join_paths(get_option('libdir'), 'f00bar'))
  else
    particles += [declare_dependency(
      sources: '@0@.c'.format(particle),
      dependencies: particle_sdk,
      compile_args: '-DHAVE_PLUGIN_@0@'.format(particle.underscorify()))]
  endif
endforeach

dynlist_lib = build_target(
  'dynlist', 'dynlist.c', 'dynlist.h', dependencies: particle_sdk,
  target_type: plugs_as_libs ? 'shared_library' : 'static_library',
  override_options : ['b_lundef=false'],
  install: plugs_as_libs,
  install_dir: get_option('libdir') + '/f00bar',
)

dynlist = declare_dependency(link_with: dynlist_lib)
