// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import { execFile, spawn, ChildProcess } from 'node:child_process';
import * as net from 'node:net';
import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import * as vscode from 'vscode';
import { suite, test, suiteSetup, suiteTeardown } from 'mocha';
import type { InitializeResult } from 'vscode-languageclient/node';
import { processEnvironment } from '../../configuration';
import { participating, Services, sourceContext } from '../../context';
import { VeroDebugger } from '../../debugger';
import { VeroTasks } from '../../tasks';
import { LanguageServers } from '../../languageClient';
import { continueNamespaceCompletion, presentCompletion, presentHover } from '../../presentation';
import { extensionUnderTest } from './host';
import type { VeroScriptToolsApi } from '../../../Public/api';

interface Client {
	readonly initializeResult: InitializeResult | undefined;
	dispose(): Promise<void>;
	sendRequest<T>(method: string, params: unknown): Promise<T>;
	sendNotification(method: string, params?: unknown): Promise<void>;
	onDidChangeState: vscode.Event<{ oldState: number; newState: number }>;
	getFeature(method: 'textDocument/didClose'): { callback(document: vscode.TextDocument): Promise<void> };
	getFeature(method: 'textDocument/didOpen'): { readonly openDocuments: Iterable<vscode.TextDocument> };
	getFeature(method: 'textDocument/semanticTokens'): { getProvider(document: vscode.TextDocument): object | undefined };
}

interface ExtensionApi extends VeroScriptToolsApi {
	ready(uri?: vscode.Uri): Promise<Client | undefined>;
	snapshot(): { key: string; state: string; runtime: string }[];
}

interface NativeCompletionResult {
	items: {
		label: string;
		filterText: string;
		sortText: string;
		preselect?: boolean;
		insertText: string;
		textEdit?: {
			newText: string;
			range: { start: { line: number; character: number }; end: { line: number; character: number } };
		};
	}[];
}

interface DapMessage {
	type: string;
	event?: string;
	command?: string;
	success?: boolean;
	body?: { reason?: string; output?: string };
}

interface DapVariable {
	name: string;
	value: string;
	variablesReference: number;
	presentationHint?: { attributes?: readonly string[] };
	memoryReference?: string;
	veroMemory?: Record<string, string>;
}

async function eventually<T>(read: () => T | PromiseLike<T>, accept: (value: T) => boolean, label: string, timeout = 15000): Promise<T> {
	const deadline = Date.now() + timeout;
	while (Date.now() < deadline) {
		const value = await read();
		if (accept(value)) return value;
		await new Promise(resolve => setTimeout(resolve, 100));
	}
	throw new Error(`Timed out: ${label}`);
}

async function nativeSemantic<T>(client: Client, method: string, params: unknown): Promise<T> {
	// Raw protocol requests bypass the SDK's ContentModified cancellation path.
	// Retry only stale snapshots; assertions still validate the complete result.
	const result = await eventually(async () => {
		try {
			return { value: await client.sendRequest<T>(method, params) };
		}
		catch (error)
		{
			if (error && typeof error === 'object' && 'code' in error && error.code === -32801) return undefined;
			throw error;
		}
	}, value => value !== undefined, method);
	assert.ok(result);
	return result.value;
}

const sessionRoot = process.env.VEROSCRIPT_TEST_SESSION!;
const rootA = path.join(sessionRoot, 'workspace A');
const rootB = path.join(sessionRoot, 'workspace B');
const program = vscode.Uri.file(path.join(rootA, 'hello world 中文.vs'));
let api: ExtensionApi;
let original: string;
const documents: vscode.TextDocument[] = [];
const resources: vscode.Disposable[] = [];
const messages: DapMessage[] = [];
let externalHost: ChildProcess | undefined;

suite('VeroScript real Extension Host', () => {
	suiteSetup(async () => {
		const extension = await extensionUnderTest<ExtensionApi>();
		api = await extension.activate();
		const document = await vscode.workspace.openTextDocument(program);
		await vscode.window.showTextDocument(document);
		documents.push(document);
		original = document.getText();
		await eventually(() => api.snapshot(), states => states.length === 1 && states[0]?.state === 'Running', 'first LSP running');
		resources.push(vscode.debug.registerDebugAdapterTrackerFactory('veroscript', { createDebugAdapterTracker: () => ({
			onDidSendMessage: (message: DapMessage) => messages.push(message)
		}) }));
	});

	suiteTeardown(async () => {
		if (vscode.debug.activeDebugSession?.type === 'veroscript') {
			await vscode.debug.stopDebugging();
		}
		externalHost?.kill();
		for (const resource of resources) {
			resource.dispose();
		}
	});

	test('exposes a scoped toolchain for add-ons without starting another language server', async () => {
		const before = api.snapshot();
		const toolchain = await api.getToolchain(program, vscode.Uri.file(path.join(rootA, 'page.vswml')));
		assert.equal(api.apiVersion, 1);
		assert.equal(path.relative(toolchain.base, rootA), '');
		assert.ok(path.isAbsolute(toolchain.executable));
		assert.ok(toolchain.compileArguments.includes('--stdlib-path'));
		assert.ok(!toolchain.compileArguments.includes('--check'));
		assert.ok(!toolchain.compileArguments.some(argument => argument.startsWith('--backend=')));
		assert.deepEqual(api.snapshot().map(server => server.key), before.map(server => server.key));
	});

	test('registers editing resources and every command', async () => {
		assert.equal(documents[0]?.languageId, 'veroscript');
		const commands = await vscode.commands.getCommands(true);
		for (const name of ['runFile', 'checkFile', 'debugFile', 'attachToHost', 'attachToDomain', 'selectRuntime', 'restartLanguageServer', 'showOutput', 'showEnvironment']) {
			assert.ok(commands.includes(`veroscript.${name}`), name);
		}
		await vscode.tasks.fetchTasks({ type: 'veroscript' });
	});

	test('opens the branded extension Details page with its shipped README', async () => {
		const extension = vscode.extensions.getExtension(process.env.VEROSCRIPT_TEST_EXTENSION_ID!)!;
		assert.equal(extension.packageJSON.publisher, 'VanstySanic');
		const readme = await fs.readFile(path.join(extension.extensionPath,
			process.env.VEROSCRIPT_TEST_VSIX ? 'readme.md' : 'README.md'), 'utf8');
		assert.ok(readme.startsWith('# VeroScript for Visual Studio Code'));
		assert.ok(readme.includes('Quick start') && readme.includes('Select Runtime'));
		try {
			await vscode.commands.executeCommand('extension.open', extension.id);
		}
		finally
		{
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
		}
	});

	test('launches and reconfigures an explicit stdio server without a Runtime', async () => {
		const before = await api.ready(program);
		assert.ok(before);
		const runtime = process.env.VEROSCRIPT_TEST_RUNTIME!;
		const server = path.join(runtime, process.platform === 'win32' ? 'veroscript-lsp.exe' : 'veroscript-lsp');
		const host = path.join(sessionRoot, process.platform === 'win32' ? 'analysis host.exe' : 'analysis host');
		await fs.copyFile(process.env.VEROSCRIPT_TEST_NODE!, host);
		if (process.platform !== 'win32')
		{
			await fs.chmod(host, 0o755);
		}
		const launcher = path.join(sessionRoot, 'stdio host.cjs');
		await fs.writeFile(launcher, `
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const [server, record, ...args] = process.argv.slice(2);
fs.writeFileSync(record + '.argv.json', JSON.stringify(args));
const child = spawn(server, [], { stdio: ['pipe', 'pipe', 'inherit'], windowsHide: true });
process.stdin.on('data', bytes => fs.appendFileSync(record + '.requests', bytes));
child.stdout.on('data', bytes => fs.appendFileSync(record + '.responses', bytes));
process.stdin.pipe(child.stdin);
child.stdout.pipe(process.stdout);
child.on('exit', code => {
	fs.writeFileSync(record + '.exit', String(code));
	process.stdin.pause();
	process.exitCode = code ?? 1;
});
`);
		const configuration = vscode.workspace.getConfiguration('veroscript', program);
		const target = vscode.ConfigurationTarget.WorkspaceFolder;
		const keys = ['runtimePath', 'lsp.executablePath', 'lsp.arguments'] as const;
		const previous = keys.map(key => configuration.inspect(key)?.workspaceFolderValue);
		const first = path.join(sessionRoot, 'first server');
		const second = path.join(sessionRoot, 'second server');
		const argv = ['--project', 'Game Project.uproject', '', '${literal}'];
		try {
			await configuration.update('lsp.arguments', [launcher, server, first, ...argv], target);
			await configuration.update('lsp.executablePath', host, target);
			await configuration.update('runtimePath', path.join(sessionRoot, 'missing Runtime'), target);
			await vscode.commands.executeCommand('veroscript.restartLanguageServer');
			const client = await api.ready(program);
			assert.ok(client?.initializeResult && client !== before);
			assert.equal(api.snapshot()[0]?.runtime, host);
			assert.deepEqual(JSON.parse(await fs.readFile(first + '.argv.json', 'utf8')), argv);
			const result = await client.sendRequest<unknown>('textDocument/hover', {
				textDocument: { uri: program.toString() }, position: { line: 7, character: 19 }
			});
			assert.match(JSON.stringify(result), /add/);

			await configuration.update('lsp.arguments', [launcher, server, second, ...argv, 'new argument'], target);
			await eventually(() => api.ready(program), value => Boolean(value?.initializeResult && value !== client),
				'arguments change restarts the explicit server');
			assert.deepEqual(JSON.parse(await fs.readFile(second + '.argv.json', 'utf8')), [...argv, 'new argument']);
			assert.equal(await fs.readFile(first + '.exit', 'utf8'), '0');

			const silent = path.join(sessionRoot, 'silent host.cjs');
			const attempted = path.join(sessionRoot, 'silent host started');
			await fs.writeFile(silent, "require('node:fs').writeFileSync(process.argv[2], 'started'); setInterval(() => {}, 1000);");
			await configuration.update('lsp.arguments', [silent, attempted], target);
			await eventually(() => api.ready(program), value => value === undefined, 'silent server startup times out', 20000);
			assert.equal(await fs.readFile(attempted, 'utf8'), 'started');
		}
		finally
		{
			for (const [index, key] of keys.entries())
			{
				await configuration.update(key, previous[index], target);
			}
			await vscode.commands.executeCommand('veroscript.restartLanguageServer');
		}
		assert.equal(await fs.readFile(second + '.exit', 'utf8'), '0');
		assert.equal(path.relative(runtime, api.snapshot()[0]?.runtime ?? ''), '');
	});

	test('coalesces document event bursts while retaining resets and work queued during a failed pass', async () => {
		const errors: string[] = [];
		const output = { error: (message: string) => { errors.push(message); } } as unknown as vscode.LogOutputChannel;
		const status = vscode.window.createStatusBarItem();
		const manager = new LanguageServers(new Services(output), status);
		const worker = manager as unknown as { schedule(reset?: boolean): void; reconcile(): Promise<void>; stopAll(): Promise<void> };
		let passes = 0;
		let resets = 0;
		worker.reconcile = async () => { ++passes; };
		worker.stopAll = async () => { ++resets; };
		try {
			for (let index = 0; index < 100; ++index) {
				worker.schedule();
			}
			await manager.ready();
			assert.equal(passes, 1, 'a burst requires one latest-state pass');
			let release!: () => void;
			let entered!: () => void;
			const blocked = new Promise<void>(resolve => { release = resolve; });
			const started = new Promise<void>(resolve => { entered = resolve; });
			worker.reconcile = async () => {
				++passes;
				if (passes === 2) {
					entered();
					await blocked;
					throw new Error('fixture reconciliation failure');
				}
			};
			worker.schedule();
			await started;
			for (let index = 0; index < 100; ++index) {
				worker.schedule(index % 10 === 0);
			}
			release();
			await manager.ready();
			assert.equal(passes, 3, 'work arriving during a failed pass is retried against current state');
			assert.equal(resets, 1, 'batched explicit resets are retained');
			assert.deepEqual(errors, ['Error: fixture reconciliation failure']);
		}
		finally
		{
			await manager.dispose();
			status.dispose();
		}
	});

	test('provides hover, definition, symbols, completion and full/delta semantic tokens', async () => {
		const document = documents[0]!;
		const position = document.positionAt(original.lastIndexOf('add(') + 1);
		const hover = await eventually(() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', program, position), value => !!value?.length, 'hover');
		assert.ok(hover?.[0]?.contents.length);
		assert.ok(hover?.[0]?.contents.some(content => content instanceof vscode.MarkdownString && content.value.includes('```veroscript')));
		const definitions = await vscode.commands.executeCommand<(vscode.Location | vscode.LocationLink)[]>('vscode.executeDefinitionProvider', program, position);
		assert.ok(definitions?.length);
		const symbols = await vscode.commands.executeCommand<vscode.DocumentSymbol[]>('vscode.executeDocumentSymbolProvider', program);
		assert.ok(symbols?.some(symbol => symbol.name === 'add'));
		const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', program, position);
		assert.ok(completion?.items.length);
		const references = await vscode.commands.executeCommand<vscode.Location[]>('vscode.executeReferenceProvider', program, position);
		assert.ok(references?.length);
		const rename = await vscode.commands.executeCommand<vscode.WorkspaceEdit>('vscode.executeDocumentRenameProvider', program, position, 'sum');
		assert.ok(rename?.size);
		const signature = await vscode.commands.executeCommand<vscode.SignatureHelp>('vscode.executeSignatureHelpProvider', program, document.positionAt(original.lastIndexOf('add(') + 5));
		assert.ok(signature?.signatures.length);
		const folding = await vscode.commands.executeCommand<vscode.FoldingRange[]>('vscode.executeFoldingRangeProvider', program);
		assert.ok(folding?.length);
		const tokens = await vscode.commands.executeCommand<vscode.SemanticTokens>('vscode.provideDocumentSemanticTokens', program);
		assert.ok(tokens?.data.length);
		const client = await api.ready(program);
		assert.ok(client);
		const provider = client.getFeature('textDocument/semanticTokens').getProvider(document);
		assert.ok(provider);
		const full = await nativeSemantic<{ resultId: string; data: number[] }>(client, 'textDocument/semanticTokens/full', { textDocument: { uri: program.toString() } });
		assert.ok(full.data.length > 0 && full.resultId);
		const delta = await nativeSemantic<{ edits?: unknown[]; data?: number[] }>(client, 'textDocument/semanticTokens/full/delta', { textDocument: { uri: program.toString() }, previousResultId: full.resultId });
		if (delta.data) {
			assert.deepEqual(delta.data, full.data, 'a valid full fallback preserves unchanged tokens');
		}
		else
		{
			assert.deepEqual(delta.edits, []);
		}
		const unrelated = vscode.Uri.file(path.join(rootA, 'notes.txt'));
		await fs.writeFile(unrelated.fsPath, 'Unrelated document.\n');
		await vscode.workspace.openTextDocument(unrelated);
		await api.ready(program);
		assert.strictEqual(client.getFeature('textDocument/semanticTokens').getProvider(document), provider, 'ordinary requests and unrelated documents preserve semantic providers');
	});

	test('preserves readonly field modifiers through the native semantic provider and unsaved edits', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'readonly fields.vs'));
		const source = 'class BaseSettings {\npublic:\n'
			+ '    const int base_value;\n    int third;\n'
			+ '    BaseSettings(int base_value) : base_value(base_value), third(0) {}\n'
			+ '    int read() const { return base_value + third; }\n'
			+ '    int shadow() const { int base_value = 7; return base_value + this.base_value; }\n};\n'
			+ 'class DerivedSettings : public BaseSettings {\npublic:\n'
			+ '    DerivedSettings() : BaseSettings(1) {}\n'
			+ '    int readInherited() const { return base_value; }\n};\n';
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		try {
			const client = await api.ready(uri);
			assert.ok(client);
			const legend = client.initializeResult?.capabilities.semanticTokensProvider?.legend;
			assert.ok(legend);
			const property = legend.tokenTypes.indexOf('property');
			const variable = legend.tokenTypes.indexOf('variable');
			const readonlyIndex = legend.tokenModifiers.indexOf('readonly');
			assert.ok(property >= 0 && variable >= 0 && readonlyIndex >= 0);
			const readonlyMask = 1 << readonlyIndex;
			const expected: [number, number][] = [
				[property, readonlyMask], [variable, 0], [property, readonlyMask], [variable, 0],
				[property, readonlyMask], [variable, 0], [variable, 0], [property, readonlyMask],
				[property, readonlyMask]
			];
			const check = (data: readonly number[] | Uint32Array, hasReadonly: boolean) => {
				assert.equal(data.length % 5, 0);
				const tokens = new Map<number, [number, number, number]>();
				let line = 0;
				let character = 0;
				for (let index = 0; index < data.length; index += 5) {
					const lineDelta = data[index]!;
					line += lineDelta;
					character = lineDelta === 0 ? character + data[index + 1]! : data[index + 1]!;
					tokens.set(document.offsetAt(new vscode.Position(line, character)), [data[index + 2]!, data[index + 3]!, data[index + 4]!]);
				}
				const occurrences = [...document.getText().matchAll(/\bbase_value\b/g)];
				assert.equal(occurrences.length, expected.length);
				for (let index = 0; index < expected.length; ++index) {
					const [type, modifiers] = expected[index]!;
					assert.deepEqual(tokens.get(occurrences[index]!.index), [10, type, hasReadonly ? modifiers : 0], `base_value occurrence ${index}`);
				}
			};
			const full = await nativeSemantic<{ data: number[] }>(client, 'textDocument/semanticTokens/full', { textDocument: { uri: uri.toString() } });
			check(full.data, true);
			// Automatic editor requests can supersede a command during document edits.
			const readTokens = () => eventually(async () => {
				try {
					return await vscode.commands.executeCommand<vscode.SemanticTokens>('vscode.provideDocumentSemanticTokens', uri);
				}
				catch (error)
				{
					if (error instanceof vscode.CancellationError || (error instanceof Error && error.name === 'Canceled')) return undefined;
					throw error;
				}
			}, value => value !== undefined, 'readonly semantic tokens');
			const presented = await readTokens();
			assert.ok(presented);
			assert.deepEqual(Array.from(presented.data), full.data);
			for (const text of [source.replace('const int base_value;', 'int base_value;'), source]) {
				const edit = new vscode.WorkspaceEdit();
				edit.replace(uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), text);
				assert.ok(await vscode.workspace.applyEdit(edit));
				const tokens = await readTokens();
				assert.ok(tokens);
				check(tokens.data, text === source);
			}
		}
		finally
		{
			await document.save();
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await fs.unlink(uri.fsPath);
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('preserves native shared and handle constructor help, member completion and hover content', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'shared handles.vs'));
		await fs.writeFile(uri.fsPath, 'void main() {}\n');
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		try {
			const client = await api.ready(uri);
			assert.ok(client);
			for (const name of ['handle', 'shared', 'std::handle', 'std::shared']) {
				const source = `void main() { ${name}<int> owner = ${name}<int>(7); print(owner.value); }\n`;
				const edit = new vscode.WorkspaceEdit();
				edit.replace(uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), source);
				assert.ok(await vscode.workspace.applyEdit(edit));
				const call = `${name}<int>(`;
				const signature = await vscode.commands.executeCommand<vscode.SignatureHelp>('vscode.executeSignatureHelpProvider', uri, document.positionAt(source.indexOf(call) + call.length));
				assert.ok(signature?.signatures.some(item => item.parameters.length === 1 && item.label.includes('int')), name);
				const member = document.positionAt(source.indexOf('owner.value') + 'owner.'.length);
				const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, member, undefined, 20);
				assert.ok(completion?.items.some(item => item.label === 'value'), name);
				const native: { contents: { value: string } } = await client.sendRequest('textDocument/hover', { textDocument: { uri: uri.toString() }, position: member });
				assert.ok(native.contents.value.includes('```veroscript\nint\n```'), `${name}: member hover specializes T to int`);
				const body = native.contents.value.slice(native.contents.value.indexOf('\n\n'));
				const hover = await vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, member);
				assert.ok(hover?.some(item => item.contents.some(content => content instanceof vscode.MarkdownString && content.value.endsWith(body))), name);
			}
		}
		finally
		{
			await document.save();
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await fs.unlink(uri.fsPath);
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('supports memory types, generic call help and snippets with the default Runtime', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'memory 中文.vs'));
		const source = 'void main() {\n    std::memory::buffer bytes = new std::memory::buffer(8);\n'
			+ '    std::memory::write<uint32>(bytes, 0, 42u);\n'
			+ '    auto value = std::memory::read<uint32>(bytes, 0);\n'
			+ '    assert(value == 42u);\n}\n';
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		try {
			assert.ok(await api.ready(uri));
			const position = (text: string) => document.positionAt(source.indexOf(text) + text.length);
			const hover = await vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, position('std::memory::buf'));
			assert.ok(hover?.some(item => item.contents.some(content => content instanceof vscode.MarkdownString && content.value.startsWith('**class** ` std::memory::buffer `'))));
			const constructor = await vscode.commands.executeCommand<vscode.SignatureHelp>('vscode.executeSignatureHelpProvider', uri, position('std::memory::buffer('));
			assert.ok(constructor?.signatures.some(signature => signature.label === 'std::memory::buffer(int64)'));
			const signature = await vscode.commands.executeCommand<vscode.SignatureHelp>('vscode.executeSignatureHelpProvider', uri, position('std::memory::read<uint32>(bytes, '));
			assert.equal(signature?.activeParameter, 1);
			assert.ok(signature?.signatures.some(item => item.label.includes('uint32 std::memory::read<uint32>')));
			const tokens = await vscode.commands.executeCommand<vscode.SemanticTokens>('vscode.provideDocumentSemanticTokens', uri);
			assert.ok(tokens?.data.length);
			for (const command of ['veroscript.checkFile', 'veroscript.runFile']) {
				let result: vscode.TaskProcessEndEvent | undefined;
				const listener = vscode.tasks.onDidEndTaskProcess(event => {
					if (event.execution.task.definition.program === uri.fsPath) {
						result = event;
					}
				});
				let execution: vscode.TaskExecution | undefined;
				try {
					execution = await vscode.commands.executeCommand<vscode.TaskExecution>(command, uri);
					assert.ok(execution);
					await eventually(() => result, event => event !== undefined, command);
					assert.equal(result?.exitCode, 0);
				}
				finally
				{
					if (!result) {
						execution?.terminate();
					}
					listener.dispose();
				}
			}
			const partial = 'void main() { std::memory::re; }\n';
			const edit = new vscode.WorkspaceEdit();
			edit.replace(uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), partial);
			assert.ok(await vscode.workspace.applyEdit(edit));
			const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, document.positionAt(partial.indexOf('::re') + 4), undefined, 20);
			const read = completion?.items.find(item => (typeof item.label === 'string' ? item.label : item.label.label) === 'read');
			assert.ok(read?.insertText instanceof vscode.SnippetString, JSON.stringify(completion?.items));
			assert.equal(read.insertText.value, 'read<${1:ValueType}>(${0})');
		}
		finally
		{
			await document.save();
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await fs.unlink(uri.fsPath);
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('reloads annotation name policies and preserves native multiline generic signature parameters', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'annotation policy.vs'));
		const catalog = path.join(rootA, 'annotation policy.json');
		const source = '@hOSTwIDGET(Label="Card",Enabled=true) class Card{};\n@HostWidget(Label="Card",Enabled=true) class Canonical{};\n@Options(values=1,label="Card",enabled=true) class Legacy{};\nvoid main(){}\n';
		const settings = vscode.workspace.getConfiguration('veroscript', uri);
		const previous = settings.inspect<string[]>('bindingCatalogPaths')?.workspaceFolderValue;
		const write = (caseSensitive: boolean) => fs.writeFile(catalog, JSON.stringify({ version: 1, annotations: [
			{ name: '@HostWidget', caseSensitive, documentation: `Host widget documentation. policy=${caseSensitive}`, arguments: [
				{ name: 'Label', detail: 'string' }, { name: 'Enabled', detail: 'bool' }
			] },
			{ name: '@Options', detail: 'annotation (\r\nmap<string,array<int>> values, string label="a,b", bool enabled\r\n)' }
		] }));
		await write(false);
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		let lifecycle: vscode.Disposable | undefined;
		try {
			await settings.update('bindingCatalogPaths', [catalog], vscode.ConfigurationTarget.WorkspaceFolder);
			const client = await api.ready(uri);
			assert.ok(client);
			const states: number[] = [];
			lifecycle = client.onDidChangeState(event => states.push(event.newState));
			const hover = async (name: string) => {
				const values = await vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, document.positionAt(source.indexOf(name) + 3));
				return values?.flatMap(value => value.contents).map(value => typeof value === 'string' ? value : value.value).join('\n') ?? '';
			};
			const signature = () => vscode.commands.executeCommand<vscode.SignatureHelp>('vscode.executeSignatureHelpProvider', uri, document.positionAt(source.indexOf('Enabled') + 2));
			for (const [index, caseSensitive] of [false, true, false].entries()) {
				if (index !== 0) {
					// Change the existing file, without replacing the setting or server.
					await write(caseSensitive);
				}
				const expected = !caseSensitive;
				// A positive revision marker distinguishes a reload from an empty or
				// canceled provider result before checking the negative name match.
				await eventually(() => hover('@HostWidget'), value => value.includes(`policy=${caseSensitive}`), 'annotation catalog policy revision');
				await eventually(() => hover('hOSTwIDGET'), value => value.split('Host widget documentation.').length - 1 === (expected ? 1 : 0), 'annotation hover name policy');
				const help = await eventually(signature, value => Boolean(value?.signatures.length) === expected, 'annotation signature name policy');
				if (expected) {
					assert.equal(help!.signatures[0]!.label, '@HostWidget(Label: string, Enabled: bool)');
					assert.deepEqual(help!.signatures[0]!.parameters.map(parameter => parameter.label), ['Label', 'Enabled']);
					assert.equal(help!.activeParameter, 1);
				}
				assert.strictEqual(await api.ready(uri), client, 'catalog edits retain the owning client');
				assert.deepEqual(states, [], 'catalog edits do not restart the server');
			}
			const legacy = await vscode.commands.executeCommand<vscode.SignatureHelp>('vscode.executeSignatureHelpProvider', uri, document.positionAt(source.indexOf('enabled=true') + 2));
			assert.ok(legacy?.signatures.length);
			assert.deepEqual(legacy.signatures[0]!.parameters.map(parameter => parameter.label), [
				'map<string,array<int>> values', 'string label="a,b"', 'bool enabled'
			]);
			assert.equal(legacy.activeParameter, 2);
		}
		finally
		{
			lifecycle?.dispose();
			await settings.update('bindingCatalogPaths', previous, vscode.ConfigurationTarget.WorkspaceFolder);
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
		}
	});

	test('preserves inferred function captures through SDK hover, completion, signatures and definitions', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'function capture 中文.vs'));
		const source = [
			'namespace CaptureFixtures', '{',
			'    int create(int value)', '    {', '        return value;', '    }', '}',
			'void main()', '{',
			'    auto holder = [callback = CaptureFixtures::create](int value) -> int', '    {',
			'        return len("😀") > 0 ? callback(value) : 0;', '    };',
			'    print(holder(42));', '}', ''
		].join('\n');
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		try {
			await api.ready(uri);
			const call = source.indexOf('callback(value)');
			const position = document.positionAt(call + 3);
			const hovers = await eventually(
				() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, position),
				values => values?.some(hover => hover.contents.some(content =>
					(typeof content === 'string' ? content : content.value).includes('const fn(int)->int'))) === true,
				'captured function hover');
			assert.ok(hovers?.length);
			const candidates = await eventually(
				() => vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, position),
				result => result?.items.some(item => item.label === 'callback' && item.detail === 'const fn(int)->int') === true,
				'captured function completion');
			const candidate = candidates!.items.find(item => item.label === 'callback')!;
			assert.equal(candidate.insertText, 'callback');
			const range = candidate.range instanceof vscode.Range ? candidate.range : candidate.range?.replacing;
			assert.deepEqual(range, new vscode.Range(document.positionAt(call), document.positionAt(call + 'callback'.length)));
			const signature = await eventually(
				() => vscode.commands.executeCommand<vscode.SignatureHelp>('vscode.executeSignatureHelpProvider', uri,
					document.positionAt(call + 'callback('.length)),
				value => value?.signatures.some(item => item.label === 'fn(int)->int') === true,
				'captured function signature');
			assert.equal(signature!.signatures[0]!.parameters.length, 1);
			assert.equal(signature!.activeParameter, 0);
			const definitions = await vscode.commands.executeCommand<(vscode.Location | vscode.LocationLink)[]>(
				'vscode.executeDefinitionProvider', uri, position);
			assert.equal(definitions?.length, 1);
			const definition = definitions![0]!;
			const target = 'targetUri' in definition ? definition.targetUri : definition.uri;
			const targetRange = 'targetUri' in definition ? definition.targetSelectionRange ?? definition.targetRange : definition.range;
			assert.equal(target.toString(), uri.toString());
			assert.deepEqual(targetRange.start, document.positionAt(source.indexOf('callback =')));
		}
		finally
		{
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
		}
	});

	test('preserves nested static generic receivers through SDK hover and completion', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'nested receiver 中文.vs'));
		const receivers = [
			'ReceiverStore<handle<ReceiverValue>>::child.value.',
			'ReceiverStore<array<ReceiverValue>>::child[0].',
			'ReceiverNest<int>::Item::child.'
		];
		const source = [
			'class ReceiverValue { public: const int value; int read() { return value; } };',
			'class ReceiverStore<T> { public: static T child = T(); };',
			'struct ReceiverNest<T> { struct Item { static ReceiverValue child; }; };',
			'void inspect() {', ...receivers.map(receiver => `    ${receiver}value;`), '}', ''
		].join('\n');
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		try {
			assert.ok(await api.ready(uri));
			for (const receiver of receivers) {
				const offset = source.indexOf(receiver) + receiver.length;
				const candidates = await eventually(
					() => vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri,
						document.positionAt(offset)),
					result => result?.items.some(item => item.label === 'value') === true
						&& result.items.some(item => item.label === 'read'),
					`nested receiver completion: ${receiver}`);
				assert.ok(candidates?.items.length);
				const hovers = await eventually(
					() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri,
						document.positionAt(offset + 2)),
					result => result?.some(hover => hover.contents.some(content =>
						(typeof content === 'string' ? content : content.value).includes('const int'))) === true,
					`nested receiver hover: ${receiver}`);
				assert.ok(hovers?.length);
			}
		}
		finally
		{
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
		}
	});

	test('refreshes same-name member hover after changing its receiver in an incomplete buffer', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'changing receiver.vs'));
		const source = [
			'class HoverFirst', '{', 'public:', '    int value;', '};',
			'class HoverSecond', '{', 'public:', '    string value;', '};',
			'void main()', '{', '    auto receiver = HoverFirst();', '    receiver.value;', '}', ''
		].join('\n');
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		const editor = await vscode.window.showTextDocument(document);
		try {
			const client = await api.ready(uri);
			const hover = async () => {
				const values = await vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri,
					document.positionAt(document.getText().indexOf('receiver.value') + 'receiver.'.length + 2));
				return values?.flatMap(value => value.contents.map(content => typeof content === 'string' ? content : content.value)).join('\n') ?? '';
			};
			await eventually(hover, value => value.includes('int') && value.includes('HoverFirst'), 'initial receiver hover');
			const changed = source.replace('= HoverFirst()', '= HoverSecond()') + 'void unfinished( {\n';
			assert.ok(await editor.edit(edit => edit.replace(new vscode.Range(0, 0, document.lineCount, 0), changed)));
			const updated = await eventually(hover, value => value.includes('string') && value.includes('HoverSecond'), 'live receiver hover');
			assert.ok(!updated.includes('HoverFirst'));
			assert.strictEqual(await api.ready(uri), client, 'buffer recovery retains the owning client');
		}
		finally
		{
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
		}
	});

	test('applies precise organize-import edits while preserving CRLF, comments, inactive imports and raw strings', async () => {
		const dependency = path.join(rootA, 'organize dependency.vs');
		const uri = vscode.Uri.file(path.join(rootA, 'organize imports.vs'));
		const imported = 'import "organize dependency.vs";';
		const prefix = `/* keep */\r\n${imported}\r\n`;
		const suffix = `#if 0\r\n${imported}\r\n${imported}\r\n#endif\r\nvoid main(){string text=r"\r\nimport module;\r\nimport module;\r\n";print(text);}\r\n`;
		const source = prefix + imported + '\r\n' + suffix;
		await fs.writeFile(dependency, 'export int importedValue(){return 7;}\n');
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		try {
			await api.ready(uri);
			const actions = () => vscode.commands.executeCommand<vscode.CodeAction[]>('vscode.executeCodeActionProvider', uri, new vscode.Range(0, 0, 0, 0), 'source.organizeImports');
			const results = await eventually(actions, value => value?.some(action => action.kind?.value === 'source.organizeImports') === true, 'organize imports action');
			const action = results!.find(value => value.kind?.value === 'source.organizeImports');
			assert.ok(action?.edit);
			const edits = action.edit.get(uri);
			assert.equal(edits.length, 1);
			assert.equal(edits[0]!.newText, '');
			assert.deepEqual(edits[0]!.range, new vscode.Range(2, 0, 3, 0), 'native edits delete only the duplicate line');
			assert.ok(await vscode.workspace.applyEdit(action.edit));
			assert.equal(document.getText(), prefix + suffix);
			assert.equal(document.eol, vscode.EndOfLine.CRLF);
			assert.ok(await document.save());
			const repeated = await actions();
			assert.ok(!repeated?.some(value => value.kind?.value === 'source.organizeImports'), 'organizing is idempotent');
		}
		finally
		{
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
		}
	});

	test('applies inherited-member snippets and rewrite actions using native insertion ranges', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'member implementations.vs'));
		const source = [
			'class Base { public: virtual int read(int value) const { return value; } virtual void sealed() final {} private: virtual void hidden() {} }',
			'interface Contract<T> { virtual T get() = 0; }',
			'class Derived : public Base, public Contract<int>',
			'{', 'public:', '    ', '    ', '}', 'void main() {}', ''
		].join('\n');
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		const editor = await vscode.window.showTextDocument(document);
		try {
			await api.ready(uri);
			const position = new vscode.Position(5, 4);
			const completion = await eventually(
				() => vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, position, undefined, 20),
				value => value?.items.some(item => item.insertText instanceof vscode.SnippetString && item.insertText.value.includes('int read(int value) const override')) === true,
				'inherited-member snippet');
			const generated = completion!.items.filter(item => item.insertText instanceof vscode.SnippetString && item.insertText.value.includes('throw 0;'));
			const name = (item: vscode.CompletionItem) => typeof item.label === 'string' ? item.label : item.label.label;
			assert.deepEqual(generated.map(name).sort(), ['get', 'read'], 'final and inaccessible methods have no implementation candidate');
			const read = generated.find(item => name(item) === 'read')!;
			const get = generated.find(item => name(item) === 'get')!;
			assert.ok(get.insertText instanceof vscode.SnippetString && get.insertText.value.includes('int get()') && !get.insertText.value.includes('override'));
			assert.ok(read.insertText instanceof vscode.SnippetString && read.range);
			const range = read.range instanceof vscode.Range ? read.range : read.range.replacing;
			assert.deepEqual(range, new vscode.Range(position, position), 'native insertion range remains exact');
			assert.ok(await editor.insertSnippet(read.insertText, range));
			await vscode.commands.executeCommand('leaveSnippet');
			assert.ok(document.getText().includes('int read(int value) const override'));
			const closing = document.positionAt(document.getText().lastIndexOf('\n}') + 1);
			const blank = document.lineAt(closing.line - 1);
			assert.equal(blank.text.trim(), '');
			const actionPosition = new vscode.Position(blank.lineNumber, blank.text.length);
			const actions = await eventually(
				() => vscode.commands.executeCommand<vscode.CodeAction[]>('vscode.executeCodeActionProvider', uri, new vscode.Range(actionPosition, actionPosition), 'refactor.rewrite', 20),
				value => value?.some(action => action.edit?.get(uri).some(edit => edit.newText.includes('int get()'))) === true,
				'interface implementation code action');
			assert.ok(!actions!.some(action => action.edit?.get(uri).some(edit => edit.newText.includes('int read('))), 'implemented overrides are not offered again');
			const action = actions!.find(action => action.edit?.get(uri).some(edit => edit.newText.includes('int get()')))!;
			assert.equal(action.kind?.value, 'refactor.rewrite');
			assert.ok(action.edit && await vscode.workspace.applyEdit(action.edit));
			assert.ok(document.getText().includes('int get()'));
			assert.ok(!document.getText().includes('${'), 'snippet placeholders are expanded and rewrite actions use ordinary edits');
			assert.ok(await document.save());
			const executable = path.join(process.env.VEROSCRIPT_TEST_RUNTIME!, process.platform === 'win32' ? 'veroscript.exe' : 'veroscript');
			await new Promise<void>((resolve, reject) => {
				execFile(executable, ['--backend=vm', '--check', uri.fsPath], {
					cwd: rootA, env: processEnvironment(), encoding: 'utf8', timeout: 10000, windowsHide: true
				}, (error, _stdout, stderr) => error ? reject(new Error(stderr || error.message)) : resolve());
			});
		}
		finally
		{
			await vscode.commands.executeCommand('leaveSnippet');
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
			await fs.unlink(uri.fsPath);
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('presents native hover and resolved completion documentation without changing edits or signature help', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'documentation.vs'));
		const source = '/// Add two values.\n/// @param left first value\n/// @param right second value\n/// @returns the sum\nint64 documentedAdd(int64 left, int64 right) { return left + right; }\nvoid main() { print(documentedAdd(1, 2)); }\n';
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		try {
			await api.ready(uri);
			const offset = source.lastIndexOf('documentedAdd(');
			const hover = await eventually(() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, document.positionAt(offset + 1)), value => !!value?.length, 'documented hover');
			const contents = hover![0]!.contents[0] as vscode.MarkdownString;
			assert.ok(contents.value.includes('```veroscript\nint64(int64 left, int64 right)\n```'));
			assert.ok(contents.value.includes('Add two values.') && contents.value.includes('`left`') && contents.value.includes('the sum'));
			assert.ok(contents.value.includes('documentation.vs:5'));
			const printHover = await eventually(() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, document.positionAt(source.lastIndexOf('print(') + 1)), value => !!value?.length, 'builtin hover');
			const printContents = printHover![0]!.contents[0] as vscode.MarkdownString;
			assert.ok(printContents.value.startsWith('**function** ` print `'), 'the builtin title starts with ordinary bold text, avoiding inline-code padding at the left edge');
			assert.ok(printContents.value.includes('```veroscript\nvoid print(any value);\nvoid print(string fmt, ...);\n```'));
			const typeHover = await eventually(() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, document.positionAt(source.indexOf('int64 documentedAdd') + 1)), value => !!value?.length, 'primitive type hover');
			const typeContents = typeHover![0]!.contents[0] as vscode.MarkdownString;
			assert.equal(typeContents.value, '**builtin type** ` int64 `\n\nBuilt-in VeroScript type.');
			const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, document.positionAt(offset + 'documentedAdd'.length), undefined, 20);
			const item = completion?.items.find(candidate => candidate.label === 'documentedAdd');
			assert.ok(item?.documentation instanceof vscode.MarkdownString);
			assert.ok(item.documentation.value.startsWith('```veroscript\n'));
			assert.equal(item.insertText, 'documentedAdd');
			const signature = await vscode.commands.executeCommand<vscode.SignatureHelp>('vscode.executeSignatureHelpProvider', uri, document.positionAt(offset + 'documentedAdd(1, '.length));
			assert.equal(signature?.activeParameter, 1);
			const parameter = signature.signatures[signature.activeSignature]?.parameters[1]?.label;
			assert.ok(typeof parameter === 'string' && parameter.includes('right'));

			const original = new vscode.MarkdownString('[Reference](./docs.md)');
			original.baseUri = vscode.Uri.file(rootA + path.sep);
			original.supportThemeIcons = true;
			const synthetic = new vscode.CompletionItem('collect');
			synthetic.detail = 'array<string>(int64 count)';
			synthetic.documentation = original;
			synthetic.insertText = new vscode.SnippetString('collect(${1:count})');
			const insertion = synthetic.insertText;
			presentCompletion(synthetic);
			assert.strictEqual(synthetic.insertText, insertion);
			const rendered = synthetic.documentation as vscode.MarkdownString;
			assert.strictEqual(rendered.baseUri, original.baseUri);
			assert.equal(rendered.supportThemeIcons, true);
			assert.ok(rendered.value.endsWith(original.value));
			presentCompletion(synthetic);
			assert.strictEqual(synthetic.documentation, rendered, 'repeated presentation does not duplicate documentation');
			const plain = new vscode.CompletionItem('plain');
			plain.detail = 'string()';
			plain.documentation = 'Literal <T> and [brackets].';
			const literal = new vscode.MarkdownString().appendText(plain.documentation).value;
			const plainDocumentation = presentCompletion(plain).documentation;
			assert.ok(plainDocumentation instanceof vscode.MarkdownString && plainDocumentation.value.endsWith(literal));
			for (const detail of ['builtin type', '内置类型', 'soft builtin', 'soft builtin value type', 'soft builtin (delegate.vs; delegate<R>)']) {
				const described = new vscode.CompletionItem('array', vscode.CompletionItemKind.Class);
				described.detail = detail;
				described.documentation = detail;
				described.insertText = new vscode.SnippetString('array<${1:T}>');
				const insertion = described.insertText;
				const documentation = presentCompletion(described).documentation;
				assert.ok(documentation instanceof vscode.MarkdownString);
				assert.ok(!documentation.value.includes('```'), detail);
				assert.strictEqual(described.insertText, insertion);
				assert.equal(described.detail, detail);
				presentCompletion(described);
				assert.strictEqual(described.documentation, documentation, 'metadata rendering is idempotent');
			}
			const soft = new vscode.CompletionItem('builtinFunction');
			soft.detail = 'void builtinFunction();';
			soft.documentation = 'soft builtin';
			assert.equal((presentCompletion(soft).documentation as vscode.MarkdownString).value,
				'```veroscript\nvoid builtinFunction();\n```\n\n*Built-in symbol.*');
			const softType = new vscode.CompletionItem('optional<T>');
			softType.detail = 'soft builtin value type';
			softType.documentation = 'soft builtin';
			assert.equal((presentCompletion(softType).documentation as vscode.MarkdownString).value, '*Built-in symbol.* ` value type `');
			const range = new vscode.Range(0, 0, 0, 1);
			const preserved = presentHover(new vscode.Hover(original, range));
			assert.strictEqual(preserved.contents[0], original, 'unknown Markdown is preserved');
			assert.strictEqual(preserved.range, range);
			const native = new vscode.MarkdownString('**` function std::print `**\n\n```veroscript\nvoid std::print(any value);\n```');
			native.baseUri = original.baseUri;
			native.isTrusted = { enabledCommands: ['veroscript.showOutput'] };
			native.supportHtml = true;
			native.supportThemeIcons = true;
			const aligned = presentHover(new vscode.Hover(native, range));
			const alignedContents = aligned.contents[0] as vscode.MarkdownString;
			assert.equal(alignedContents.value, '**function** ` std::print `\n\n```veroscript\nvoid std::print(any value);\n```');
			assert.strictEqual(aligned.range, range);
			assert.strictEqual(alignedContents.baseUri, native.baseUri);
			assert.strictEqual(alignedContents.isTrusted, native.isTrusted);
			assert.equal(alignedContents.supportHtml, true);
			assert.equal(alignedContents.supportThemeIcons, true);
		}
		finally
		{
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await fs.unlink(uri.fsPath);
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('presents primitive and container builtins consistently while preserving source aliases and soft type help', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'builtin type presentation.vs'));
		const source = 'using Count = int64;\nvoid sample(array<int8> values, map<string, int> table, priority_queue<int> queue, optional<int> selected, readonly<int> frozen) {}\nvoid main() { Count value = 1; }\n';
		await fs.writeFile(uri.fsPath, source);
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		try {
			await api.ready(uri);
			for (const name of ['int64', 'int8', 'array', 'map', 'string', 'priority_queue']) {
				const hover = await eventually(() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, document.positionAt(source.indexOf(name) + 1)),
					value => !!value?.length, 'builtin type hover: ' + name);
				const value = (hover![0]!.contents[0] as vscode.MarkdownString).value;
				assert.equal(value, `**builtin type** \` ${name} \`\n\nBuilt-in VeroScript type.`, name);
			}
			for (const name of ['optional', 'readonly']) {
				const hover = await eventually(() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, document.positionAt(source.indexOf(name) + 1)),
					value => !!value?.length, 'soft type hover: ' + name);
				const value = (hover![0]!.contents[0] as vscode.MarkdownString).value;
				assert.ok(value.includes(name) && value.includes('Built-in symbol.'), name);
				assert.ok(!value.includes('```veroscript\nsoft builtin'), name);
			}
			const alias = await eventually(() => vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, document.positionAt(source.lastIndexOf('Count') + 1)),
				value => !!value?.length, 'source alias hover');
			const aliasValue = (alias![0]!.contents[0] as vscode.MarkdownString).value;
			assert.ok(aliasValue.startsWith('**type alias** ` Count `'));
			assert.ok(aliasValue.includes('```veroscript') && aliasValue.includes('defined at'));
			for (const prefix of ['arr', 'int', 'opt']) {
				const text = `void main() {\n    ${prefix}\n}\n`;
				const edit = new vscode.WorkspaceEdit();
				edit.replace(uri, new vscode.Range(0, 0, document.lineCount, 0), text);
				assert.ok(await vscode.workspace.applyEdit(edit));
				const list = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, new vscode.Position(1, 4 + prefix.length), undefined, 30);
				const name = prefix === 'arr' ? 'array' : prefix === 'opt' ? 'optional' : 'int8';
				const item = list?.items.find(item => (typeof item.label === 'string' ? item.label : item.label.label) === name);
				assert.ok(item, name + ': ' + JSON.stringify(list?.items.map(item => item.label)));
				assert.ok(item.documentation instanceof vscode.MarkdownString, name);
				assert.ok(item.documentation.value.startsWith(prefix === 'opt' ? '*Built-in symbol.*' : '**builtin type**'), name);
				assert.ok(!item.documentation.value.includes('```veroscript\nbuiltin type') && !item.documentation.value.includes('```veroscript\nsoft builtin'), name);
			}
		}
		finally
		{
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
			await fs.unlink(uri.fsPath);
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('suppresses textual fallback and continues accepted namespace completions without changing native filters', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'completion.vs'));
		await fs.writeFile(uri.fsPath, 'void main() {}\n');
		const document = await vscode.workspace.openTextDocument(uri);
		const editor = await vscode.window.showTextDocument(document);
		const editorSettings = vscode.workspace.getConfiguration('editor', document);
		assert.equal(editorSettings.get('wordBasedSuggestions'), 'off');
		const client = await api.ready(uri);
		assert.ok(client);
		const replace = async (marked: string) => {
			const index = marked.indexOf('|');
			assert.notEqual(index, -1);
			const edit = new vscode.WorkspaceEdit();
			edit.replace(uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), marked.replace('|', ''));
			assert.ok(await vscode.workspace.applyEdit(edit));
			const position = document.positionAt(index);
			editor.selection = new vscode.Selection(position, position);
			return position;
		};
		try {
			for (const source of ['// All Rights Reserved.\nclass A|', 'struct A|', 'interface A|', 'enum A|', 'namespace A|', 'void main() { int64 A|; }', 'void main() { // pri|\n}', 'void main() { print("pri|"); }', 'void main() { unknown::| }']) {
				const position = await replace(source);
				const emptyResult: { items: unknown[] } = await client.sendRequest('textDocument/completion', { textDocument: { uri: uri.toString() }, position });
				assert.deepEqual(emptyResult.items, [], source);
				const list = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, position);
				assert.equal(list?.items.length ?? 0, 0, source);
			}
			const position = await replace('void main() { std| }');
			const native = await client.sendRequest<{ items: { label: string; filterText: string; sortText: string }[] }>('textDocument/completion', { textDocument: { uri: uri.toString() }, position });
			const list = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, position, undefined, 20);
			const item = list?.items.find(candidate => candidate.label === 'std');
			assert.ok(item);
			assert.equal(item.insertText, 'std::');
			assert.equal(item.command?.command, 'editor.action.triggerSuggest', 'acceptance command survives completion resolve');
			for (const expected of native.items) {
				const actual = list!.items.find(candidate => (typeof candidate.label === 'string' ? candidate.label : candidate.label.label) === expected.label);
				assert.ok(actual);
				const label = typeof actual.label === 'string' ? actual.label : actual.label.label;
				assert.equal(actual.filterText ?? label, expected.filterText);
				assert.equal(actual.sortText, expected.sortText);
			}
			await vscode.commands.executeCommand('editor.action.triggerSuggest');
			await eventually(async () => {
				await vscode.commands.executeCommand('acceptSelectedSuggestion');
				return document.getText();
			}, text => text.includes('std::'), 'accept std namespace');
			// Do not trigger a second list explicitly: accepting std must open it.
			await eventually(async () => {
				await vscode.commands.executeCommand('acceptSelectedSuggestion');
				return document.getText();
			}, text => text.includes('std::algorithm::'), 'accept automatically suggested child namespace');
			await vscode.commands.executeCommand('hideSuggestWidget');

			const namespacePosition = await replace('void main() { std::alg|orithm::sort(); }');
			// Runtime discovery resolves directory junctions before starting the server.
			const modulePath = await fs.realpath(path.join(process.env.VEROSCRIPT_TEST_RUNTIME!, 'stdlib', 'algorithm.vs'));
			const settings = vscode.workspace.getConfiguration('veroscript', document);
			assert.deepEqual(settings.get<string[]>('importPaths'), [], 'namespace navigation uses the default standard-library search path');
			assert.deepEqual(settings.get<string[]>('stdlibSearchPaths'), [], 'the empty setting selects Runtime stdlib');
			await eventually(() => vscode.commands.executeCommand<(vscode.Location | vscode.LocationLink)[]>('vscode.executeDefinitionProvider', document.uri, namespacePosition),
				definitions => !!definitions?.some(definition => path.relative(('uri' in definition ? definition.uri : definition.targetUri).fsPath, modulePath) === ''),
				'standard namespace definition preserves the Runtime module path without explicit imports');

			const synthetic = new vscode.CompletionItem('std', vscode.CompletionItemKind.Module);
			synthetic.insertText = 'std::';
			synthetic.command = { title: 'Existing', command: 'existing.command' };
			continueNamespaceCompletion(synthetic);
			assert.equal(synthetic.command.command, 'existing.command');
			const callable = new vscode.CompletionItem('call', vscode.CompletionItemKind.Function);
			callable.insertText = new vscode.SnippetString('call(${1:value})');
			continueNamespaceCompletion(callable);
			assert.equal(callable.command, undefined);

			await editorSettings.update('suggestOnTriggerCharacters', false, vscode.ConfigurationTarget.WorkspaceFolder, true);
			const disabledPosition = await replace('void main() { std| }');
			const disabled = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, disabledPosition);
			assert.equal(disabled?.items.find(candidate => candidate.label === 'std')?.command, undefined);
		}
		finally
		{
			await editorSettings.update('suggestOnTriggerCharacters', undefined, vscode.ConfigurationTarget.WorkspaceFolder, true);
			await vscode.commands.executeCommand('hideSuggestWidget');
			await document.save();
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await fs.unlink(uri.fsPath);
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('preserves native completion recovery, numeric suppression and local scope boundaries', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'completion recovery.vs'));
		await fs.writeFile(uri.fsPath, 'void main() {}\n');
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		const client = await api.ready(uri);
		assert.ok(client);
		const cases: [string, string | null | undefined][] = [
			['void main(){ for(int i=0;i|) }', 'i'],
			['void main(){ for(int i=0;i|', 'i'],
			['void main(){ for(int i=0;|', 'i'],
			['void main(){ for(int i=0;i<10;i|) }', 'i'],
			['void main(){ for(int i=0;abs(i|)) }', 'i'],
			['void main()\r\n{\r\n    for(int i=0;\r\n        i|)\r\n}', 'i'],
			['void main(){ for(int i=0;i<1;++i) { for(int j=0;j|) } }', 'j'],
			['void main(){ for(int i=0;i<1;++i) {} i|; }', null],
			['void first(){ for(int i=0;i<1;++i) {} } void second(){ i|; }', null],
			['void main(){ int value=0|; }', undefined],
			['void main(){ int value=0xF|F; }', undefined],
			['void main(){ int value=0b1|01; }', undefined],
			['void main(){ int value=12i|64; }', undefined],
			['void main(){ double value=1e+|2; }', undefined],
			['void main(){ int value=0x|; }', undefined],
			['void main(){ double value=1e+|; }', undefined]
		];
		try {
			for (const [marked, local] of cases) {
				const offset = marked.indexOf('|');
				const edit = new vscode.WorkspaceEdit();
				edit.replace(uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), marked.replace('|', ''));
				assert.ok(await vscode.workspace.applyEdit(edit));
				// VS Code normalizes inserted line endings to the document's EOL.
				const prefixLines = marked.slice(0, offset).split(/\r\n|\r|\n/);
				const position = new vscode.Position(prefixLines.length - 1, prefixLines.at(-1)!.length);
				const native: NativeCompletionResult = await client.sendRequest('textDocument/completion', { textDocument: { uri: uri.toString() }, position });
				const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', uri, position);
				const items = completion?.items ?? [];
				assert.ok(!items.some(item => item.label === 'import' || item.kind === vscode.CompletionItemKind.Snippet), marked);
				if (local) {
					const expected = native.items.find(item => item.label === local);
					const actual = items.find(item => item.label === local);
					assert.ok(expected && actual, marked);
					assert.equal(actual.kind, vscode.CompletionItemKind.Variable, marked);
					assert.equal(actual.filterText ?? actual.label, expected.filterText, marked);
					assert.equal(actual.sortText, expected.sortText, marked);
					assert.equal(actual.preselect, expected.preselect, marked);
					assert.equal(actual.insertText ?? actual.label, expected.textEdit?.newText ?? expected.insertText, marked);
					const defaultRange = document.getWordRangeAtPosition(position) ?? new vscode.Range(position, position);
					const range = expected.textEdit?.range;
					const expectedRange = range ? new vscode.Range(range.start.line, range.start.character, range.end.line, range.end.character) : defaultRange;
					const actualRange = actual.range ?? defaultRange;
					if (actualRange instanceof vscode.Range) {
						assert.ok(actualRange.isEqual(expectedRange), marked);
					}
					else
					{
						assert.ok(actualRange.inserting.isEqual(expectedRange) && actualRange.replacing.isEqual(expectedRange), marked);
					}
				}
				else if (local === null)
				{
					assert.ok(!native.items.some(item => item.label === 'i') && !items.some(item => item.label === 'i'), marked);
				}
				else
				{
					assert.deepEqual(native.items, [], marked);
					assert.deepEqual(items, [], marked);
				}
			}
		}
		finally
		{
			await document.save();
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await fs.unlink(uri.fsPath);
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('offers contextual templates in untitled documents without a native language server', async () => {
		const document = await vscode.workspace.openTextDocument({ language: 'veroscript', content: '' });
		await vscode.window.showTextDocument(document);
		const cases: [string, string[]][] = [
			['im|', ['import']], ['ma|', ['main']], ['he|', ['hello']], ['fn|', ['fn']], ['cl|', ['class']],
			['void main() {\n    fo|\n}', ['for']], ['class Demo {\n    fn|\n}', ['fn']],
			['namespace demo {\n    cl|\n}', ['class']],
			['void main() { for (int i = 0;i|) }', []], ['void main() { for (int i = 0;\n    i|) }', []],
			['void main() {\n    im|\n}', []], ['namespace demo {\n    im|\n}', []],
			['void main() { print("im|"); }', []], ['// im|', []], ['/* im| */', []],
			['class im|', []], ['std::|', []], ['void main() { call(\n    fo|); }', []],
			['array<int> values = array<int>{1, 2}\n    im|', []], ['class Demo {}\n    im|', ['import']],
			['void main() { auto callback = fn() -> void {}\n    fo| }', []],
			['void main() { auto callback = []() {}\n    fo| }', []],
			['array<int> values = array<int>{1, 2};\n    im|', ['import']],
			['void main() { auto callback = fn() -> void {};\n    fo| }', ['for']],
			['#define BODY \\\n    im|', []], ['#define BODY \\\r\n    ma|', []],
			['#define BODY \\\n    print(1); \\\n    im|', []],
			['void main() {\n#define BODY \\\n    print(1); fo|\n}', []],
			['#define BODY \\\n    print(1);\n    im|', ['import']],
			['// continued \\\n    im|', []], ['// continued \\\r\n    ma|', []],
			['// continued \\\n    print(1); \\\n    im|', []],
			['void main() {\n// continued \\\n    print(1); fo|\n}', []],
			['// continued \\\n    ignored\n    im|', ['import']],
			['#define BODY /*\n    im|', []], ['#define BODY \\\n/*\nvoid sample() { fo|', []],
			['#define BODY /*\n*/void sample() {\n    fo|\n}', ['for']],
			['#define BODY /* inline */ void sample() {\n    im|', ['import']]
		];
		try {
			for (const [marked, expected] of cases) {
				const offset = marked.indexOf('|');
				const edit = new vscode.WorkspaceEdit();
				edit.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), marked.replace('|', ''));
				assert.ok(await vscode.workspace.applyEdit(edit));
				// Text models normalize inserted line endings to the document's EOL.
				// Locate the marker by line/column, independent of LF/CRLF fixtures.
				const prefixLines = marked.slice(0, offset).split(/\r\n|\r|\n/);
				const position = new vscode.Position(prefixLines.length - 1, prefixLines.at(-1)!.length);
				const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', document.uri, position);
				assert.deepEqual(completion?.items.map(item => item.label) ?? [], expected, marked);
				for (const item of completion?.items ?? []) {
					assert.equal(item.kind, vscode.CompletionItemKind.Snippet);
					assert.ok(item.insertText instanceof vscode.SnippetString);
				}
			}
		}
		finally
		{
			const clear = new vscode.WorkspaceEdit();
			clear.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), '');
			await vscode.workspace.applyEdit(clear);
			await vscode.window.showTextDocument(document);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('synchronizes unsaved text, diagnostics and formatting', async () => {
		const document = documents[0]!;
		const edit = new vscode.WorkspaceEdit();
		edit.insert(program, document.positionAt(document.getText().length), '\nint64 fromMemory() { return 1; }\nvoid broken() { print(missing_name); }\n');
		assert.equal(await vscode.workspace.applyEdit(edit), true);
		await eventually(() => vscode.commands.executeCommand<vscode.DocumentSymbol[]>('vscode.executeDocumentSymbolProvider', program), symbols => !!symbols?.some(symbol => symbol.name === 'fromMemory'), 'unsaved symbols');
		await eventually(() => vscode.languages.getDiagnostics(program), diagnostics => diagnostics.some(diagnostic => diagnostic.severity === vscode.DiagnosticSeverity.Error), 'unsaved diagnostic');
		const edits = await vscode.commands.executeCommand<vscode.TextEdit[]>('vscode.executeFormatDocumentProvider', program, { tabSize: 4, insertSpaces: true });
		assert.ok(edits?.length);
		const restore = new vscode.WorkspaceEdit();
		restore.replace(program, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), original);
		await vscode.workspace.applyEdit(restore);
		assert.ok(await document.save());
		await eventually(() => vscode.languages.getDiagnostics(program), diagnostics => !diagnostics.some(diagnostic => diagnostic.severity === vscode.DiagnosticSeverity.Error), 'diagnostics cleared');
	});

	test('recovers cyclic import overlays and excessive parse depth without restarting the server', async () => {
		const directory = path.join(rootA, 'cyclic overlays');
		await fs.mkdir(directory);
		const mainUri = vscode.Uri.file(path.join(directory, 'main.vs'));
		const helperUri = vscode.Uri.file(path.join(directory, 'helper.vs'));
		const source = 'import "helper.vs";\nexport int seed() { return 40; }\nvoid main() { print(answer()); }\n';
		const helperSource = 'import "main.vs";\nexport int answer() { return seed() + 2; }\n';
		await Promise.all([fs.writeFile(mainUri.fsPath, source), fs.writeFile(helperUri.fsPath, helperSource)]);
		const main = await vscode.workspace.openTextDocument(mainUri);
		const helper = await vscode.workspace.openTextDocument(helperUri);
		await vscode.window.showTextDocument(main);
		const client = await api.ready(mainUri);
		assert.ok(client);
		assert.equal(await api.ready(helperUri), client);
		const states: number[] = [];
		const listener = client.onDidChangeState(event => states.push(event.newState));
		const replace = async (document: vscode.TextDocument, text: string) => {
			const edit = new vscode.WorkspaceEdit();
			edit.replace(document.uri, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), text);
			assert.ok(await vscode.workspace.applyEdit(edit));
		};
		const hasErrors = (uri: vscode.Uri) => vscode.languages.getDiagnostics(uri).some(item => item.severity === vscode.DiagnosticSeverity.Error);
		// Use the current editor document for canonical URIs and cursor positions.
		// File creation and document synchronization may invalidate an in-flight snapshot.
		const tokens = async () => (await eventually(async () => {
			try {
				return await client.sendRequest<{ data: number[] }>('textDocument/semanticTokens/full', { textDocument: { uri: main.uri.toString() } });
			}
			catch (error)
			{
				if (error instanceof Error && 'code' in error && error.code === -32801) return undefined;
				throw error;
			}
		}, value => value !== undefined, 'current cyclic import snapshot'))!;
		const navigation = () => vscode.commands.executeCommand<(vscode.Location | vscode.LocationLink)[]>('vscode.executeDefinitionProvider', main.uri, main.positionAt(main.getText().indexOf('answer()') + 1));
		try {
			const importedDefinition = (items: (vscode.Location | vscode.LocationLink)[] | undefined) => !!items?.some(item => path.relative(('uri' in item ? item.uri : item.targetUri).fsPath, helper.uri.fsPath) === '');
			await eventually(navigation, importedDefinition, 'cyclic imported definition');
			const synchronized = new Set(Array.from(client.getFeature('textDocument/didOpen').openDocuments).map(document => document.uri.toString()));
			for (const document of [documents[0]!, main, helper]) {
				assert.ok(synchronized.has(document.uri.toString()), 'expanding the selector retains every owned open document');
			}
			const baseline = await tokens();
			assert.ok(baseline.data.length);
			await replace(helper, 'export int answer( {');
			await eventually(() => hasErrors(helper.uri), Boolean, 'cyclic dependency parse error');
			assert.ok(helper.isDirty, 'invalid dependency remains an unsaved overlay');
			await replace(helper, helperSource);
			await eventually(() => hasErrors(helper.uri), value => !value, 'cyclic dependency repair');
			assert.deepEqual((await tokens()).data, baseline.data);
			await replace(main, 'namespace n{'.repeat(256) + '}'.repeat(256));
			await eventually(() => hasErrors(main.uri), Boolean, 'namespace depth diagnostic');
			await replace(main, source);
			await eventually(() => hasErrors(main.uri), value => !value, 'guarded document repair');
			assert.deepEqual((await tokens()).data, baseline.data);
			await eventually(navigation, importedDefinition, 'restored cyclic imported definition');
			assert.ok(await helper.save());
			await vscode.window.showTextDocument(helper);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await vscode.window.showTextDocument(main);
			await api.ready(main.uri);
			assert.deepEqual((await tokens()).data, baseline.data, 'closing a dependency retains the main document');
			await eventually(navigation, importedDefinition, 'cyclic definition after closing the dependency');
			assert.equal(await api.ready(mainUri), client);
			assert.deepEqual(states, [], 'editing errors must not crash or restart the language server');
		}
		finally
		{
			listener.dispose();
			for (const document of [helper, main]) {
				if (!document.isClosed) {
					await document.save();
					await vscode.window.showTextDocument(document);
					await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
				}
				await fs.unlink(document.uri.fsPath);
			}
			await vscode.window.showTextDocument(documents[0]!);
		}
	});

	test('refreshes imported disk files with overlapping watch paths', async () => {
		const document = documents[0]!;
		const dependency = path.join(rootA, 'watcher_dep.vs');
		const valid = 'export int64 watched() { return 1; }\n';
		const settings = vscode.workspace.getConfiguration('veroscript', program);
		const replace = async (source: string) => {
			const edit = new vscode.WorkspaceEdit();
			edit.replace(program, new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), source);
			await vscode.workspace.applyEdit(edit);
			assert.ok(await document.save());
		};
		await fs.writeFile(dependency, valid);
		await settings.update('importPaths', [process.platform === 'win32' ? rootA.toUpperCase() : rootA], vscode.ConfigurationTarget.WorkspaceFolder);
		try {
			await replace('import "watcher_dep.vs";\nvoid main() { int64 value = watched(); print(value); }\n');
			await new Promise(resolve => setTimeout(resolve, 500));
			await api.ready(program);
			assert.ok(!vscode.languages.getDiagnostics(program).some(diagnostic => diagnostic.severity === vscode.DiagnosticSeverity.Error));
			await fs.writeFile(dependency, 'export int64 renamed() { return 2; }\n');
			await eventually(() => vscode.languages.getDiagnostics(program), diagnostics => diagnostics.some(diagnostic => diagnostic.severity === vscode.DiagnosticSeverity.Error), 'import file change invalidated diagnostics');
			await fs.writeFile(dependency, valid);
			await eventually(() => vscode.languages.getDiagnostics(program), diagnostics => !diagnostics.some(diagnostic => diagnostic.severity === vscode.DiagnosticSeverity.Error), 'import file restoration cleared diagnostics');
		}
		finally
		{
			await replace(original);
			await settings.update('importPaths', undefined, vscode.ConfigurationTarget.WorkspaceFolder);
		}
	});

	test('forwards create, change and delete events for current uppercase library extensions', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'watched library.VSLIB'));
		const client = await api.ready(program);
		assert.ok(client);
		const original = client.sendNotification;
		const seen: number[] = [];
		client.sendNotification = async (method, params) => {
			if (method === 'workspace/didChangeWatchedFiles') {
				for (const change of (params as { changes: { uri: string; type: number }[] }).changes) {
					if (change.uri === uri.toString()) {
						seen.push(change.type);
					}
				}
			}
			await original.call(client, method, params);
		};
		try {
			await fs.writeFile(uri.fsPath, 'first snapshot');
			await eventually(() => seen, values => values.includes(1), 'library created');
			await fs.writeFile(uri.fsPath, 'second library snapshot');
			await eventually(() => seen, values => values.includes(2), 'library changed');
			await fs.unlink(uri.fsPath);
			await eventually(() => seen, values => values.includes(3), 'library deleted');
		}
		finally
		{
			client.sendNotification = original;
			await fs.rm(uri.fsPath, { force: true });
		}
	});

	test('refreshes an unsaved dependency owned by another workspace root', async () => {
		const document = documents[0]!;
		const directory = rootB;
		const dependency = vscode.Uri.file(path.join(directory, 'other.vs'));
		const originalImport = await fs.readFile(dependency.fsPath, 'utf8');
		const valid = 'export int64 sharedValue() { return 1; }\n';
		const settings = vscode.workspace.getConfiguration('veroscript', program);
		const replace = async (target: vscode.TextDocument, source: string) => {
			const edit = new vscode.WorkspaceEdit();
			edit.replace(target.uri, new vscode.Range(target.positionAt(0), target.positionAt(target.getText().length)), source);
			assert.ok(await vscode.workspace.applyEdit(edit));
		};
		await fs.writeFile(dependency.fsPath, valid);
		const imported = await vscode.workspace.openTextDocument(dependency);
		await vscode.window.showTextDocument(imported);
		await settings.update('importPaths', [directory], vscode.ConfigurationTarget.WorkspaceFolder);
		try {
			await replace(document, 'import "other.vs";\nvoid main() { print(sharedValue()); }\n');
			assert.ok(await document.save());
			await new Promise(resolve => setTimeout(resolve, 500));
			await api.ready(program);
			assert.ok(!vscode.languages.getDiagnostics(program).some(diagnostic => diagnostic.severity === vscode.DiagnosticSeverity.Error));
			const position = document.positionAt(document.getText().lastIndexOf('sharedValue') + 1);
			const hover = async () => {
				const result = await vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', program, position);
				return result?.flatMap(item => item.contents.map(content => typeof content === 'string' ? content : content.value)).join('\n') ?? '';
			};
			await eventually(hover, value => value.includes('int64'), 'initial imported signature');
			await replace(imported, 'export string sharedValue() { return "updated"; }\n');
			await eventually(hover, value => value.includes('string'), 'unsaved external import updated signature');
			await settings.update('importPaths', [], vscode.ConfigurationTarget.WorkspaceFolder);
			await new Promise(resolve => setTimeout(resolve, 400));
			await api.ready(program);
			const importing = await api.ready(program);
			assert.ok(importing);
			assert.ok(![...importing.getFeature('textDocument/didOpen').openDocuments].some(item => item.uri.toString() === dependency.toString()), 'removed imports leave the SDK synchronization registry');
			await settings.update('importPaths', [directory], vscode.ConfigurationTarget.WorkspaceFolder);
			await new Promise(resolve => setTimeout(resolve, 400));
			await api.ready(program);
			await eventually(hover, value => value.includes('string'), 'existing unsaved buffer synchronized after import settings changed');
			const client = await api.ready(program);
			assert.ok(client);
			const states: number[] = [];
			const listener = client.onDidChangeState(event => states.push(event.newState));
			try {
				await client.sendNotification('exit');
				await eventually(() => states, value => value.includes(1) && value.at(-1) === 2, 'importing server restarted');
				await eventually(hover, value => value.includes('string'), 'unsaved import restored after server restart');
			}
			finally
			{
				listener.dispose();
			}
			await replace(imported, valid);
			await eventually(hover, value => value.includes('int64'), 'unsaved external import restored signature');
		}
		finally
		{
			await replace(imported, originalImport);
			await imported.save();
			await vscode.window.showTextDocument(imported);
			await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			await replace(document, original);
			await document.save();
			await settings.update('importPaths', undefined, vscode.ConfigurationTarget.WorkspaceFolder);
			await vscode.window.showTextDocument(document);
		}
	});

	test('isolates two roots, a nested root, and a standalone document', async () => {
		const uris = [path.join(rootB, 'other.vs'), path.join(rootA, 'nested/nested.vs'), path.join(sessionRoot, 'outside/outside [x].vs')].map(file => vscode.Uri.file(file));
		for (const uri of uris) {
			documents.push(await vscode.workspace.openTextDocument(uri));
		}
		await eventually(() => api.snapshot(), states => states.length === 4 && states.every(state => state.state === 'Running'), 'four isolated clients');
		for (const uri of uris) {
			const symbols = await vscode.commands.executeCommand<vscode.DocumentSymbol[]>('vscode.executeDocumentSymbolProvider', uri);
			assert.ok(symbols?.some(symbol => symbol.name === 'main'));
			const tokens = await vscode.commands.executeCommand<vscode.SemanticTokens>('vscode.provideDocumentSemanticTokens', uri);
			assert.ok(tokens?.data.length, `semantic provider ownership: ${uri.fsPath}`);
			const edits = await vscode.commands.executeCommand<vscode.TextEdit[]>('vscode.executeFormatDocumentProvider', uri, { tabSize: 4, insertSpaces: true });
			assert.ok(edits?.length, `formatter ownership: ${uri.fsPath}`);
		}
		const outside = await api.ready(uris[2]);
		assert.ok(outside);
		assert.deepEqual(await outside.sendRequest('workspace/symbol', { query: 'onlyB' }), []);
		const other = await api.ready(uris[0]);
		assert.ok(other);
		const symbols = await other.sendRequest<unknown[]>('workspace/symbol', { query: 'onlyB' });
		assert.ok(symbols.length);
		const nestedSymbols = await vscode.commands.executeCommand<vscode.SymbolInformation[]>('vscode.executeWorkspaceSymbolProvider', 'nestedOnly');
		assert.equal(nestedSymbols?.length, 1, 'nested root workspace symbols have a single owner');
		await vscode.workspace.getConfiguration('veroscript', uris[0]).update('defines', ['ROOT_B=1'], vscode.ConfigurationTarget.WorkspaceFolder);
		await new Promise(resolve => setTimeout(resolve, 400));
		await api.ready(uris[0]);
		assert.equal(api.snapshot().length, 4);
		await vscode.commands.executeCommand('veroscript.restartLanguageServer');
		assert.equal(api.snapshot().filter(state => state.state === 'Running').length, 4);
		await vscode.window.showTextDocument(documents[0]!);
	});

	test('includes explicit implicit-import buffers in save checks using host path comparison rules', async () => {
		const imported = await vscode.workspace.openTextDocument(vscode.Uri.file(path.join(rootB, 'other.vs')));
		const context = sourceContext(program);
		const options = { stdlibSearchPaths: [], importPaths: [], implicitImportPaths: [], defines: [], predefinedMacros: [],
			bindingCatalogPaths: [], allowCircularImports: true, allowProjectWideAccess: false };
		assert.equal(participating(imported, context, options), false, 'unrelated root is outside the save scope');
		assert.equal(participating(imported, context, { ...options, implicitImportPaths: [imported.uri.fsPath] }), true);
		assert.equal(participating(imported, context, { ...options, implicitImportPaths: [imported.uri.fsPath.toUpperCase()] }), process.platform === 'win32');
		assert.equal(participating(imported, context, { ...options, implicitImportPaths: [imported.uri.fsPath + '.other'] }), false);
	});

	test('runs native check and run tasks with real process exit codes', async () => {
		const execute = async (id: string) => {
			let result: vscode.TaskProcessEndEvent | undefined;
			const subscription = vscode.tasks.onDidEndTaskProcess(event => {
				if (event.execution.task.definition.program === program.fsPath) {
					result = event;
				}
			});
			let execution: vscode.TaskExecution | undefined;
			try {
				execution = await vscode.commands.executeCommand<vscode.TaskExecution>(id, program);
				assert.ok(execution);
				const event = await eventually(() => result, value => value !== undefined, id);
				assert.equal(event?.exitCode, 0);
			}
			finally
			{
				if (!result) {
					execution?.terminate();
				}
				subscription.dispose();
			}
		};
		await execute('veroscript.checkFile');
		await execute('veroscript.runFile');
		const output = vscode.window.createOutputChannel('VeroScript test', { log: true });
		resources.push(output);
		const tasks = new VeroTasks(new Services(output));
		const definition = { type: 'veroscript', action: 'run', program: 'args.vs', args: ['a b', '中文', '$(literal)'], importPaths: [], defines: [], backend: 'vm' };
		const task = new vscode.Task(definition, vscode.workspace.getWorkspaceFolder(program)!, 'Custom run', 'VeroScript');
		const resolved = await tasks.resolveTask(task);
		assert.ok(resolved?.execution instanceof vscode.ProcessExecution);
		assert.deepEqual(resolved.definition, definition);
		assert.deepEqual(resolved.execution.args.slice(-3), definition.args);
		assert.equal(resolved.execution.process.includes('veroscript'), true);
	});

	test('runs independent JIT modules with inherited, cleared and compile-only task paths', async function () {
		const configuredModule = process.env.VEROSCRIPT_TEST_JIT_BACKEND;
		if (!configuredModule) return this.skip();
		const module = path.resolve(configuredModule);
		const uri = vscode.Uri.file(path.join(rootA, 'jit task 中文.vs'));
		await fs.writeFile(uri.fsPath, 'int value() { return 42; } void main() { print(value()); }');
		const settings = vscode.workspace.getConfiguration('veroscript', uri);
		const previous = settings.inspect<string>('run.jitBackendPath')?.workspaceFolderValue;
		const previousBackend = settings.inspect<string>('run.backend')?.workspaceFolderValue;
		const output = vscode.window.createOutputChannel('VeroScript JIT task test', { log: true });
		resources.push(output);
		const tasks = new VeroTasks(new Services(output));
		const folder = vscode.workspace.getWorkspaceFolder(uri)!;
		const resolve = async (overrides: Record<string, unknown>) => {
			const definition = { type: 'veroscript', action: 'run', program: uri.fsPath, backend: 'jit', ...overrides };
			const task = await tasks.resolveTask(new vscode.Task(definition, folder, 'JIT task', 'VeroScript'));
			assert.ok(task?.execution instanceof vscode.ProcessExecution);
			return task;
		};
		const execute = async (task?: vscode.Task) => {
			let execution: vscode.TaskExecution | undefined;
			let result: vscode.TaskProcessEndEvent | undefined;
			const listener = vscode.tasks.onDidEndTaskProcess(event => {
				if (event.execution === execution) {
					result = event;
				}
			});
			try {
				execution = task ? await vscode.tasks.executeTask(task)
					: await vscode.commands.executeCommand<vscode.TaskExecution>('veroscript.runFile', uri);
				assert.ok(execution);
				if (!task) {
					const process = execution.task.execution;
					assert.ok(process instanceof vscode.ProcessExecution);
					assert.ok(process.args.includes('--backend=jit'));
					assert.ok(process.args.includes(module));
				}
				return (await eventually(() => result, value => value !== undefined, 'JIT task exit'))!.exitCode;
			}
			finally
			{
				if (!result) {
					execution?.terminate();
				}
				listener.dispose();
			}
		};
		try {
			await settings.update('run.jitBackendPath', module, vscode.ConfigurationTarget.WorkspaceFolder);
			await settings.update('run.backend', 'jit', vscode.ConfigurationTarget.WorkspaceFolder);
			const inherited = await resolve({});
			assert.ok((inherited.execution as vscode.ProcessExecution).args.includes(module));
			assert.equal(await execute(), 0, 'the installed Run File command selects the configured JIT module');
			const relative = path.relative(rootA, path.dirname(module));
			const explicit = await resolve({ jitBackendPath: relative, cwd: path.dirname(rootA) });
			const explicitArgs = (explicit.execution as vscode.ProcessExecution).args;
			const selected = explicitArgs[explicitArgs.indexOf('--jit-backend') + 1];
			assert.ok(selected);
			assert.equal(path.relative(path.dirname(module), selected), '');
			assert.equal(await execute(explicit), 0);
			const cleared = await resolve({ jitBackendPath: '' });
			assert.equal((cleared.execution as vscode.ProcessExecution).args.includes('--jit-backend'), false);
			assert.notEqual(await execute(cleared), 0, 'forced JIT without a selected backend fails');
			const check = await resolve({ action: 'check', jitBackendPath: 'missing backend' });
			assert.equal((check.execution as vscode.ProcessExecution).args.includes('--jit-backend'), false);
			assert.equal(await execute(check), 0, 'checking does not load a JIT module');
		}
		finally
		{
			await settings.update('run.jitBackendPath', previous, vscode.ConfigurationTarget.WorkspaceFolder);
			await settings.update('run.backend', previousBackend, vscode.ConfigurationTarget.WorkspaceFolder);
			await fs.unlink(uri.fsPath);
		}
	});

	test('launches native DAP, evaluates variables, steps, and terminates cleanly', async () => {
		messages.length = 0;
		const terminated = new Promise<void>(resolve => {
			const disposable = vscode.debug.onDidTerminateDebugSession(session => {
				if (session.name === 'VeroScript integration') { disposable.dispose(); resolve(); }
			});
		});
		const breakpoint = new vscode.SourceBreakpoint(new vscode.Location(program, new vscode.Position(8, 0)), true, 'answer == 42');
		vscode.debug.addBreakpoints([breakpoint]);
		assert.ok(await vscode.debug.startDebugging(vscode.workspace.getWorkspaceFolder(program), { name: 'VeroScript integration', type: 'veroscript', request: 'launch', program: program.fsPath, stopOnEntry: true }));
		await eventually(() => messages, events => events.some(event => event.event === 'stopped'), 'DAP entry stop');
		const debug = vscode.debug.activeDebugSession;
		assert.ok(debug);
		const stack = await debug.customRequest('stackTrace', { threadId: 1 }) as { stackFrames: { id: number; line: number }[] };
		assert.ok(stack.stackFrames.length);
		const evaluation = await debug.customRequest('evaluate', { frameId: stack.stackFrames[0]!.id, expression: '20 + 22', context: 'watch' }) as { result: string };
		assert.equal(evaluation.result, '42');
		const entryScopes = await debug.customRequest('scopes', { frameId: stack.stackFrames[0]!.id }) as { scopes: { variablesReference: number }[] };
		assert.ok(entryScopes.scopes[0]?.variablesReference);
		const stopped = messages.filter(message => message.event === 'stopped').length;
		await debug.customRequest('next', { threadId: 1 });
		await eventually(() => messages, events => events.filter(event => event.event === 'stopped').length > stopped, 'DAP next');
		const nextStack = await debug.customRequest('stackTrace', { threadId: 1 }) as { stackFrames: { id: number }[] };
		assert.notEqual(nextStack.stackFrames[0]!.id, stack.stackFrames[0]!.id, 'frame identities change across stops');
		await assert.rejects(async () => debug.customRequest('scopes', { frameId: stack.stackFrames[0]!.id }));
		await assert.rejects(async () => debug.customRequest('variables', { variablesReference: entryScopes.scopes[0]!.variablesReference }));
		const scopes = await debug.customRequest('scopes', { frameId: nextStack.stackFrames[0]!.id }) as { scopes: { variablesReference: number }[] };
		let localReference: number | undefined;
		for (const scope of scopes.scopes) {
			const variables = await debug.customRequest('variables', { variablesReference: scope.variablesReference }) as { variables: { name: string; value: string }[] };
			if (variables.variables.some(variable => variable.name === 'answer')) {
				localReference = scope.variablesReference;
			}
		}
		assert.ok(localReference, 'local answer is visible after assignment');
		const changed = await debug.customRequest('setVariable', { variablesReference: localReference, name: 'answer', value: '43' }) as { value: string };
		assert.equal(changed.value, '43');
		const defaultFrame = await debug.customRequest('evaluate', { expression: 'answer', context: 'repl' }) as { result: string };
		assert.equal(defaultFrame.result, '43', 'evaluation without frameId uses the current stop after stepping');
		await debug.customRequest('setVariable', { variablesReference: localReference, name: 'answer', value: '42' });
		await debug.customRequest('continue', { threadId: 1 });
		await eventually(() => messages, events => events.some(event => event.event === 'stopped' && event.body?.reason === 'breakpoint') || events.some(event => event.event === 'terminated'), 'breakpoint or exit');
		if (!messages.some(message => message.event === 'terminated')) {
			await debug.customRequest('continue', { threadId: 1 });
		}
		await terminated;
		assert.ok(messages.some(message => message.event === 'output' && message.body?.output?.includes('42')));
		vscode.debug.removeBreakpoints([breakpoint]);
	});

	test('preserves native memory metadata, readonly inspection and stop-scoped handles', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'memory debug 中文.vs'));
		await fs.writeFile(uri.fsPath, 'void main() {\n    auto bytes = new std::memory::buffer(8);\n'
			+ '    auto view = bytes.readonly_slice(0, 4);\n    print(bytes.size());\n    print(view.size());\n}\n');
		const breakpoint = new vscode.SourceBreakpoint(new vscode.Location(uri, new vscode.Position(3, 0)));
		vscode.debug.addBreakpoints([breakpoint]);
		messages.length = 0;
		let debug: vscode.DebugSession | undefined;
		try {
			assert.ok(await vscode.debug.startDebugging(vscode.workspace.getWorkspaceFolder(uri), {
				name: 'VeroScript memory integration', type: 'veroscript', request: 'launch', program: uri.fsPath
			}));
			await eventually(() => messages, events => events.some(event => event.event === 'stopped'), 'memory breakpoint');
			debug = vscode.debug.activeDebugSession;
			assert.ok(debug);
			const stack = await debug.customRequest('stackTrace', { threadId: 1 });
			const scopes = await debug.customRequest('scopes', { frameId: stack.stackFrames[0].id });
			const locals = await debug.customRequest('variables', { variablesReference: scopes.scopes[0].variablesReference }) as { variables: DapVariable[] };
			for (const [name, length] of [['bytes', '8'], ['view', '4']]) {
				const variable = locals.variables.find(item => item.name === name);
				assert.ok(variable?.veroMemory && variable.variablesReference > 0, name);
				assert.equal(variable.veroMemory.byteLength, length);
				for (const value of Object.values(variable.veroMemory)) {
					assert.equal(typeof value, 'string');
					assert.match(value, /^\d+$/);
				}
				if (variable.memoryReference !== undefined) {
					assert.equal(typeof variable.memoryReference, 'string');
					assert.ok(BigInt(variable.memoryReference) > 0n);
				}
				if (name === 'bytes') {
					assert.ok(BigInt(variable.veroMemory.objectId!) > 0n);
				}
				else
				{
					assert.equal(variable.veroMemory.objectId, undefined, 'views have no independent object identity');
				}
				const children = await debug.customRequest('variables', { variablesReference: variable.variablesReference }) as { variables: DapVariable[] };
				assert.ok(children.variables.some(item => item.name === '[byte_length]' && item.value === length));
				for (const child of children.variables) {
					assert.equal(child.variablesReference, 0);
					assert.equal(child.memoryReference, undefined);
					assert.equal(child.veroMemory, undefined);
				}
				await assert.rejects(Promise.resolve(debug.customRequest('setVariable', { variablesReference: variable.variablesReference, name: '[byte_length]', value: '0' })));
			}
			const stopped = messages.filter(message => message.event === 'stopped').length;
			await debug.customRequest('next', { threadId: 1 });
			await eventually(() => messages, events => events.filter(event => event.event === 'stopped').length > stopped, 'memory next');
			for (const variable of locals.variables.filter(item => item.name === 'bytes' || item.name === 'view')) {
				await assert.rejects(Promise.resolve(debug.customRequest('variables', { variablesReference: variable.variablesReference })));
			}
		}
		finally
		{
			if (debug && vscode.debug.activeDebugSession?.id === debug.id) {
				await vscode.debug.stopDebugging(debug);
				await eventually(() => vscode.debug.activeDebugSession, active => active?.id !== debug!.id, 'memory debug disconnected');
			}
			vscode.debug.removeBreakpoints([breakpoint]);
			await fs.unlink(uri.fsPath);
		}
	});

	test('preserves native readonly variant payloads and watch children without disabling mutable locals', async () => {
		const uri = vscode.Uri.file(path.join(rootA, 'readonly variant debug.vs'));
		const source = 'struct Item { int value = 7; };\nvoid main()\n{\n'
			+ '    variant box = Item();\n    int ready = 0;\n    print(ready);\n}\n';
		await fs.writeFile(uri.fsPath, source);
		const breakpoint = new vscode.SourceBreakpoint(new vscode.Location(uri, new vscode.Position(5, 0)));
		vscode.debug.addBreakpoints([breakpoint]);
		messages.length = 0;
		let debug: vscode.DebugSession | undefined;
		try {
			assert.ok(await vscode.debug.startDebugging(vscode.workspace.getWorkspaceFolder(uri), {
				name: 'VeroScript readonly variant integration', type: 'veroscript', request: 'launch', program: uri.fsPath
			}));
			debug = vscode.debug.activeDebugSession;
			assert.ok(debug);
			await eventually(() => messages, events => events.some(event => event.event === 'stopped'), 'readonly variant breakpoint');
			const session = debug;
			const stack = await session.customRequest('stackTrace', { threadId: 1 });
			const frameId = stack.stackFrames[0].id;
			const scopes = await session.customRequest('scopes', { frameId });
			const localReference = scopes.scopes[0].variablesReference;
			const locals = await session.customRequest('variables', { variablesReference: localReference }) as { variables: DapVariable[] };
			const ready = locals.variables.find(variable => variable.name === 'ready');
			const box = locals.variables.find(variable => variable.name === 'box');
			assert.ok(ready && box && box.variablesReference > 0);
			assert.ok(!ready.presentationHint?.attributes?.includes('readOnly'), 'readonly children do not change unrelated local capabilities');
			const inspectBox = async (reference: number) => {
				const children = await session.customRequest('variables', { variablesReference: reference }) as { variables: DapVariable[] };
				assert.deepEqual(children.variables.map(variable => variable.name).sort(), ['$present', '$type', '$value']);
				for (const child of children.variables) {
					assert.ok(child.presentationHint?.attributes?.includes('readOnly'), child.name);
				}
				const payload = children.variables.find(variable => variable.name === '$value');
				assert.ok(payload && payload.variablesReference > 0);
				const fields = await session.customRequest('variables', { variablesReference: payload.variablesReference }) as { variables: DapVariable[] };
				assert.equal(fields.variables.length, 1);
				assert.equal(fields.variables[0]?.name, 'value');
				assert.equal(fields.variables[0]?.value, '7');
				assert.ok(fields.variables[0]?.presentationHint?.attributes?.includes('readOnly'));
				await assert.rejects(Promise.resolve(session.customRequest('setVariable', {
					variablesReference: payload.variablesReference, name: 'value', value: '9'
				})));
				const unchanged = await session.customRequest('variables', { variablesReference: payload.variablesReference }) as { variables: DapVariable[] };
				assert.equal(unchanged.variables[0]?.value, '7');
			};
			await inspectBox(box.variablesReference);
			const watch = await session.customRequest('evaluate', { expression: 'box', frameId, context: 'watch' });
			assert.ok(watch.variablesReference > 0);
			await inspectBox(watch.variablesReference);
			const changed = await session.customRequest('setVariable', { variablesReference: localReference, name: 'ready', value: '1' });
			assert.equal(changed.value, '1', 'mutable locals remain editable');
		}
		finally
		{
			if (debug && vscode.debug.activeDebugSession?.id === debug.id) {
				await vscode.debug.stopDebugging(debug);
				await eventually(() => vscode.debug.activeDebugSession, active => active?.id !== debug!.id, 'readonly variant debug disconnected');
			}
			vscode.debug.removeBreakpoints([breakpoint]);
			await fs.unlink(uri.fsPath);
		}
	});

	test('offers host configurations, validates connection inputs and cancels without starting debug', async () => {
		const output = vscode.window.createOutputChannel('VeroScript host input test', { log: true });
		resources.push(output);
		const prompts: vscode.InputBoxOptions[] = [];
		const provider = new VeroDebugger(new Services(output), async options => {
			prompts.push(options!);
			return undefined;
		});
		const initial = provider.provideDebugConfigurations();
		assert.deepEqual(initial.map(config => config.request), ['launch', 'attach', 'attach']);
		assert.equal(initial[1]!.host, '127.0.0.1');
		assert.equal(initial[1]!.port, 4711);
		assert.equal(initial[1]!.stopOnEntry, true);
		assert.equal(initial[2]!.attachMode, 'domain');
		assert.equal(initial[2]!.program, undefined);
		const manifest = (await extensionUnderTest()).packageJSON.contributes.debuggers[0];
		assert.ok(manifest.initialConfigurations.some((config: vscode.DebugConfiguration) => config.request === 'attach'));
		assert.ok(manifest.configurationSnippets.some((snippet: { body: vscode.DebugConfiguration }) => snippet.body.request === 'attach'));
		assert.equal(await provider.attachToHost(program), undefined);
		assert.equal(prompts.length, 1);
		for (const host of ['localhost', '127.0.0.1', '::1', '2001:db8::1', '  localhost  ']) {
			assert.equal(await prompts[0]!.validateInput!(host), undefined, host);
		}
		for (const host of ['', ' ', 'two hosts', 'localhost:4711', 'tcp://localhost', '[::1]', '\0']) {
			assert.ok(await prompts[0]!.validateInput!(host), host);
		}
		const portPrompts: vscode.InputBoxOptions[] = [];
		const portProvider = new VeroDebugger(new Services(output), async options => {
			portPrompts.push(options!);
			return portPrompts.length === 1 ? 'localhost' : undefined;
		});
		assert.equal(await portProvider.attachToHost(program), undefined);
		assert.equal(portPrompts.length, 2);
		for (const port of ['1', '4711', '65535', ' 4711 ']) {
			assert.equal(await portPrompts[1]!.validateInput!(port), undefined, port);
		}
		for (const port of ['', '0', '-1', '65536', '1.5', '1e3', '4711junk']) {
			assert.ok(await portPrompts[1]!.validateInput!(port), port);
		}
		assert.equal(vscode.debug.activeDebugSession, undefined);
	});

	test('starts the host command workflow against a real external TCP DAP session', async () => {
		const server = net.createServer();
		await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
		const port = (server.address() as net.AddressInfo).port;
		await new Promise<void>((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
		const cli = path.join(process.env.VEROSCRIPT_TEST_RUNTIME!, process.platform === 'win32' ? 'veroscript.exe' : 'veroscript');
		externalHost = spawn(cli, ['--debug', `--dap-port=${port}`, program.fsPath], { stdio: 'pipe', windowsHide: true });
		let log = '';
		externalHost.stderr?.on('data', data => { log += String(data); });
		externalHost.stdout?.on('data', data => { log += String(data); });
		// The host prints its listening state before accepting a single DAP connection.
		await eventually(() => log, value => /listen|DAP|TCP|port/i.test(value), 'TCP host ready');
		messages.length = 0;
		const output = vscode.window.createOutputChannel('VeroScript host workflow test', { log: true });
		resources.push(output);
		const answers = [' 127.0.0.1 ', String(port)];
		const provider = new VeroDebugger(new Services(output), async () => answers.shift());
		assert.ok(await provider.attachToHost(program));
		await eventually(() => messages, events => events.some(event => event.event === 'stopped'), 'attach stop');
		assert.ok(!('cwd' in vscode.debug.activeDebugSession!.configuration));
		assert.ok(!('_veroscriptRuntimeRoot' in vscode.debug.activeDebugSession!.configuration));
		const session = vscode.debug.activeDebugSession!;
		assert.equal(session.configuration.host, '127.0.0.1');
		const stack = await session.customRequest('stackTrace', { threadId: 1 }) as { stackFrames: { id: number }[] };
		const evaluated = await session.customRequest('evaluate', { frameId: stack.stackFrames[0]!.id, expression: '20 + 22', context: 'watch' }) as { result: string };
		assert.equal(evaluated.result, '42');
		const disconnected = new Promise<void>(resolve => {
			const listener = vscode.debug.onDidTerminateDebugSession(ended => {
				if (ended.id === session.id) {
					listener.dispose();
					resolve();
				}
			});
		});
		await vscode.debug.stopDebugging(vscode.debug.activeDebugSession);
		await disconnected;
		externalHost.kill();
		externalHost = undefined;
	});

	test('normalizes attach without inheriting launch compile settings', async () => {
		const output = vscode.window.createOutputChannel('VeroScript configuration test', { log: true });
		resources.push(output);
		const provider = new VeroDebugger(new Services(output));
		const uri = vscode.Uri.file(path.join(rootB, 'other.vs'));
		const settings = vscode.workspace.getConfiguration('veroscript', uri);
		await settings.update('runtimePath', '${env:VEROSCRIPT_TEST_MISSING_RUNTIME}', vscode.ConfigurationTarget.WorkspaceFolder);
		try {
			const config = await provider.resolveDebugConfigurationWithSubstitutedVariables(undefined, { type: 'veroscript', name: 'Attach config', request: 'attach', program: uri.fsPath, port: 4711 });
			assert.ok(config);
			assert.ok(!('stdlibSearchPaths' in config));
			assert.ok(!('env' in config));
			const before = await provider.resolveDebugConfiguration(undefined, { type: 'veroscript', name: 'Attach config', request: 'attach', program: uri.fsPath, port: 4711 });
			assert.ok(before, 'the save phase does not resolve a local Runtime either');
			const explicit = await provider.resolveDebugConfigurationWithSubstitutedVariables(vscode.workspace.getWorkspaceFolder(uri), {
				type: 'veroscript', name: 'Attach explicit', request: 'attach', program: uri.fsPath, host: ' ::1 ', port: 4711,
				importPaths: ['shared'], defines: [], allowCircularImports: false
			});
			assert.ok(explicit);
			assert.equal(explicit.host, '::1');
			assert.deepEqual(explicit.importPaths, [path.join(vscode.workspace.getWorkspaceFolder(uri)!.uri.fsPath, 'shared')]);
			assert.deepEqual(explicit.defines, []);
			assert.equal(explicit.allowCircularImports, false);
			for (const key of ['stdlibSearchPaths', 'bindingCatalogPaths', 'predefinedMacros', 'env', 'cwd', 'workspace', '_veroscriptRuntimeRoot']) {
				assert.ok(!(key in explicit), key);
			}
		}
		finally
		{
			await settings.update('runtimePath', undefined, vscode.ConfigurationTarget.WorkspaceFolder);
		}
	});

	test('disables and restores only the affected language server', async () => {
		const uri = vscode.Uri.file(path.join(rootB, 'other.vs'));
		const settings = vscode.workspace.getConfiguration('veroscript', uri);
		await settings.update('lsp.enabled', false, vscode.ConfigurationTarget.WorkspaceFolder);
		await eventually(() => api.snapshot(), states => states.length === 3 && states.every(state => state.state === 'Running'), 'one LSP disabled');
		await settings.update('lsp.enabled', true, vscode.ConfigurationTarget.WorkspaceFolder);
		await eventually(() => api.snapshot(), states => states.length === 4 && states.every(state => state.state === 'Running'), 'one LSP restored');
	});

	test('finishes an in-flight document close before disposing its language client', async () => {
		const client = await api.ready(program);
		assert.ok(client);
		// Exercise the SDK callback directly: editor models can remain cached after tab closure.
		const closing = client.getFeature('textDocument/didClose').callback(documents[0]!);
		try {
			await Promise.all([closing, client.dispose()]);
		}
		finally
		{
			await vscode.commands.executeCommand('veroscript.restartLanguageServer');
			await api.ready(program);
		}
		const tokens = await vscode.commands.executeCommand<vscode.SemanticTokens>('vscode.provideDocumentSemanticTokens', program);
		assert.ok(tokens?.data.length, 'the reopened client restores document synchronization and providers');
	});

	test('recovers an unexpected native server exit and restores owned providers', async () => {
		const client = await api.ready(program);
		assert.ok(client);
		const states: number[] = [];
		const subscription = client.onDidChangeState(event => states.push(event.newState));
		try {
			await client.sendNotification('exit');
			await eventually(() => states, value => value.includes(1) && value.at(-1) === 2, 'LSP automatically restarted');
			await api.ready(program);
			const tokens = await vscode.commands.executeCommand<vscode.SemanticTokens>('vscode.provideDocumentSemanticTokens', program);
			assert.ok(tokens?.data.length, 'semantic provider restored after native restart');
			assert.equal(api.snapshot().length, 4);
		}
		finally
		{
			subscription.dispose();
		}
	});
});
