project('xdccget', 'c')

configure_file(output : 'config.h',
  configuration : {
    '_FILE_OFFSET_BITS' : 64
  }
)

if not meson.get_compiler('c').has_function('strlcpy', prefix : '#include <string.h>')
  dependencies = dependency('libbsd', required : true)
else
  dependencies = []
endif

executable('xdccget', ['xdccget.c', 'libircclient/src/libircclient.c'], dependencies : dependencies)