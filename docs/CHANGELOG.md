# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog and this project follows semantic versioning pre-release tags.

## [Unreleased]

### Added
- Public-facing text now frames Amanay as the GTK4 + Libadwaita terminal for the Light Desktop
  Stack.

### Fixed
- Settings fallback behavior now respects an explicitly provided `GSETTINGS_SCHEMA_DIR`.
- Lifecycle regression tests now run against the memory backend to avoid host `dconf` coupling.

## [0.1.0-rc1] - 2026-02-28

### Added
- Link detection hardening with curated TLD validation and bounded behavior.
- Confirm-close policy for running foreground jobs.
- Strict determinism mode enabled by default.
- Runtime diagnostics/watchdog flow for long-session troubleshooting.

### Changed
- Shortcut registry and preferences flow refactored for consistency.
- Search count/snapshot flow hardened against stale worker races.
- Terms ownership lifecycle consolidated with stronger invariants.
- Build metadata aligned to MIT licensing.

### Fixed
- Tab detach/create-window flow stabilized for AdwTabView multi-window behavior.
- Async close and shutdown teardown made safer under concurrency.
- Settings load/apply path hardened with schema-fallback behavior.
- URL/open flow normalized to avoid malformed click targets.

### Notes
- `lds-terminal-renderer-invalid` and `lds-terminal-truecolor-conflict` are expected-fail tests and are part of
  CLI contract validation.
