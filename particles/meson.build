particle_sdk = declare_dependency(dependencies: [pixman, tllist, fcft])

dynlist_lib = build_target(
  'dynlist', 'dynlist.c', 'dynlist.h', dependencies: particle_sdk,
  target_type: plugs_as_libs ? 'shared_library' : 'static_library',
  override_options : ['b_lundef=false'],
  install: plugs_as_libs,
  install_dir: get_option('libdir') + '/yambar',
)

dynlist = declare_dependency(link_with: dynlist_lib)

# Particle name -> dep-list
deps = {
  'empty': [],
  'list': [],
  'map': [dynlist],
  'progress-bar': [],
  'ramp': [],
  'string': [],
}

particles = []
foreach particle, particle_deps : deps
  if plugs_as_libs
    shared_module('@0@'.format(particle), '@0@.c'.format(particle),
                  dependencies: [particle_sdk] + particle_deps,
                  name_prefix: 'particle_',
                  install: true,
                  install_dir: join_paths(get_option('libdir'), 'yambar'))
  else
    particles += [declare_dependency(
      sources: '@0@.c'.format(particle),
      dependencies: [particle_sdk] + particle_deps,
      compile_args: '-DHAVE_PLUGIN_@0@'.format(particle.underscorify()))]
  endif
endforeach
