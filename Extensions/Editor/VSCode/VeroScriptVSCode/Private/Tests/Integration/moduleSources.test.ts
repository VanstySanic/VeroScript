// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import * as vscode from 'vscode';
import { LanguageClient, State, StateChangeEvent } from 'vscode-languageclient/node';
import { suite, test } from 'mocha';
import { ModuleSources, ModuleSourceClient } from '../../moduleSources';

suite('VeroScript immutable module sources', () => {
	test('isolates identical module URIs and rejects missing, canceled, changed or retired snapshots', async () => {
		const sources = new ModuleSources();
		const cancellation = new vscode.CancellationTokenSource();
		const active = new vscode.CancellationTokenSource();
		const states = new vscode.EventEmitter<StateChangeEvent>();
		const native = 'veroscript-module:/dep/api';
		let fingerprint = 'snapshot-a';
		let requests = 0;
		const client = (text: string): ModuleSourceClient => ({
			state: State.Running,
			onDidChangeState: states.event,
			get initializeResult() { return { capabilities: { experimental: { veroscriptModuleEnvironment: {
				compileFingerprint: fingerprint, moduleSourceMethod: 'veroscript/moduleSource'
			} } } }; },
			async sendRequest<T>(method: string, params: unknown): Promise<T> {
				++requests;
				assert.equal(method, 'veroscript/moduleSource');
				assert.deepEqual(params, { uri: native });
				return { uri: native, text, readOnly: true, languageId: 'veroscript', compileFingerprint: fingerprint } as T;
			}
		});
		const a = sources.register('a', client('export int answer(){return 7;}\n'));
		const b = sources.register('b', client('export int answer(){return 9;}\n'));
		try {
			const first = sources.uriConverters('a');
			const second = sources.uriConverters('b');
			const uriA = first.protocol2Code(native);
			const uriB = second.protocol2Code(native);
			assert.notEqual(uriA.toString(), uriB.toString());
			assert.notEqual(sources.owner(uriA), sources.owner(uriB));
			assert.equal(first.code2Protocol(uriA), native);
			const documentA = await vscode.workspace.openTextDocument(uriA);
			const documentB = await vscode.workspace.openTextDocument(uriB);
			for (let attempt = 0; attempt < 50 && (documentA.languageId !== 'veroscript' || documentB.languageId !== 'veroscript'); ++attempt) {
				await new Promise(resolve => setTimeout(resolve, 20));
			}
			assert.equal(documentA.languageId, 'veroscript');
			assert.equal(documentB.languageId, 'veroscript');
			assert.ok(documentA.getText().includes('return 7;'));
			assert.ok(documentB.getText().includes('return 9;'));
			assert.equal(uriA.scheme, 'veroscript-module', 'content-provider documents have no writable filesystem');
			const token = cancellation.token;
			fingerprint = 'snapshot-b';
			assert.equal(sources.owner(uriA), undefined);
			await assert.rejects(sources.provideTextDocumentContent(uriA, token));
			const changed = first.protocol2Code(native);
			assert.notEqual(changed.toString(), uriA.toString());
			const escaped = 'veroscript-module:/dep/encoded%20%25%20%E6%A8%A1%E5%9D%97.vs';
			assert.equal(first.code2Protocol(first.protocol2Code(escaped)), escaped);
			const file = vscode.Uri.file(path.join(process.env.VEROSCRIPT_TEST_SESSION!, 'plain.vs'));
			assert.equal(first.protocol2Code(file.toString()).toString(), file.toString());
			assert.equal(first.code2Protocol(file), file.toString());
			cancellation.cancel();
			const canceledRequests = requests;
			await assert.rejects(sources.provideTextDocumentContent(changed, token), vscode.CancellationError);
			assert.equal(requests, canceledRequests, 'canceled reads never call the host');
			a.dispose();
			const before = requests;
			await assert.rejects(sources.provideTextDocumentContent(changed, active.token));
			assert.equal(requests, before);
		}
		finally
		{
			a.dispose();
			b.dispose();
			cancellation.dispose();
			active.dispose();
			sources.dispose();
			states.dispose();
		}
	});

	test('rejects snapshot responses canceled, replaced, changed or restarted while the host is preparing them', async () => {
		for (const mode of ['canceled', 'replaced', 'changed', 'restarted']) {
			const sources = new ModuleSources();
			const cancellation = new vscode.CancellationTokenSource();
			const states = new vscode.EventEmitter<StateChangeEvent>();
			const native = 'veroscript-module:/dep/late.vs';
			let fingerprint = 'before';
			let release!: (value: unknown) => void;
			const client: ModuleSourceClient = {
				state: State.Running,
				onDidChangeState: states.event,
				get initializeResult() { return { capabilities: { experimental: { veroscriptModuleEnvironment: {
					compileFingerprint: fingerprint, moduleSourceMethod: 'veroscript/moduleSource'
				} } } }; },
				async sendRequest<T>(): Promise<T> { return await new Promise<unknown>(resolve => { release = resolve; }) as T; }
			};
			const registration = sources.register('late', client);
			let replacement: vscode.Disposable | undefined;
			try {
				const uri = sources.uriConverters('late').protocol2Code(native);
				const pending = sources.provideTextDocumentContent(uri, cancellation.token);
				if (mode === 'canceled') {
					cancellation.cancel();
				}
				else if (mode === 'changed')
				{
					fingerprint = 'after';
				}
				else if (mode === 'restarted')
				{
					states.fire({ oldState: State.Running, newState: State.Stopped });
					states.fire({ oldState: State.Stopped, newState: State.Starting });
					states.fire({ oldState: State.Starting, newState: State.Running });
					assert.equal(sources.owner(uri), undefined);
					const restarted = sources.uriConverters('late').protocol2Code(native);
					assert.notEqual(restarted.toString(), uri.toString());
					assert.equal(sources.owner(restarted), client);
				}
				else
				{
					const replacementClient = { ...client };
					replacement = sources.register('late', replacementClient);
					registration.dispose();
					assert.equal(sources.owner(uri), undefined);
					assert.equal(sources.owner(sources.uriConverters('late').protocol2Code(native)), replacementClient,
						'retiring the old client preserves its replacement');
				}
				release({ uri: native, text: 'stale text', readOnly: true, languageId: 'veroscript', compileFingerprint: 'before' });
				await assert.rejects(pending, mode === 'canceled' ? vscode.CancellationError : Error);
			}
			finally
			{
				registration.dispose();
				replacement?.dispose();
				cancellation.dispose();
				sources.dispose();
				states.dispose();
			}
		}
	});

	test('opens native module definitions and reads the owning immutable environment through the advertised method', async function () {
		const executable = process.env.VEROSCRIPT_TEST_MODULE_LSP;
		if (!executable) {
			this.skip();
		}
		const base = path.join(process.env.VEROSCRIPT_TEST_SESSION!, 'module host 中文');
		await fs.mkdir(path.join(base, 'app'), { recursive: true });
		const sources = new ModuleSources();
		const converters = sources.uriConverters('native');
		const client = new LanguageClient('veroscript-module-native', 'VeroScript module native', {
			command: executable!, args: ['--lsp'], options: { cwd: base }
		}, { documentSelector: [], uriConverters: converters, initializationFailedHandler: () => false });
		const registration = sources.register('native', client);
		const cancellation = new vscode.CancellationTokenSource();
		try {
			await client.start();
			const uri = vscode.Uri.file(path.join(base, 'app/main.vs'));
			const text = 'import "dep"; int main(){return dep_value();}\n';
			await client.sendNotification('textDocument/didOpen', { textDocument: { uri: uri.toString(), languageId: 'veroscript', version: 1, text } });
			const definitions = await client.sendRequest<{ uri: string }[]>('textDocument/definition', {
				textDocument: { uri: uri.toString() }, position: { line: 0, character: text.indexOf('dep_value') + 2 }
			});
			assert.equal(definitions[0]?.uri, 'veroscript-module:/dep/api.vs');
			const moduleUri = converters.protocol2Code(definitions[0]!.uri);
			const document = await vscode.workspace.openTextDocument(moduleUri);
			assert.equal(document.languageId, 'veroscript');
			assert.ok(document.getText().includes('中文😀'));
			assert.ok(document.getText().includes('export int dep_value(){return 7;}'));
			// A facade can legally re-export a module owned by a transitive dependency.
			// The server grants source access after returning that navigation target.
			const facade = vscode.Uri.file(path.join(base, 'app/facade.vs'));
			const facadeText = 'import "facade"; int main(){return leaf_value();}\n';
			await client.sendNotification('textDocument/didOpen', { textDocument: { uri: facade.toString(), languageId: 'veroscript', version: 1, text: facadeText } });
			const transitive = await client.sendRequest<{ uri: string }[]>('textDocument/definition', {
				textDocument: { uri: facade.toString() }, position: { line: 0, character: facadeText.indexOf('leaf_value') + 2 }
			});
			assert.equal(transitive[0]?.uri, 'veroscript-module:/leaf/api.vs');
			const leaf = converters.protocol2Code(transitive[0]!.uri);
			assert.equal(sources.owner(leaf), client);
			assert.equal(converters.code2Protocol(leaf), transitive[0]!.uri);
			assert.ok((await vscode.workspace.openTextDocument(leaf)).getText().includes('return 13;'));
			const hidden = converters.protocol2Code('veroscript-module:/hidden/api.vs');
			await assert.rejects(sources.provideTextDocumentContent(hidden, cancellation.token));
			const missing = converters.protocol2Code('veroscript-module:/absent/api.vs');
			await assert.rejects(sources.provideTextDocumentContent(missing, cancellation.token));
			await client.stop();
			await client.start();
			const restarted = converters.protocol2Code(definitions[0]!.uri);
			assert.notEqual(restarted.toString(), moduleUri.toString(), 'the same client and fingerprint start a distinct source lifetime');
			assert.equal(sources.owner(moduleUri), undefined);
			await assert.rejects(sources.provideTextDocumentContent(moduleUri, cancellation.token));
			assert.ok((await vscode.workspace.openTextDocument(restarted)).getText().includes('export int dep_value(){return 7;}'));
		}
		finally
		{
			registration.dispose();
			await client.dispose();
			cancellation.dispose();
			sources.dispose();
		}
	});
});
