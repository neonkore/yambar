test('is-executable', yambar, args: ['--version'])

pwd = meson.current_source_dir()

# Configuration validation tests
test('no-config', yambar, args: ['-C', '-c', 'xyz'], should_fail: true)
test('config-isnt-file', yambar, args: ['-C', '-c', '.'], should_fail: true)
test('config-no-bar', yambar, args: ['-C', '-c', join_paths(pwd, 'no-bar.yml')],
     should_fail: true)
test('full-conf-good', yambar, args: ['-C', '-c', join_paths(pwd, 'full-conf-good.yml')])
