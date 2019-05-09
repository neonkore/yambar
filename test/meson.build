test('is-executable', f00bar, args: ['--version'])

pwd = meson.current_source_dir()

# Configuratoin validation tests
test('no-config', f00bar, args: ['-c', 'xyz'], should_fail: true)
test('config-isnt-file', f00bar, args: ['-c', '.'], should_fail: true)
test('config-no-bar', f00bar, args: ['-c', join_paths(pwd, 'no-bar.yml')],
     should_fail: true)
