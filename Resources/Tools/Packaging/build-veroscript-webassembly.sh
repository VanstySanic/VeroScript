#!/usr/bin/env bash
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

set -euo pipefail

configuration="${1:-Release}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../../.." && pwd)"
module_root="$(cd -- "${script_dir}/../../.." && pwd)"
build_dir="${VEROSCRIPT_WEBASSEMBLY_BUILD_DIR:-${repo_root}/build/webassembly}"

if ! command -v emcmake >/dev/null 2>&1; then
	echo "emcmake was not found. Activate the Emscripten SDK environment first." >&2
	exit 1
fi

emcmake cmake \
	-S "${repo_root}" \
	-B "${build_dir}" \
	-G Ninja \
	-DCMAKE_BUILD_TYPE="${configuration}" \
	-DVEROSCRIPT_MODULE_ROOT="${module_root}" \
	-DBUILD_TESTING=OFF \
	-DVS_ENABLE_IPO=ON \
	-DVS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY=ON \
	-DVEROSCRIPT_BUILD_VM_BACKEND=ON \
	-DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF \
	-DVEROSCRIPT_ENABLE_ASMJIT=OFF

cmake --build "${build_dir}" --target VeroScriptWebAssemblySdkSources
echo "VeroScript WebAssembly SDK: ${build_dir}/bin"
