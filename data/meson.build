resource_sources = gnome.compile_resources(
  'lds_terminal',
  'lds-terminal.gresource.xml',
  source_dir: '.',
  c_name: 'lds_terminal'
)

set_variable('resource_sources', resource_sources)

gnome.compile_schemas(depend_files: 'bar.astware.lds-terminal.gschema.xml')

install_data(
  'bar.astware.lds-terminal.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas')
)

desktop_install_dir = get_option('desktop_install_dir')
if desktop_install_dir == ''
  desktop_install_dir = join_paths(get_option('datadir'), 'applications')
endif

install_data(
  'bar.astware.lds-terminal.desktop',
  install_dir: desktop_install_dir
)

install_data(
  'lds-terminal.png',
  install_dir: join_paths(get_option('datadir'), 'pixmaps')
)

icon_sizes = [
  '16x16',
  '24x24',
  '32x32',
  '48x48',
  '64x64',
  '128x128'
]

foreach size : icon_sizes
  install_data(
    join_paths('icons', size, 'lds-terminal.png'),
    install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', size, 'apps')
  )
endforeach
