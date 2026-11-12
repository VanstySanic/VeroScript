#!/usr/bin/env bash
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Install a local Runtime through the managed installer engine. PATH is opt-in.
set -euo pipefail
CONFIGURE_PRESET=""
CONFIG="Release"
SOURCE_DIR=""
INSTALL_DIR=""
ADD_PATH=0
NO_PATH=0
while [[ $# -gt 0 ]]; do
	case "$1" in
		--configure-preset|--config|--source-dir|--install-dir)
			[[ $# -ge 2 && -n "$2" ]] || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--configure-preset) CONFIGURE_PRESET="$2" ;;
				--config) CONFIG="$2" ;;
				--source-dir) SOURCE_DIR="$2" ;;
				--install-dir) INSTALL_DIR="$2" ;;
			esac
			shift 2 ;;
		--add-to-path) ADD_PATH=1; shift ;;
		--no-path) NO_PATH=1; shift ;;
		-h|--help)
			echo "Usage: $0 [--configure-preset preset] [--config Release|Debug] [--source-dir dir] [--install-dir product-root] [--add-to-path|--no-path]"
			echo "Requires Python 3.11+ and .NET 9 SDK (or a compatible newer SDK). Executables are installed in Runtime/."
			exit 0 ;;
		*) echo "Unknown option: $1" >&2; exit 2 ;;
	esac
done
[[ "$CONFIG" == Release || "$CONFIG" == Debug ]] || { echo "Invalid configuration: $CONFIG" >&2; exit 2; }
[[ "$ADD_PATH" -eq 0 || "$NO_PATH" -eq 0 ]] || { echo "Choose either --add-to-path or --no-path" >&2; exit 2; }
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
MODULE_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
if [[ -z "$CONFIGURE_PRESET" ]]; then
	case "$(uname -s)" in
		Linux) CONFIGURE_PRESET="linux-ninja" ;;
		Darwin) CONFIGURE_PRESET="macos-ninja" ;;
		*) echo "Use the PowerShell entry point on Windows" >&2; exit 2 ;;
	esac
	[[ "$CONFIG" != Release ]] || CONFIGURE_PRESET+="-release"
fi
if [[ -z "$SOURCE_DIR" ]]; then
	BUILD_DIR="$ROOT/build/$CONFIGURE_PRESET"
	if [[ -x "$BUILD_DIR/bin/$CONFIG/veroscript" ]]; then
		SOURCE_DIR="$BUILD_DIR/bin/$CONFIG"
	else
		SOURCE_DIR="$BUILD_DIR/bin"
	fi
fi
ARGS=("$MODULE_ROOT/Resources/Tools/Packaging/install-veroscript-runtime.py" --repo-root "$ROOT" --source-dir="$SOURCE_DIR" --config "$CONFIG")
[[ -z "$INSTALL_DIR" ]] || ARGS+=(--install-dir="$INSTALL_DIR")
[[ "$ADD_PATH" -eq 0 ]] || ARGS+=(--add-to-path)
exec python3 "${ARGS[@]}"
