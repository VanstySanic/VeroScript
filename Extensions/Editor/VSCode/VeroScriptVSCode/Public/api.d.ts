// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import type * as vscode from 'vscode';

/** Resolved on the workspace extension host; arguments never contain a command or entry file. */
export interface VeroScriptToolchain {
	readonly executable: string;
	readonly runtimeRoot: string;
	readonly base: string;
	readonly cwd: string;
	readonly environment: Readonly<Record<string, string | undefined>>;
	readonly compileArguments: readonly string[];
	readonly executionArguments: readonly string[];
	readonly scriptArguments: readonly string[];
	readonly sourceDirectories: readonly string[];
	readonly sourceFiles: readonly string[];
}

/** Add-on API. Resolve only for an explicit execution request, never for editing or task discovery. */
export interface VeroScriptToolsApi {
	readonly apiVersion: 1;
	getToolchain(uri: vscode.Uri, sourceFile?: vscode.Uri): Promise<VeroScriptToolchain>;
}
