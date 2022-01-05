project('xdccget', 'c')

config = configuration_data({
  '_FILE_OFFSET_BITS' : 64
})

if meson.get_compiler('c').has_header('endian.h')
  config.set('HAVE_ENDIAN_H', true)
endif

if not meson.get_compiler('c').has_function('strlcpy', prefix : '#include <string.h>')
  dependencies = dependency('libbsd-overlay', required : true)
else
  dependencies = []
endif

configure_file(output : 'config.h', configuration : config)
executable('xdccget', ['xdccget.c', 'libircclient/src/libircclient.c'], dependencies : dependencies)