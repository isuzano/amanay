#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

fail=0

info() {
	printf '[identity] %s\n' "$1"
}

error() {
	printf 'ERROR: %s\n' "$1"
	fail=1
}

print_matches() {
	local matches="$1"
	local allowed_file="$2"

	printf '%s\n' "${matches}" |
		sed 's#^\./##' |
		awk -F: -v allowed="${allowed_file}" '
			$1 != allowed {
				print "  " $1 ":" $2 ": " substr($0, index($0, $3))
			}
		'
}

scan_forbidden() {
	local label="$1"
	local needle="$2"
	local matches
	local filtered
	shift 2
	local allowed_files=("$@")

	matches=$(grep -RIn -I --exclude-dir=.git --exclude-dir=build --exclude=identity_guard.sh -F "${needle}" "${forbidden_scan_targets[@]}" || true)
	if [[ -z "${matches}" ]]; then
		return
	fi

	filtered=$(printf '%s\n' "${matches}" | sed 's#^\./##' | awk -F: -v allowed_list="${allowed_files[*]}" '
		BEGIN {
			n = split(allowed_list, allowed, " ")
		}
		{
			keep = 0
			for (i = 1; i <= n; i++) {
				if (allowed[i] != "" && $1 == allowed[i]) {
					keep = 1
					break
				}
			}
			if (!keep)
				print "  " $1 ":" $2 ": " substr($0, index($0, $3))
		}
	')

	if [[ -n "${filtered}" ]]; then
		error "${label}"
		printf '%s\n' "${filtered}"
	fi
}

require_file() {
	local path="$1"
	if [[ ! -f "${path}" ]]; then
		error "missing required file: ${path}"
	fi
}

require_line() {
	local path="$1"
	local line="$2"
	if ! grep -Fxq "${line}" "${path}"; then
		error "missing required line in ${path}: ${line}"
	fi
}

extract_value() {
	local path="$1"
	local sed_expr="$2"
	sed -n "${sed_expr}" "${path}" | head -n1
}

info "scanning project identity"

forbidden_scan_targets=(
	"README.md"
	"docs/BRANDING.md"
	"docs/DESIGN.md"
	"docs/CHANGELOG.md"
	"data/bar.astware.lds-terminal.desktop"
	"data/bar.astware.lds-terminal.gschema.xml"
	"data/lds-terminal.gresource.xml"
)

require_file "data/bar.astware.lds-terminal.desktop"
require_file "data/bar.astware.lds-terminal.gschema.xml"
require_file "data/lds-terminal.gresource.xml"
require_file "src/main.c"
require_file "src/lds_terminal.c"

scan_forbidden "legacy application-id found" "bar.astware.amanay"
scan_forbidden "legacy desktop Exec found" "Exec=amanay"
scan_forbidden "legacy desktop icon found" "Icon=bar.astware.amanay"
scan_forbidden "legacy StartupWMClass found" "StartupWMClass=AmanayTerminal"
scan_forbidden "legacy technical AmanayTerminal token found" "AmanayTerminal"
scan_forbidden "legacy lowercase amanay token found" "amanay"
scan_forbidden "branding string Amanay Terminal found outside the desktop file" "Amanay Terminal" \
	"data/bar.astware.lds-terminal.desktop"

require_line "data/bar.astware.lds-terminal.desktop" "Name=Amanay Terminal"
require_line "data/bar.astware.lds-terminal.desktop" "Exec=lds-terminal"
require_line "data/bar.astware.lds-terminal.desktop" "Icon=lds-terminal"
require_line "data/bar.astware.lds-terminal.desktop" "StartupWMClass=LdsTerminal"
require_line "data/bar.astware.lds-terminal.gschema.xml" '  <schema id="bar.astware.lds-terminal" path="/bar/astware/lds-terminal/">'
require_line "data/lds-terminal.gresource.xml" '  <gresource prefix="/bar/astware/lds-terminal">'
require_line "src/lds_terminal.c" '		gdk_x11_display_set_program_class(display, "LdsTerminal");'
require_line "src/main.c" '	g_autoptr(AdwApplication) app = adw_application_new("bar.astware.lds-terminal", flags);'
require_line "src/main.c" '		gtk_window_set_default_icon_name("lds-terminal");'
require_line "src/lds_terminal.c" '	gtk_window_set_icon_name(GTK_WINDOW(terminal->window), "lds-terminal");'
require_line "src/menu.c" '	adw_about_dialog_set_application_icon(dlg, "lds-terminal");'

application_id=$(extract_value "src/main.c" 's/.*adw_application_new("\([^"]*\)".*/\1/p')
schema_id=$(extract_value "data/bar.astware.lds-terminal.gschema.xml" 's/.*<schema id="\([^"]*\)".*/\1/p')
resource_prefix=$(extract_value "data/lds-terminal.gresource.xml" 's/.*<gresource prefix="\([^"]*\)".*/\1/p')
wm_class=$(extract_value "src/lds_terminal.c" 's/.*gdk_x11_display_set_program_class(display, "\([^"]*\)".*/\1/p')
startup_wm_class=$(extract_value "data/bar.astware.lds-terminal.desktop" 's/^StartupWMClass=\(.*\)$/\1/p')
exec_name=$(extract_value "data/bar.astware.lds-terminal.desktop" 's/^Exec=\(.*\)$/\1/p')
icon_name=$(extract_value "data/bar.astware.lds-terminal.desktop" 's/^Icon=\(.*\)$/\1/p')
expected_prefix="/${application_id//./\/}"

if [[ "${application_id}" != "bar.astware.lds-terminal" ]]; then
	error "application-id mismatch: expected bar.astware.lds-terminal, got ${application_id:-<empty>}"
fi

if [[ "${schema_id}" != "${application_id}" ]]; then
	error "schema id mismatch: expected ${application_id}, got ${schema_id:-<empty>}"
fi

if [[ "${resource_prefix}" != "${expected_prefix}" ]]; then
	error "resource prefix mismatch: expected ${expected_prefix}, got ${resource_prefix:-<empty>}"
fi

if [[ "${wm_class}" != "LdsTerminal" ]]; then
	error "WM_CLASS mismatch: expected LdsTerminal, got ${wm_class:-<empty>}"
fi

if [[ "${startup_wm_class}" != "${wm_class}" ]]; then
	error "StartupWMClass mismatch: expected ${wm_class}, got ${startup_wm_class:-<empty>}"
fi

if [[ "${exec_name}" != "lds-terminal" ]]; then
	error "Exec mismatch: expected lds-terminal, got ${exec_name:-<empty>}"
fi

if [[ "${icon_name}" != "lds-terminal" ]]; then
	error "Icon mismatch: expected lds-terminal, got ${icon_name:-<empty>}"
fi

if (( fail != 0 )); then
	printf '[identity] validation failed\n'
	exit 1
fi

printf '[identity] validation passed\n'
