test(
  'lds-terminal-help',
  lds_terminal_exe,
  args: ['--help']
)

test(
  'lds-terminal-renderer-help',
  lds_terminal_exe,
  args: ['--renderer=auto', '--help']
)

test(
  'lds-terminal-renderer-invalid',
  lds_terminal_exe,
  args: ['--renderer=invalid'],
  should_fail: true
)

test(
  'lds-terminal-truecolor-conflict',
  lds_terminal_exe,
  args: ['--truecolor', '--no-truecolor'],
  should_fail: true
)

runtime_regression_sources = files(
  '../src/diag.c',
  '../src/diag_runtime.c',
  '../src/app_args.c',
  '../src/app_runtime.c'
)

runtime_regression_lib = static_library(
  'lds-terminal-runtime-regression',
  runtime_regression_sources,
  include_directories: inc,
  dependencies: deps
)

runtime_regression_dep = declare_dependency(
  link_with: runtime_regression_lib,
  include_directories: inc,
  dependencies: deps
)

integration_regression_sources = files(
  '../src/diag.c',
  '../src/diag_runtime.c',
  '../src/actions.c',
  '../src/search.c',
  '../src/search_engine.c',
  '../src/search_ui.c',
  '../src/overview.c',
  '../src/overview_controller.c',
  '../src/focus_manager.c',
  '../src/app_args.c',
  '../src/app_runtime.c',
  '../src/shortcuts_registry.c',
  '../src/prefs.c',
  '../src/prefs_shortcuts.c',
  '../src/lds_terminal.c',
  '../src/term_registry.c',
  '../src/link_cache.c',
  '../src/link_detect.c',
  '../src/menu.c',
  '../src/settings.c',
  '../src/tabs.c',
  '../src/vte_spawn.c',
  '../src/vte_links.c',
  '../src/vte_panes.c',
  '../src/vte.c',
  '../src/window.c'
)

integration_regression_lib = static_library(
  'lds-terminal-integration-regression',
  integration_regression_sources,
  include_directories: inc,
  dependencies: deps
)

integration_regression_dep = declare_dependency(
  link_with: integration_regression_lib,
  include_directories: inc,
  dependencies: deps
)

spawn_close_regression_test = executable(
  'spawn-close-regression-test',
  'spawn_close_regression_test.c',
  dependencies: integration_regression_dep
)

test(
  'spawn-close-regression',
  spawn_close_regression_test
)

close_concurrency_regression_test = executable(
  'close-concurrency-regression-test',
  'close_concurrency_regression_test.c',
  dependencies: integration_regression_dep
)

test(
  'close-concurrency-regression',
  close_concurrency_regression_test
)

close_tab_closes_tab_regression_test = executable(
  'close-tab-closes-tab-regression-test',
  'close_tab_prefers_pane_regression_test.c',
  dependencies: integration_regression_dep
)

test(
  'close-tab-closes-tab-regression',
  close_tab_closes_tab_regression_test
)

focus_recovery_regression_test = executable(
  'focus-recovery-regression-test',
  'focus_recovery_regression_test.c',
  dependencies: integration_regression_dep
)

test(
  'focus-recovery-regression',
  focus_recovery_regression_test
)

overview_enter_regression_test = executable(
  'overview-enter-regression-test',
  'overview_enter_regression_test.c',
  dependencies: integration_regression_dep
)

test(
  'overview-enter-regression',
  overview_enter_regression_test
)

window_confirm_regression_test = executable(
  'window-confirm-regression-test',
  'window_confirm_regression_test.c',
  dependencies: integration_regression_dep
)

test(
  'window-confirm-regression',
  window_confirm_regression_test,
  env: [
    'GSETTINGS_BACKEND=memory',
    'LDS_TERMINAL_DEV_SCHEMA=1'
  ]
)

settings_consistency_test = executable(
  'settings-consistency-test',
  'settings_consistency_test.c',
  dependencies: integration_regression_dep
)

test(
  'settings-consistency',
  settings_consistency_test,
  env: ['GSETTINGS_BACKEND=memory']
)

settings_fallback_test = executable(
  'settings-fallback-test',
  'settings_fallback_test.c',
  dependencies: integration_regression_dep
)

test(
  'settings-fallback',
  settings_fallback_test,
  workdir: '/tmp',
  env: [
    'GSETTINGS_BACKEND=memory',
    'GSETTINGS_SCHEMA_DIR=/tmp/lds-terminal-no-such-schema'
  ]
)

search_policy_test = executable(
  'search-policy-test',
  'search_policy_test.c',
  dependencies: integration_regression_dep
)

test(
  'search-policy',
  search_policy_test
)

lifecycle_regression_test = executable(
  'lifecycle-regression-test',
  'lifecycle_regression_test.c',
  dependencies: integration_regression_dep
)

test(
  'lifecycle-regression',
  lifecycle_regression_test
  , env: ['GSETTINGS_BACKEND=memory']
)

global_actions_regression_test = executable(
  'global-actions-regression-test',
  'global_actions_regression_test.c',
  dependencies: integration_regression_dep
)

test(
  'global-actions-regression',
  global_actions_regression_test
)

multi_pane_api_test = executable(
  'multi-pane-api-test',
  'multi_pane_api_test.c',
  dependencies: integration_regression_dep
)

test(
  'multi-pane-api',
  multi_pane_api_test
)

json_glib_dep = dependency('json-glib-1.0')
link_detect_cases_json = configure_file(
  input: 'link_detect_cases.json',
  output: 'link_detect_cases.json',
  copy: true
)

link_detect_test = executable(
  'link-detect-test',
  'link_detect_test.c',
  '../src/link_detect.c',
  c_args: ['-DLDS_LINK_DETECT_CASES_JSON_PATH="' + meson.current_build_dir() + '/link_detect_cases.json"'],
  include_directories: inc,
  dependencies: [deps, json_glib_dep]
)

test(
  'link-detect',
  link_detect_test
)

link_cache_test = executable(
  'link-cache-test',
  'link_cache_test.c',
  '../src/link_cache.c',
  '../src/link_detect.c',
  include_directories: inc,
  dependencies: deps
)

test(
  'link-cache',
  link_cache_test
)

app_runtime_policy_test = executable(
  'app-runtime-policy-test',
  'app_runtime_policy_test.c',
  dependencies: runtime_regression_dep
)

test(
  'app-runtime-policy',
  app_runtime_policy_test,
  env: ['GSETTINGS_BACKEND=memory']
)

diag_runtime_lifecycle_test = executable(
  'diag-runtime-lifecycle-test',
  'diag_runtime_lifecycle_test.c',
  dependencies: runtime_regression_dep
)

test(
  'diag-runtime-lifecycle',
  diag_runtime_lifecycle_test
)
