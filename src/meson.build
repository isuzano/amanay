resource_sources = get_variable('resource_sources')

gtk_dep = dependency('gtk4')
vte_dep = dependency('vte-2.91-gtk4', version: '>= 0.46')
adw_dep = dependency('libadwaita-1')
gio_dep = dependency('gio-2.0')
glib_dep = dependency('glib-2.0')
x11_dep = dependency('x11', required: false)

lds_terminal_sources = [
  'diag.c',
  'diag_runtime.c',
  'actions.c',
  'search.c',
  'search_engine.c',
  'search_ui.c',
  'overview.c',
  'overview_controller.c',
  'focus_manager.c',
  'app_args.c',
  'app_actions.c',
  'shortcuts_registry.c',
  'app_runtime.c',
  'prefs.c',
  'prefs_shortcuts.c',
  'main.c',
  'lds_terminal.c',
  'term_registry.c',
  'link_cache.c',
  'link_detect.c',
  'menu.c',
  'settings.c',
  'tabs.c',
  'vte_spawn.c',
  'vte_links.c',
  'vte_panes.c',
  'vte.c',
  'window.c'
]

deps = [gtk_dep, vte_dep, adw_dep, gio_dep, glib_dep]
if x11_dep.found()
  deps += x11_dep
endif

lds_terminal_exe = executable(
  'lds-terminal',
  lds_terminal_sources + resource_sources,
  include_directories: inc,
  dependencies: deps,
  install: true
)

set_variable('lds_terminal_exe', lds_terminal_exe)
