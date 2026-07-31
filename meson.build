project(
  'koboterminal',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

with_nickelhook = get_option('with_nickelhook')

inc = include_directories('include')

# libtsm subproject (vendored)
libtsm_sub = subproject('libtsm', default_options: 'tests=false')
libtsm_dep = libtsm_sub.get_variable('libtsm_dep')

# NickelHook shared library sources (Qt integration + dialog)
nickelhook_sources = files(
  'src/nickelhook.c',
  'src/app.c',
  'src/pty_qt.c',
  'src/ui_dialog.c',
  'src/term_serialize.c',
  'src/term_tsm.c',
)

dependencies = []

# NickelHook shared library build
if with_nickelhook
  nickelhook_cargs = ['-DKTERM_TARGET_KOBO=1']
  nickelhook_cargs += ['-DKTERM_USE_NICKELHOOK=1']

  dependencies += libtsm_dep

  shared_library(
    'koboterminal',
    nickelhook_sources,
    include_directories: inc,
    c_args: nickelhook_cargs,
    dependencies: dependencies,
    link_args: ['-lutil', '-lm', '-ldl'],
    install: true,
  )
endif

summary({
  'with_nickelhook': with_nickelhook,
}, section: 'koboterminal options')
