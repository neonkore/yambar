particle_sdk = declare_dependency(dependencies: [cairo, cairo_ft])

particles = []
foreach particle : ['empty', 'list', 'map', 'progress-bar', 'ramp', 'string']
  if get_option('core-plugins-as-shared-libraries')
    shared_module('@0@'.format(particle), '@0@.c'.format(particle),
                  dependencies: particle_sdk,
                  name_prefix: 'particle_',
                  install: true,
                  install_dir: join_paths(get_option('libdir'), 'f00bar'))
  else
    lib = static_library(
      'particle_@0@'.format(particle), '@0@.c'.format(particle),
      dependencies: particle_sdk)

    particles += [declare_dependency(
      link_with: lib,
      compile_args: '-DHAVE_PLUGIN_@0@'.format(particle.underscorify()))]
  endif
endforeach

dynlist_lib = build_target(
  'dynlist', 'dynlist.c', 'dynlist.h', dependencies: particle_sdk,
  target_type: (get_option('core-plugins-as-shared-libraries')
                ? 'shared_library' : 'static_library'),
  override_options : ['b_lundef=false'],
  install: get_option('core-plugins-as-shared-libraries'),
  install_dir: get_option('libdir') + '/f00bar',
)

dynlist = declare_dependency(link_with: dynlist_lib)
