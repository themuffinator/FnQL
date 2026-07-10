project('vorbis', 'c', version : '1.3.7', license : 'bsd-style')

# The upstream 1.3.7 sources intentionally use portable ISO C file/string
# functions. Microsoft marks those declarations deprecated, which creates
# clang-cl/MSVC diagnostics without identifying an actual portability defect.
# Keep the suppression inside this fallback overlay so FnQL-owned targets
# retain their strict warning policy and the fetched vendor sources stay
# untouched.
if host_machine.system() == 'windows'
  add_project_arguments('-D_CRT_SECURE_NO_WARNINGS', language: 'c')
endif

ogg_dep = dependency('ogg')

# configure.ac
lib_current = 4
lib_revision = 9
lib_age = 4
lib_soversion = lib_current - lib_age
lib_version = '@0@.@1@.@2@'.format(lib_soversion, lib_age, lib_revision)

file_lib_current = 6
file_lib_revision = 8
file_lib_age = 3
vorbisfile_soversion = file_lib_current - file_lib_age
vorbisfile_version = '@0@.@1@.@2@'.format(vorbisfile_soversion, file_lib_age, file_lib_revision)

enc_lib_current = 2
enc_lib_revision = 12
enc_lib_age = 0
vorbisenc_soversion = enc_lib_current - enc_lib_age
vorbisenc_version = '@0@.@1@.@2@'.format(vorbisenc_soversion, enc_lib_age, enc_lib_revision)

headers = files(
  'include/vorbis/codec.h',
  'include/vorbis/vorbisenc.h',
  'include/vorbis/vorbisfile.h'
)
install_headers(headers, subdir: 'vorbis')

incdir = include_directories('include')
subdir('lib')
if get_option('tests')
  subdir('test')
endif
