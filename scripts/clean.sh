#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_ASAN_DIR="${ROOT_DIR}/build-asan"

REMOVE_ASAN=false
REMOVE_ALL=false

usage() {
	cat <<'USAGE'
Usage: scripts/clean.sh [options]

Options:
  --asan          Remove the ASAN/UBSAN build directory too
  --all           Remove extra Meson/Ninja artifacts in the repository root
  -h, --help      Show this help text

Examples:
  scripts/clean.sh
  scripts/clean.sh --asan
  scripts/clean.sh --all
USAGE
}

while (($#)); do
	case "$1" in
	--asan)
		REMOVE_ASAN=true
		;;
	--all)
		REMOVE_ALL=true
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

echo "Removing main build directory..."
rm -rf "${BUILD_DIR}"

if [[ "${REMOVE_ASAN}" == true || "${REMOVE_ALL}" == true ]]; then
	echo "Removing ASAN build directory..."
	rm -rf "${BUILD_ASAN_DIR}"
fi

if [[ "${REMOVE_ALL}" == true ]]; then
	echo "Removing root Meson/Ninja artifacts..."
	rm -rf \
		"${ROOT_DIR}/.cache" \
		"${ROOT_DIR}/.ninja_deps" \
		"${ROOT_DIR}/.ninja_log" \
		"${ROOT_DIR}/meson-private" \
		"${ROOT_DIR}/meson-logs" \
		"${ROOT_DIR}/compile_commands.json"
fi

echo "Clean finished successfully."
