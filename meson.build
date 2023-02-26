project('xget', 'c')

config = configuration_data({
  '_FILE_OFFSET_BITS' : 64
})

compiler = meson.get_compiler('c')
if not compiler.has_function('strlcpy', prefix : '#include <string.h>')
  dependencies = [ dependency('threads'), dependency('libbsd-overlay') ]
else
  dependencies = [ dependency('threads') ]
endif

configure_file(output : 'config.h', configuration : config)
executable('xget', ['xget.c', 'libircclient/src/libircclient.c'], dependencies : dependencies)

xget_test = executable('xget-test', 'test/xget-test.c')
test('default', xget_test)
