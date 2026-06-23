c_args = host_machine.system() == 'windows' ? ['-D_USE_MATH_DEFINES'] : []

testexe = executable('testbin',
  'util.c', 'write_read.c', 'test.c',
  c_args: c_args,
  dependencies : [vorbisenc_dep, vorbis_dep, ogg_dep],
)

test('vorbistest', testexe, timeout: 90)
