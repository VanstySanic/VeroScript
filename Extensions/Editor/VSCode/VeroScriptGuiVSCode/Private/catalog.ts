// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { readFileSync } from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';
import { GuiSchema, readSchema, SchemaIndex } from './schema';
import { localContext, resolveSetting } from './paths';

interface CatalogEntry {
	readonly promise: Promise<SchemaIndex>;
	readonly watchers: vscode.Disposable[];
}

export class Catalogs implements vscode.Disposable {
	constructor(context: vscode.ExtensionContext, private readonly output: vscode.LogOutputChannel) {
		this.builtin = readSchema(JSON.parse(readFileSync(context.asAbsolutePath('Resources/Schemas/builtin.json'), 'utf8')));
		this.builtinIndex = new SchemaIndex([this.builtin]);
		this.configuration = vscode.workspace.onDidChangeConfiguration(event => {
			if (event.affectsConfiguration('veroscriptGui.schemaFiles')) {
				this.clear();
			}
		});
	}

	async get(uri: vscode.Uri, token: vscode.CancellationToken): Promise<SchemaIndex> {
		if (token.isCancellationRequested) {
			throw new vscode.CancellationError();
		}
		const setting = vscode.workspace.getConfiguration('veroscriptGui', uri).get<unknown>('schemaFiles', []);
		if (!Array.isArray(setting) || setting.some(value => typeof value !== 'string')) {
			throw new Error(vscode.l10n.t('schemaFiles must be an array of paths.'));
		}
		// Unsaved documents have no stable directory for custom schemas or file variables.
		if (uri.scheme !== 'file' || !setting.length) return this.builtinIndex;
		const files = setting.map(value => resolveSetting(value, uri));
		const key = `${localContext(uri).base}\0${JSON.stringify(files)}`;
		let entry = this.entries.get(key);
		if (entry) {
			this.entries.delete(key);
			this.entries.set(key, entry);
		}
		if (!entry) {
			const watchers: vscode.Disposable[] = [];
			const invalidate = () => {
				if (this.entries.get(key)?.watchers !== watchers) return;
				this.entries.delete(key);
				for (const watcher of watchers) {
					watcher.dispose();
				}
			};
			for (const file of files) {
				const literal = path.basename(file).replace(/[?*\[\]{}]/g, value => `[${value}]`);
				const watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(path.dirname(file), literal));
				watchers.push(watcher, watcher.onDidChange(invalidate), watcher.onDidCreate(invalidate), watcher.onDidDelete(invalidate));
			}
			const promise = Promise.all(files.map(async file => {
				try {
					const target = vscode.Uri.file(file);
					if ((await vscode.workspace.fs.stat(target)).size > 2 * 1024 * 1024) {
						throw new Error('schema.size');
					}
					return readSchema(JSON.parse(Buffer.from(await vscode.workspace.fs.readFile(target)).toString('utf8')));
				}
				catch (error)
				{
					this.output.error(vscode.l10n.t('Cannot load GUI schema {0}: {1}', file, error instanceof Error ? error.message : String(error)));
					return undefined;
				}
			})).then(schemas => new SchemaIndex([this.builtin, ...schemas.filter((schema): schema is GuiSchema => schema !== undefined)]));
			entry = { promise, watchers };
			this.entries.set(key, entry);
			// File-dependent settings can otherwise retain watchers for every document ever opened.
			if (this.entries.size > 32) {
				const oldest = this.entries.entries().next().value!;
				this.entries.delete(oldest[0]);
				for (const watcher of oldest[1].watchers) {
					watcher.dispose();
				}
			}
			void promise.catch(error => this.output.error(String(error)));
		}
		const result = await entry.promise;
		if (token.isCancellationRequested || this.entries.get(key) !== entry) {
			throw new vscode.CancellationError();
		}
		return result;
	}

	dispose(): void {
		this.configuration.dispose();
		this.clear();
	}

	private clear(): void {
		for (const entry of this.entries.values()) {
			for (const watcher of entry.watchers) {
				watcher.dispose();
			}
		}
		this.entries.clear();
	}

	private readonly builtin: GuiSchema;
	private readonly builtinIndex: SchemaIndex;
	private readonly configuration: vscode.Disposable;
	private readonly entries = new Map<string, CatalogEntry>();
}
