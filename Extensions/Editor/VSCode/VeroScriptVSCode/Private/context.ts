// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as path from 'node:path';
import * as vscode from 'vscode';
import { AnalysisSettings, compileFields, CompileOptions, containsPath, readAnalysisSettings, readRuntimeSettings, readSettings, RuntimeSettings, Settings } from './configuration';
import { Runtime, RuntimeResolver } from './runtime';

export interface SourceContext {
	readonly uri: vscode.Uri;
	readonly folder: vscode.WorkspaceFolder | undefined;
	readonly base: string;
}

export interface RuntimeContext extends SourceContext {
	readonly settings: RuntimeSettings;
}

export interface AnalysisContext extends RuntimeContext {
	readonly settings: AnalysisSettings;
	readonly compileOverrides: readonly (typeof compileFields[number])[];
}

export interface ResourceContext extends RuntimeContext {
	readonly settings: Settings;
}

export function sourceContext(uri: vscode.Uri): SourceContext {
	if (uri.scheme !== 'file') {
		throw new Error(vscode.l10n.t('Save the VeroScript document to a file first.'));
	}
	const folder = vscode.workspace.getWorkspaceFolder(uri);
	const base = folder?.uri.fsPath ?? path.dirname(uri.fsPath);
	return { uri, folder, base };
}

export function resourceContext(uri: vscode.Uri, file?: string): ResourceContext {
	const context = sourceContext(uri);
	const sourceFile = file ?? (context.folder?.uri.toString() === uri.toString() ? undefined : uri.fsPath);
	return { ...context, settings: readSettings(vscode.workspace.getConfiguration('veroscript', uri), context.base, sourceFile, context.folder !== undefined) };
}

export function targetUri(argument?: unknown): vscode.Uri {
	const uri = argument instanceof vscode.Uri ? argument : vscode.window.activeTextEditor?.document.uri;
	if (!uri || uri.scheme !== 'file' || path.extname(uri.fsPath).toLowerCase() !== '.vs') {
		throw new Error(vscode.l10n.t('Open or select a saved .vs file first.'));
	}
	return uri;
}

export function locale(): string {
	try {
		return Intl.getCanonicalLocales(vscode.env.language)[0] ?? 'en-US';
	}
	catch
	{
		return 'en-US';
	}
}

export function participating(document: vscode.TextDocument, context: SourceContext, options: CompileOptions): boolean {
	if (document.uri.scheme !== 'file' || document.languageId !== 'veroscript') return false;
	const file = document.uri.fsPath;
	return file === context.uri.fsPath || containsPath(context.base, file)
		|| [...options.importPaths, ...options.stdlibSearchPaths].some(directory => containsPath(directory, file))
		|| options.implicitImportPaths.some(imported => path.relative(imported, file) === '');
}

export async function saveDocuments(context: SourceContext, options: CompileOptions): Promise<boolean> {
	const dirty = vscode.workspace.textDocuments.filter(document => document.isDirty && participating(document, context, options));
	if (dirty.length === 0) return true;
	const save = vscode.l10n.t('Save and Continue');
	if (await vscode.window.showWarningMessage(vscode.l10n.t('Save {0} VeroScript document(s) before starting?', dirty.length), { modal: true }, save) !== save) return false;
	for (const document of dirty) {
		if (!await document.save()) return false;
	}
	return !vscode.workspace.textDocuments.some(document => document.isDirty && participating(document, context, options));
}

export class Services {
	constructor(readonly output: vscode.LogOutputChannel) {}

	context(uri: vscode.Uri, file?: string): ResourceContext {
		return resourceContext(uri, file);
	}

	analysisContext(uri: vscode.Uri): AnalysisContext {
		const context = sourceContext(uri);
		const configuration = vscode.workspace.getConfiguration('veroscript', uri);
		const settings = readAnalysisSettings(configuration, context.base, context.folder !== undefined);
		const compileOverrides = settings.executablePath ? compileFields.filter(key => {
			const entry = configuration.inspect(key);
			return entry !== undefined && [
				entry.globalValue, entry.workspaceValue, entry.workspaceFolderValue,
				entry.globalLanguageValue, entry.workspaceLanguageValue, entry.workspaceFolderLanguageValue
			].some(value => value !== undefined);
		}) : [];
		return { ...context, settings, compileOverrides };
	}

	runtimeContext(uri: vscode.Uri): RuntimeContext {
		const context = sourceContext(uri);
		return { ...context, settings: readRuntimeSettings(vscode.workspace.getConfiguration('veroscript', uri), context.base, context.folder !== undefined) };
	}

	async runtime(context: RuntimeContext): Promise<Runtime> {
		return this.resolver.resolve(context.settings.runtimePath);
	}

	report(error: unknown): void {
		const message = error instanceof Error ? error.message : String(error);
		this.output.error(message);
		void vscode.window.showErrorMessage(vscode.l10n.t('VeroScript: {0}', message));
	}

	readonly resolver = new RuntimeResolver();
}
