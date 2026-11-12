// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import type * as vscode from 'vscode';
import type { VeroScriptToolchain } from '../Public/api';
import { compileArguments, mergedCompileOptions, processEnvironment } from './configuration';
import { locale, Services } from './context';

export async function toolchain(services: Services, uri: vscode.Uri, sourceFile?: vscode.Uri): Promise<VeroScriptToolchain> {
	const context = services.context(uri, sourceFile?.scheme === 'file' ? sourceFile.fsPath : undefined);
	const runtime = await services.runtime(context);
	const options = mergedCompileOptions(context.settings, {}, context.base, runtime.stdlib);
	const executionArguments = [`--backend=${context.settings.backend}`];
	if (context.settings.jitBackendPath) {
		executionArguments.push('--jit-backend', context.settings.jitBackendPath);
	}
	return {
		executable: runtime.cli, runtimeRoot: runtime.root, base: context.base, cwd: context.settings.cwd,
		environment: processEnvironment({ VERO_LANG: locale(), ...context.settings.env }),
		compileArguments: compileArguments(options), executionArguments, scriptArguments: [...context.settings.args],
		sourceDirectories: [context.base, ...options.importPaths, ...options.stdlibSearchPaths],
		sourceFiles: [...options.implicitImportPaths]
	};
}
