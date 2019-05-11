test('is-executable', f00bar, args: ['--version'])

pwd = meson.current_source_dir()

# Configuration validation tests
test('no-config', f00bar, args: ['-C', '-c', 'xyz'], should_fail: true)
test('config-isnt-file', f00bar, args: ['-C', '-c', '.'], should_fail: true)
test('config-no-bar', f00bar, args: ['-C', '-c', join_paths(pwd, 'no-bar.yml')],
     should_fail: true)
test('full-conf-good', f00bar, args: ['-C', '-c', join_paths(pwd, 'full-conf-good.yml')])
