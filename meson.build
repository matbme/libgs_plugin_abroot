project('gs_plugin_abroot', 'c')

files = [
  'packagekit-common.c',
  'gs-packagekit-helper.c',
  'gs-packagekit-task.c',
  'gs-plugin-abroot.c'
]

deps = [
  dependency('glib-2.0', version : '>= 2.70.0'),
  dependency('gnome-software'),
  dependency('packagekit-glib2', version : '>= 1.1.0')
]

# Required for gs-packagekit-task.c
conf = configuration_data()
conf.set_quoted('GETTEXT_PACKAGE', 'gnome-software')
conf.set('HAVE_PACKAGEKIT', '1')
conf.set('GS_PACKAGEKIT_AUTOREMOVE', 'FALSE')
configure_file(
  output : 'config.h',
  configuration : conf
)

args = [
  '-fPIC',
  '-DI_KNOW_THE_GNOME_SOFTWARE_API_IS_SUBJECT_TO_CHANGE',
  '-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE'
]

shared_module(
  'gs_plugin_abroot',
  files,
  dependencies: deps,
  c_args: args
)
