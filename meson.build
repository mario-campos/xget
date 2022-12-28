project('xget', 'c')

config = configuration_data({
  '_FILE_OFFSET_BITS' : 64
})

compiler = meson.get_compiler('c')

configure_file(output : 'config.h', configuration : config)
executable('xget', ['xget.c', 'libircclient/src/libircclient.c'], dependencies : dependency('threads'))
executable('xget-test', 'test/xget-test.c')