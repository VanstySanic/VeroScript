#!/usr/bin/env bash
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Package veroscript + DAP/LSP + current module-owned runtime assets into
# the isolated version/platform components directory and a tar.gz.
# Example:
#   ./Resources/Tools/Packaging/package-veroscript.sh --build-preset release-linux
set -euo pipefail

CONFIGURE_PRESET=""
BUILD_PRESET=""
CONFIG="Release"
OUT_ROOT=""
VERSION=""
NO_EXAMPLES=0

while [[ $# -gt 0 ]]; do
	case "$1" in
		--configure-preset) CONFIGURE_PRESET="$2"; shift 2 ;;
		--build-preset) BUILD_PRESET="$2"; shift 2 ;;
		--config) CONFIG="$2"; shift 2 ;;
		--out-root) OUT_ROOT="$2"; shift 2 ;;
		--version) VERSION="$2"; shift 2 ;;
		--no-examples) NO_EXAMPLES=1; shift ;;
		-h|--help)
			sed -n '2,5p' "$0"
			exit 0
			;;
		*)
			echo "Unknown option: $1" >&2
			exit 2
			;;
	esac
done

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
MODULE_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
if [[ -z "$VERSION" ]]; then
	VERSION="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["businesses"]["Runtime"])' "$MODULE_ROOT/Resources/Tools/Packaging/ReleaseVersions.json")"
fi
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid version: $VERSION" >&2; exit 2; }
UNAME_S="$(uname -s)"
UNAME_M="$(uname -m)"

case "$UNAME_S" in
	Linux) OS_TAG="linux" ;;
	Darwin) OS_TAG="macos" ;;
	*) echo "Native Runtime packaging supports only Linux and macOS." >&2; exit 2 ;;
esac

# Map friendly / legacy names onto CMakePresets.json build + configure presets.
if [[ -z "$BUILD_PRESET" && -n "$CONFIGURE_PRESET" ]]; then
	case "$CONFIGURE_PRESET" in
		linux-ninja|linux-ninja-release)
			BUILD_PRESET="release-linux"
			CONFIGURE_PRESET="linux-ninja-release"
			;;
		macos-ninja|macos-ninja-release)
			BUILD_PRESET="release-macos"
			CONFIGURE_PRESET="macos-ninja-release"
			;;
		*)
			echo "Unknown configure preset for auto build mapping: $CONFIGURE_PRESET" >&2
			exit 2
			;;
	esac
elif [[ -z "$BUILD_PRESET" ]]; then
	case "$OS_TAG" in
		linux)
			BUILD_PRESET="release-linux"
			CONFIGURE_PRESET="linux-ninja-release"
			;;
		macos)
			BUILD_PRESET="release-macos"
			CONFIGURE_PRESET="macos-ninja-release"
			;;
		*)
			echo "Set --configure-preset / --build-preset for host $UNAME_S" >&2
			exit 2
			;;
	esac
else
	case "$BUILD_PRESET" in
		linux-ninja-release|release-linux)
			BUILD_PRESET="release-linux"
			CONFIGURE_PRESET="${CONFIGURE_PRESET:-linux-ninja-release}"
			;;
		macos-ninja-release|release-macos)
			BUILD_PRESET="release-macos"
			CONFIGURE_PRESET="${CONFIGURE_PRESET:-macos-ninja-release}"
			;;
		*)
			if [[ -z "$CONFIGURE_PRESET" ]]; then
				echo "Pass --configure-preset with custom --build-preset $BUILD_PRESET" >&2
				exit 2
			fi
			;;
	esac
fi

BUILD_DIR="$ROOT/build/$CONFIGURE_PRESET"
# Ninja single-config puts binaries in bin/; multi-config uses bin/$CONFIG.
if [[ -x "$BUILD_DIR/bin/veroscript" ]]; then
	BIN_DIR="$BUILD_DIR/bin"
elif [[ -x "$BUILD_DIR/bin/$CONFIG/veroscript" ]]; then
	BIN_DIR="$BUILD_DIR/bin/$CONFIG"
else
	BIN_DIR="$BUILD_DIR/bin"
fi

EXE="$BIN_DIR/veroscript"
DAP="$BIN_DIR/veroscript-dap"
LSP="$BIN_DIR/veroscript-lsp"
INTEROP_HEADER="$MODULE_ROOT/VeroScriptInterop/Public/VeroScriptInterop.h"
STDLIB_SRC="$MODULE_ROOT/VeroScript/Resources/Scripts/stdlib"
LOCALIZATION_SRC="$MODULE_ROOT/VeroScript/Resources/Localization"
EXAMPLES_SRC="$MODULE_ROOT/VeroScript/Resources/Scripts/examples"

if [[ -n "$BUILD_PRESET" ]]; then
	echo "Building VeroScript CLI + DAP/LSP (preset: $BUILD_PRESET)..."
	(cd "$ROOT" && cmake --build --preset "$BUILD_PRESET" --target VeroScriptCli VeroScriptDap VeroScriptLsp VeroScriptInterop)
fi

# Re-resolve after build (clean trees may not have had binaries yet).
if [[ -x "$BUILD_DIR/bin/veroscript" ]]; then
	BIN_DIR="$BUILD_DIR/bin"
elif [[ -x "$BUILD_DIR/bin/$CONFIG/veroscript" ]]; then
	BIN_DIR="$BUILD_DIR/bin/$CONFIG"
fi
EXE="$BIN_DIR/veroscript"
DAP="$BIN_DIR/veroscript-dap"
LSP="$BIN_DIR/veroscript-lsp"

case "$OS_TAG" in
	linux) INTEROP_LIBRARY_NAME="libVeroScriptInterop.so" ;;
	macos) INTEROP_LIBRARY_NAME="libVeroScriptInterop.dylib" ;;
esac
INTEROP_LIBRARY=""
for candidate in "$BUILD_DIR/lib/$INTEROP_LIBRARY_NAME" "$BUILD_DIR/lib/$CONFIG/$INTEROP_LIBRARY_NAME" "$BIN_DIR/$INTEROP_LIBRARY_NAME"; do
	if [[ -f "$candidate" ]]; then INTEROP_LIBRARY="$candidate"; break; fi
done

for p in "$EXE" "$DAP" "$LSP"; do
	if [[ ! -x "$p" ]]; then
		echo "Missing executable: $p" >&2
		echo "Build first or pass --build-preset (e.g. release-linux / release-macos)." >&2
		exit 1
	fi
done
[[ -n "$INTEROP_LIBRARY" ]] || { echo "Missing VeroScriptInterop library under $BUILD_DIR/lib" >&2; exit 1; }
[[ -f "$INTEROP_HEADER" ]] || { echo "VeroScriptInterop header not found: $INTEROP_HEADER" >&2; exit 1; }
[[ -d "$STDLIB_SRC" ]] || { echo "stdlib not found: $STDLIB_SRC" >&2; exit 1; }
[[ -d "$LOCALIZATION_SRC" ]] || { echo "localization not found: $LOCALIZATION_SRC" >&2; exit 1; }

ARGS=(--repo-root "$ROOT" --bin-directory "$BIN_DIR"
	  --interop-library "$INTEROP_LIBRARY" --version "$VERSION")
[[ -z "$OUT_ROOT" ]] || ARGS+=(--out-root "$OUT_ROOT")
[[ "$NO_EXAMPLES" -eq 0 ]] || ARGS+=(--no-examples)
python3 "$MODULE_ROOT/Resources/Tools/Packaging/package-veroscript-runtime.py" "${ARGS[@]}"
