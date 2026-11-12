#!/usr/bin/env bash
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Full native Linux/macOS release. Python implements the shared release contract.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$SCRIPT_DIR/package-veroscript-all.py" "$@"
