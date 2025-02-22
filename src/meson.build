ev_deps = [
  gdkpixbuf_dep,
  gio_dep,
  gio_unix_dep,
  glib_dep,
  gobject_dep,
  libcmatrix_dep,
  libedit_dep,
]

eigenvalue = executable(
  'eigenvalue',
  [
    'main.c',
    'ev-application.c',
    'ev-format-builder.c',
    'ev-matrix.c',
    'ev-prompt.c',
  ],
  dependencies: ev_deps,
  install: true,
)
