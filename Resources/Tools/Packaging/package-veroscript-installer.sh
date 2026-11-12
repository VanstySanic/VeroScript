#!/usr/bin/env bash
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Build a self-contained, localized single-file VeroScript graphical installer.
set -euo pipefail

RUNTIME=""
VERSION=""
RUNTIME_PACKAGE_DIR=""
OUT_ROOT=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--runtime) RUNTIME="$2"; shift 2 ;;
		--version) VERSION="$2"; shift 2 ;;
		--runtime-package-dir) RUNTIME_PACKAGE_DIR="$2"; shift 2 ;;
		--out-root) OUT_ROOT="$2"; shift 2 ;;
		-h|--help)
			echo "Usage: $0 [--runtime linux-x64|linux-arm64|osx-x64|osx-arm64] [--version 0.1.1]"
			exit 0 ;;
		*) echo "Unknown option: $1" >&2; exit 2 ;;
	esac
done

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
MODULE_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUSINESS="Runtime"
if [[ -z "$VERSION" ]]; then
	VERSION="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["businesses"][sys.argv[2]])' "$MODULE_ROOT/Resources/Tools/Packaging/ReleaseVersions.json" "$BUSINESS")"
fi
NATIVE_VERSION="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["nativeVersion"])' "$MODULE_ROOT/Resources/Tools/Packaging/ReleaseVersions.json")"
if [[ -z "$RUNTIME" ]]; then
	case "$(uname -s)" in
		Linux) OS_RID="linux" ;;
		Darwin) OS_RID="osx" ;;
		*) echo "Pass --runtime for this platform." >&2; exit 2 ;;
	esac
	case "$(uname -m)" in
		x86_64|amd64) ARCH="x64" ;;
		arm64|aarch64) ARCH="arm64" ;;
		*) echo "Unsupported architecture: $(uname -m)" >&2; exit 2 ;;
	esac
	RUNTIME="$OS_RID-$ARCH"
fi

RID_OS="${RUNTIME%%-*}"
ARCH="${RUNTIME##*-}"
case "$RID_OS" in
	linux) PLATFORM="linux"; PACKAGE_OS="linux" ;;
	osx|macos) PLATFORM="macos"; PACKAGE_OS="macos" ;;
	win|windows) PLATFORM="windows"; PACKAGE_OS="win" ;;
	*) echo "Unsupported runtime OS: $RID_OS" >&2; exit 2 ;;
esac

case "$(uname -s)" in
	Linux) HOST_OS="linux" ;;
	Darwin) HOST_OS="macos" ;;
	*) echo "Installer packaging requires a native Linux or macOS host." >&2; exit 2 ;;
esac
case "$(uname -m)" in
	x86_64|amd64) HOST_ARCH="x64" ;;
	arm64|aarch64) HOST_ARCH="arm64" ;;
	*) echo "Unsupported host architecture: $(uname -m)" >&2; exit 2 ;;
esac
[[ "$PLATFORM" == "$HOST_OS" && "$ARCH" == "$HOST_ARCH" ]] || {
	echo "Native VeroScript tools cannot be relabeled for $PLATFORM-$ARCH on a $HOST_OS-$HOST_ARCH host." >&2
	exit 2
}

[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid version: $VERSION" >&2; exit 2; }
if [[ -z "$OUT_ROOT" ]]; then
	python3 "$MODULE_ROOT/Resources/Tools/Packaging/veroscript_business_release.py" guard --release-directory "$MODULE_ROOT/Release" >/dev/null
	mkdir -p "$ROOT/.tmp"
	COMPONENT_STAGE="$(mktemp -d "$ROOT/.tmp/installer-component.XXXXXX")"
	trap 'rm -rf "$COMPONENT_STAGE"' EXIT
	INPUT_ARGS=(--runtime "$RUNTIME" --version "$VERSION" --out-root "$COMPONENT_STAGE/output")
	SOURCE_PACKAGE="$RUNTIME_PACKAGE_DIR"
	if [[ -z "$SOURCE_PACKAGE" ]]; then
		SOURCE_ARCHIVE="$MODULE_ROOT/Release/$BUSINESS/$PACKAGE_OS-$ARCH/$VERSION/archives/VeroScript$BUSINESS-$VERSION-$PACKAGE_OS-$ARCH.tar.gz"
		[[ -f "$SOURCE_ARCHIVE" ]] || { echo "Package the Runtime archive first or supply --runtime-package-dir: $SOURCE_ARCHIVE" >&2; exit 1; }
		python3 - "$MODULE_ROOT" "$SOURCE_ARCHIVE" "$COMPONENT_STAGE/inputs" <<'PY'
import sys, tarfile
from pathlib import Path
sys.path.insert(0, str(Path(sys.argv[1]) / 'Resources/Tools/Packaging'))
from veroscript_release import archive_entries
source, destination = Path(sys.argv[2]), Path(sys.argv[3])
archive_entries(source, max_entry_bytes=4096)
destination.mkdir()
with tarfile.open(source, 'r:gz') as archive:
	archive.extractall(destination, filter='data')
PY
		SOURCE_PACKAGE="$COMPONENT_STAGE/inputs/VeroScript$BUSINESS-$VERSION-$PACKAGE_OS-$ARCH"
	fi
	INPUT_ARGS+=(--runtime-package-dir "$SOURCE_PACKAGE")
	bash "$0" "${INPUT_ARGS[@]}"
	python3 "$MODULE_ROOT/Resources/Tools/Packaging/veroscript_business_release.py" publish-component --candidate "$COMPONENT_STAGE/output" \
		--business "$BUSINESS" --version "$VERSION" --target "$PACKAGE_OS-$ARCH"
	exit
fi
if [[ "$PLATFORM" == "macos" ]]; then RUNTIME="osx-$ARCH"; fi
case "$OUT_ROOT" in
	/*) OUT_ROOT_PATH="$OUT_ROOT" ;;
	*) OUT_ROOT_PATH="$ROOT/$OUT_ROOT" ;;
esac
python3 - "$MODULE_ROOT" "$ROOT" "$OUT_ROOT_PATH" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, str(Path(sys.argv[1]) / 'Resources/Tools/Packaging'))
from veroscript_release import safe_output
safe_output(Path(sys.argv[2]), Path(sys.argv[3]), component=True)
PY
RELEASE_ROOT="$(dirname "$OUT_ROOT_PATH")"
RUNTIME_PACKAGE_DIR="${RUNTIME_PACKAGE_DIR:-$RELEASE_ROOT/runtime/VeroScriptRuntime-$VERSION-$PACKAGE_OS-$ARCH}"
[[ -d "$RUNTIME_PACKAGE_DIR" ]] || { echo "Runtime package not found: $RUNTIME_PACKAGE_DIR" >&2; exit 1; }

EXPECTED_HOST_TAG="[$PACKAGE_OS-$ARCH;"
CLI_IDENTITY="$("$RUNTIME_PACKAGE_DIR/veroscript" --version)"
[[ "$CLI_IDENTITY" == "VeroScript $NATIVE_VERSION "* && "$CLI_IDENTITY" == *"$EXPECTED_HOST_TAG"* ]] || {
	echo "Runtime package CLI version/target mismatch. Expected native $NATIVE_VERSION / $PACKAGE_OS-$ARCH, got: $CLI_IDENTITY" >&2; exit 1;
}
python3 "$MODULE_ROOT/Resources/Tools/Packaging/veroscript_icons.py" verify-runtime \
	--repo-root "$ROOT" --root "$RUNTIME_PACKAGE_DIR" --target "$PACKAGE_OS-$ARCH"

PROJECT="$MODULE_ROOT/VeroScriptInstaller/App/VeroScriptInstaller.csproj"
TOOL="$MODULE_ROOT/VeroScriptInstaller/Tool/VeroScriptInstaller.Tool.csproj"
# Use a trailing-X directory template supported by GNU and Apple/BSD mktemp.
# Register cleanup before any subsequent staging or build can fail.
WORK_ROOT="${TMPDIR:-$ROOT/.tmp}"
mkdir -p "$WORK_ROOT"
WORK_ROOT="$(cd -- "$WORK_ROOT" && pwd -P)"
WORK_DIR="$(mktemp -d "$WORK_ROOT/veroscript-installer.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
PUBLISH_DIR="$WORK_DIR/publish"
BUNDLE_DIR="$WORK_DIR/bundle"
EMBEDDED_ARCHIVE="$WORK_DIR/bundle.zip"
export DOTNET_BUNDLE_EXTRACT_BASE_DIR="$WORK_DIR/cache"

installer_dotnet() (
	cd -- "$MODULE_ROOT/VeroScriptInstaller"
	dotnet "$@"
)
ARGS=(run --project "$TOOL" -c Release -- build-bundle
	--output "$BUNDLE_DIR" --version "$VERSION" --profile runtime --platform "$PLATFORM" --architecture "$ARCH")
ARGS+=(--source-runtime "$RUNTIME_PACKAGE_DIR")
installer_dotnet "${ARGS[@]}"
python3 "$MODULE_ROOT/Resources/Tools/Packaging/veroscript_icons.py" verify-bundle \
	--repo-root "$ROOT" --root "$BUNDLE_DIR" --target "$PACKAGE_OS-$ARCH"
installer_dotnet run --project "$TOOL" -c Release -- verify --bundle "$BUNDLE_DIR"
installer_dotnet run --project "$TOOL" -c Release -- pack-bundle --bundle "$BUNDLE_DIR" --output "$EMBEDDED_ARCHIVE"

installer_dotnet publish "$PROJECT" -c Release -r "$RUNTIME" --self-contained true \
	-p:PublishSingleFile=true \
	-p:IncludeNativeLibrariesForSelfExtract=true \
	-p:EnableCompressionInSingleFile=true \
	-p:PublishTrimmed=false \
	-p:DebugType=None \
	-p:DebugSymbols=false \
	-p:Version="$VERSION" \
	-p:InstallerBundleArchive="$EMBEDDED_ARCHIVE" \
	-o "$PUBLISH_DIR"

PUBLISHED_INSTALLER="$PUBLISH_DIR/VeroScriptInstaller"
[[ -f "$PUBLISHED_INSTALLER" ]] || { echo "Published installer not found: $PUBLISHED_INSTALLER" >&2; exit 1; }
chmod +x "$PUBLISHED_INSTALLER"
"$PUBLISHED_INSTALLER" --verify-localization
"$PUBLISHED_INSTALLER" --verify-embedded-bundle
"$PUBLISHED_INSTALLER" --license > "$WORK_DIR/project-license.txt"
cmp -s "$ROOT/LICENSE" "$WORK_DIR/project-license.txt" || {
	echo "The installer does not retain the complete project license notice." >&2
	exit 1
}
"$PUBLISHED_INSTALLER" --third-party-licenses > "$WORK_DIR/third-party-licenses.txt"
cmp -s "$MODULE_ROOT/VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt" "$WORK_DIR/third-party-licenses.txt" || {
	echo "The installer does not retain the complete third-party license notices." >&2
	exit 1
}

python3 - "$MODULE_ROOT" "$PUBLISHED_INSTALLER" "$OUT_ROOT_PATH" "$VERSION" "$PACKAGE_OS-$ARCH" "$EMBEDDED_ARCHIVE" <<'PY'
import sys
from pathlib import Path
repo, program, output = map(Path, sys.argv[1:4])
sys.path.insert(0, str(repo / 'Resources/Tools/Packaging'))
from veroscript_installer import package_unix_installer
from veroscript_release import digest
print(package_unix_installer(repo, program, output, 'Runtime', sys.argv[4], sys.argv[5], digest(Path(sys.argv[6]))))
PY
echo "PATH modification is an explicit, default-off checkbox in the installer."
