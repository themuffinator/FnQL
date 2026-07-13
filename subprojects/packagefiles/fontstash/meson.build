project('fontstash', 'c', license: 'Zlib')

fontstash_dep = declare_dependency(
  include_directories: include_directories('src', is_system: true),
)

meson.override_dependency('fontstash', fontstash_dep)
