project('eigenvalue', 'c',
          version: '0.0.0',
          license: 'GPLv3+',
    meson_version: '>= 0.63.0',
  default_options: [
    'warning_level=1',
    'buildtype=debugoptimized',
    'c_std=gnu11',
  ],
)

add_project_arguments([
  '-I' + meson.project_build_root(),
  '-DCMATRIX_USE_EXPERIMENTAL_API',
  '-DG_LOG_DOMAIN="eigenvalue"',
], language: 'c')

app_id = 'org.sigxcpu.Eigenvalue'

root_inc = include_directories('.')
src_inc = include_directories('src')

prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')
localedir = prefix / get_option('localedir')
desktopdir = prefix / datadir / 'applications'
pkgdatadir = prefix / datadir / meson.project_name()

cc = meson.get_compiler('c')

glib_ver = '2.78'
glib_ver_str = 'GLIB_VERSION_@0@'.format(glib_ver.replace('.','_'))
glib_ver_cmp = '>=@0@'.format(glib_ver)
gio_dep        = dependency('gio-2.0', version: glib_ver_cmp)
gio_unix_dep   = dependency('gio-unix-2.0', version: glib_ver_cmp)
glib_dep       = dependency('glib-2.0', version: glib_ver_cmp)
gobject_dep    = dependency('gobject-2.0', version: glib_ver_cmp)
gdkpixbuf_dep  = dependency('gdk-pixbuf-2.0')
libcmatrix_dep = dependency('libcmatrix',
  fallback: ['libcmatrix', 'libcmatrix_dep'],
  default_options: [
    'build-examples=false',
    'build-tests=false',
  ])
libedit_dep = dependency('libedit')

global_c_args = []
test_c_args = [
  '-Wcast-align',
  '-Wdate-time',
  ['-Werror=format-security', '-Werror=format=2'],
  '-Wendif-labels',
  '-Werror=incompatible-pointer-types',
  '-Werror=missing-declarations',
  '-Werror=overflow',
  '-Werror=return-type',
  '-Werror=shift-count-overflow',
  '-Werror=shift-overflow=2',
  '-Werror=implicit-fallthrough=3',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmaybe-uninitialized',
  '-Wmissing-field-initializers',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wnested-externs',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wtype-limits',
  '-Wundef',
  '-Wunused-function',
  '-Wswitch-default',
  '-Wfloat-equal',
  '-Wswitch-enum',
]

if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif

c_args = cc.get_supported_arguments(test_c_args)

if cc.get_id() == 'clang'
  # Avoid cast align warnings for wl_container_of, etc
  c_args += '-Wno-cast-align'
endif

add_project_arguments(
  c_args,
  language: 'c'
)

gnome = import('gnome')
i18n  = import('i18n')

config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_h.set_quoted('LOCALEDIR', localedir)
config_h.set_quoted('EV_PROJECT', meson.project_name())
config_h.set_quoted('EV_APP_ID', app_id)
config_h.set_quoted('EV_VERSION', meson.project_version())

phoc_config_h = configure_file(
  output: 'ev-config.h',
  configuration: config_h,
)

subdir('po')
subdir('src')
subdir('data')

run_data = configuration_data()
run_data.set('ABS_BUILDDIR', meson.current_build_dir())
run_data.set('ABS_SRCDIR', meson.current_source_dir())
configure_file(
  input: 'run.in',
  output: 'run',
  configuration: run_data)
