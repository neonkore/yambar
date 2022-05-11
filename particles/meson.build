flex = find_program('flex', required: true)
bison = find_program('bison', required: true)

lgen = generator(
  flex,
  output : '@BASENAME@.yy.c',
  arguments : ['-o', '@OUTPUT@', '@INPUT@']
)
lfiles = lgen.process('map.l')

pgen = generator(
  bison,
  output : ['@BASENAME@.tab.c', '@BASENAME@.tab.h'],
  arguments : ['-Wall',
               '-Wcounterexamples',
               '--defines=@OUTPUT1@',
               '--output=@OUTPUT0@', '@INPUT@']
)
pfiles = pgen.process('map.y')

map_parser = declare_dependency(sources: [pfiles, lfiles], include_directories: '.')

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
  'map': [dynlist, map_parser],
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
