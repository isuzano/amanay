project(
  'lds-terminal',
  'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=true'
  ]
)

gnome = import('gnome')
i18n = import('i18n')
cc = meson.get_compiler('c')

inc = include_directories('include', 'include/internal')

strict_build = get_option('strict_build')
if strict_build and not get_option('werror')
  error('strict_build=true requires Meson option -Dwerror=true')
endif

system_policy = get_option('system_check_policy')
host_system = host_machine.system()
host_cpu = host_machine.cpu_family()
supported_systems = ['linux', 'freebsd']

if not supported_systems.contains(host_system)
  msg = 'Unsupported host system "' + host_system + '" (cpu: ' + host_cpu + ')'
  if system_policy == 'error'
    error(msg)
  else
    warning(msg)
  endif
endif

renderer_opt = get_option('default_renderer')
renderer_define = 'LDS_TERMINAL_RENDERER_AUTO'
if renderer_opt == 'cairo'
  renderer_define = 'LDS_TERMINAL_RENDERER_CAIRO'
elif renderer_opt == 'ngl'
  renderer_define = 'LDS_TERMINAL_RENDERER_NGL'
elif renderer_opt == 'vulkan'
  renderer_define = 'LDS_TERMINAL_RENDERER_VULKAN'
endif

add_project_arguments('-DLDS_TERMINAL_DEFAULT_RENDERER_MODE=' + renderer_define, language: 'c')
add_project_arguments('-DLDS_TERMINAL_VERSION="' + meson.project_version() + '"', language: 'c')
add_project_arguments('-DGETTEXT_PACKAGE="lds-terminal"', language: 'c')

subdir('po')
subdir('data')
subdir('man')
subdir('src')
if get_option('tests')
  subdir('tests')
endif

custom_target(
  'lds-terminal-launcher',
  output: 'lds-terminal',
  command: ['ln', '-sf', join_paths('src', 'lds-terminal'), '@OUTPUT@'],
  depends: lds_terminal_exe,
  build_by_default: true
)

gnome.post_install(gtk_update_icon_cache: true)

summary({
  'Host system': host_system,
  'Host CPU': host_cpu,
  'Strict build (-Werror)': strict_build,
  'System check policy': system_policy,
  'Default renderer': renderer_opt,
}, section: 'Build configuration')
