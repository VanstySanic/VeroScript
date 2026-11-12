// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import type {
	VeroScriptEngineModule,
	VeroScriptEngineModuleOptions,
} from "./veroscript-engine.mjs";

export type { VeroScriptEngineModule, VeroScriptEngineModuleOptions };

export default function createFastVeroScriptEngine(
	options?: VeroScriptEngineModuleOptions,
): Promise<VeroScriptEngineModule>;
