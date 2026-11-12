// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as path from 'node:path';
import * as vscode from 'vscode';

export function localContext(uri: vscode.Uri): { base: string; folder?: vscode.WorkspaceFolder } {
	if (uri.scheme !== 'file') {
		throw new Error(vscode.l10n.t('Save the GUI document to a file first.'));
	}
	const folder = vscode.workspace.getWorkspaceFolder(uri);
	return { base: folder?.uri.fsPath ?? path.dirname(uri.fsPath), folder };
}

export function resolveSetting(value: unknown, uri: vscode.Uri, sourceFile = uri): string {
	const { base, folder } = localContext(uri);
	const file = sourceFile.scheme === 'file' && !vscode.workspace.workspaceFolders?.some(item => item.uri.toString() === sourceFile.toString())
		? sourceFile : undefined;
	if (typeof value !== 'string' || !value || value.includes('\0')) {
		throw new Error(vscode.l10n.t('Expected a nonempty path.'));
	}
	const expanded = value.replace(/\$\{([^}]+)\}/g, (_, name: string) => {
		if (name === 'workspaceFolder' && folder) return base;
		if (name === 'file' && file) return file.fsPath;
		if (name === 'fileDirname' && file) return path.dirname(file.fsPath);
		if (name.startsWith('env:') && process.env[name.slice(4)] !== undefined) return process.env[name.slice(4)]!;
		throw new Error(vscode.l10n.t('Unresolved GUI path variable: {0}.', name));
	});
	return path.resolve(base, expanded);
}

export function fileReference(reference: string, owner: vscode.Uri): vscode.Uri | undefined {
	if (!reference || reference.includes('\0') || owner.scheme !== 'file') return undefined;
	// Opaque resource identities belong to the GUI resource provider.
	if (/^[a-zA-Z][\w+.-]*:/.test(reference) && !/^[a-zA-Z]:[/\\]/.test(reference)) return undefined;
	return vscode.Uri.file(path.resolve(path.dirname(owner.fsPath), reference));
}

export function containsPath(directory: string, file: string): boolean {
	const relative = path.relative(directory, file);
	return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
}
