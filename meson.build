project(
  'koboterminal',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

kobo_target = get_option('kobo_target')
with_real_fbink = get_option('with_real_fbink')
with_real_libtsm = get_option('with_real_libtsm')

inc = include_directories('include')

common_sources = files(
  'src/main.c',
  'src/app.c',
  'src/config.c',
  'src/pty.c',
  'src/term_tsm.c',
  'src/renderer_fbink.c',
  'src/input_evdev.c',
  'src/osk.c',
)

dependencies = []

if with_real_fbink
  fbink_inc = include_directories('third_party/FBInk', 'third_party/FBInk/cutef8', 'third_party/FBInk/libunibreak/src')
  fbink_sources = files(
    'third_party/FBInk/fbink.c',
    'third_party/FBInk/cutef8/utf8.c',
    'third_party/FBInk/cutef8/dfa.c',
    'third_party/FBInk/libunibreak/src/unibreakbase.c',
    'third_party/FBInk/libunibreak/src/unibreakdef.c',
    'third_party/FBInk/libunibreak/src/eastasianwidthdef.c',
    'third_party/FBInk/libunibreak/src/linebreakdata.c',
    'third_party/FBInk/libunibreak/src/linebreakdef.c',
    'third_party/FBInk/libunibreak/src/linebreak.c',
  )
  fbink_lib = static_library(
    'fbink_minimal',
    fbink_sources,
    include_directories: fbink_inc,
    c_args: [
      '-std=gnu11',
      '-DFBINK_FOR_LINUX',
      '-DFBINK_MINIMAL',
      '-DFBINK_WITH_DRAW',
      '-DFBINK_WITH_OPENTYPE',
    ],
  )
  dependencies += declare_dependency(
    link_with: fbink_lib,
    include_directories: fbink_inc,
  )
endif

cargs = []
if kobo_target
  cargs += ['-DKTERM_TARGET_KOBO=1']
else
  cargs += ['-DKTERM_TARGET_HOST=1']
endif

if with_real_fbink
  cargs += ['-DKTERM_USE_REAL_FBINK=1']
endif

if with_real_libtsm
  cargs += ['-DKTERM_USE_REAL_LIBTSM=1']
endif

executable(
  'koboterminal',
  common_sources,
  include_directories: inc,
  c_args: cargs,
  dependencies: dependencies,
  link_args: ['-lutil', '-lm'],
  install: true,
)

summary({
  'kobo_target': kobo_target,
  'with_real_fbink': with_real_fbink,
  'with_real_libtsm': with_real_libtsm,
}, section: 'koboterminal options')
