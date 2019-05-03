sh = find_program('sh', native: true)
scdoc = dependency('scdoc', native: true)
scdoc_prog = find_program(scdoc.get_pkgconfig_variable('scdoc'), native: true)
gzip = find_program('gzip', native: true)

foreach man_src : ['f00bar.1.scd', 'f00bar.5.scd', 'f00bar-decorations.5.scd',
                   'f00bar-modules.5.scd', 'f00bar-particles.5.scd',
                   'f00bar-tags.5.scd']
  parts = man_src.split('.')
  name = parts[-3]
  section = parts[-2]
  out = '@0@.@1@.gz'.format(name, section)

  custom_target(
    out,
    output: out,
    input: man_src,
    command: [sh, '-c', '@0@ < @INPUT@ | @1@ > doc/@2@'.format(
      scdoc_prog.path(), gzip.path(), out)],
    install: true,
    install_dir: '@0@/man@1@'.format(get_option('mandir'), section))
endforeach
