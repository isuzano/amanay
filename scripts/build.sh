#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_ASAN_DIR="${ROOT_DIR}/build-asan"

ENABLE_TESTS=true
ENABLE_ASAN=false
DESKTOP_LOCAL=false
FORCE_FRESH=false

usage() {
	cat <<'USAGE'
Usage: scripts/build.sh [options]

Options:
  --no-tests        Configure Meson with -Dtests=false
  --asan            Create an additional ASAN/UBSAN build in build-asan
  --desktop-local   Install the desktop entry under ~/.local/share/applications
  --fresh           Remove the main build directory before configuring
  -h, --help        Show this help text

Environment:
  BUILD_DIR         Override the main build directory
  BUILD_ASAN_DIR    Override the ASAN build directory

Examples:
  scripts/build.sh
  scripts/build.sh --no-tests
  scripts/build.sh --asan
USAGE
}

require_tool() {
	local tool="$1"
	if ! command -v "${tool}" >/dev/null 2>&1; then
		echo "Missing required tool: ${tool}" >&2
		exit 1
	fi
}

while (($#)); do
	case "$1" in
	--no-tests)
		ENABLE_TESTS=false
		;;
	--asan)
		ENABLE_ASAN=true
		;;
	--desktop-local)
		DESKTOP_LOCAL=true
		;;
	--fresh)
		FORCE_FRESH=true
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		echo "Invalid option: $1" >&2
		usage
		exit 1
		;;
	esac
	shift
done

require_tool meson
require_tool ninja

if [[ "${FORCE_FRESH}" == true ]]; then
	rm -rf "${BUILD_DIR}"
fi

MESON_ARGS=()
if [[ "${ENABLE_TESTS}" == true ]]; then
	MESON_ARGS+=("-Dtests=true")
else
	MESON_ARGS+=("-Dtests=false")
fi

if [[ "${DESKTOP_LOCAL}" == true ]]; then
	MESON_ARGS+=("-Ddesktop_install_dir=${HOME}/.local/share/applications")
fi

if [[ -d "${BUILD_DIR}" ]]; then
	echo "Reconfiguring main build..."
	meson setup --reconfigure "${BUILD_DIR}" "${MESON_ARGS[@]}"
else
	echo "Configuring main build..."
	meson setup "${BUILD_DIR}" "${MESON_ARGS[@]}"
fi

echo "Compiling main build..."
meson compile -C "${BUILD_DIR}"

if [[ "${ENABLE_ASAN}" == true ]]; then
	ASAN_ARGS=("${MESON_ARGS[@]}" "-Db_sanitize=address,undefined")

	if [[ -d "${BUILD_ASAN_DIR}" ]]; then
		echo "Reconfiguring ASAN build..."
		meson setup --reconfigure "${BUILD_ASAN_DIR}" "${ASAN_ARGS[@]}"
	else
		echo "Configuring ASAN build..."
		meson setup "${BUILD_ASAN_DIR}" "${ASAN_ARGS[@]}"
	fi

	echo "Compiling ASAN build..."
	meson compile -C "${BUILD_ASAN_DIR}"
fi

echo "Build finished successfully."
