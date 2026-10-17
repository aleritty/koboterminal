project(
  'koboterminal',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

executable(
  'kterm-server',
  ['src/kterm_server.c', 'src/sha1.c'],
  link_args: ['-lutil'],
  install: true,
)
