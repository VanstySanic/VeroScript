// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as path from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import * as vscode from 'vscode';
import { suite, suiteSetup, suiteTeardown, test } from 'mocha';

async function eventually<T>(read: () => T | PromiseLike<T>, accept: (value: T) => boolean, label: string, timeout = 15000): Promise<T> {
	const deadline = Date.now() + timeout;
	while (Date.now() < deadline) {
		const value = await read();
		if (accept(value)) return value;
		await new Promise(resolve => setTimeout(resolve, 50));
	}
	throw new Error(label);
}

const root = process.env.VEROSCRIPT_GUI_TEST_WORKSPACE!;
const page = vscode.Uri.file(path.join(root, 'page 中文.vswml'));
const style = vscode.Uri.file(path.join(root, 'page.vswcss'));
const encode = (value: string) => Buffer.from(value, 'utf8');
const source = '<Stack id="root">\n    <style src="page.vswcss" />\n    <Button Tex />\n</Stack>\n';
// Match the native GUI CLI acceptance budget for cold framework compilation.
// Editor providers retain their shorter deadline.
const nativeTaskTimeout = 600000;

suite('VeroScript GUI editor and dependency', () => {
	suiteSetup(async () => {
		for (const file of [process.env.VEROSCRIPT_GUI_TEST_BASE_VSIX, process.env.VEROSCRIPT_GUI_TEST_VSIX]) {
			if (file) {
				await vscode.commands.executeCommand('workbench.extensions.installExtension', vscode.Uri.file(file));
			}
		}
		for (const id of ['VanstySanic.veroscript', 'VanstySanic.veroscript-gui']) {
			const extension = await eventually(() => vscode.extensions.getExtension(id), value => value !== undefined, id);
			assert.ok(extension);
			if (process.env.VEROSCRIPT_GUI_TEST_VSIX) {
				const relative = path.relative(process.env.VEROSCRIPT_GUI_TEST_EXTENSIONS!, extension.extensionPath);
				assert.ok(relative && !relative.startsWith('..') && !path.isAbsolute(relative), 'loads the installed package');
			}
			await extension.activate();
		}
		assert.equal(vscode.extensions.getExtension('VanstySanic.veroscript')!.exports.apiVersion, 1);
		await vscode.workspace.fs.writeFile(page, encode(source));
		await vscode.workspace.fs.writeFile(style, encode('Button { fo }\n'));
	});

	suiteTeardown(async () => {
		await vscode.commands.executeCommand('workbench.action.closeAllEditors');
	});

	test('owns GUI languages without replacing .vs or changing the editor theme', async () => {
		const document = await vscode.workspace.openTextDocument(page);
		assert.equal(document.languageId, 'veroscript-gui-markup');
		assert.equal((await vscode.workspace.openTextDocument(style)).languageId, 'veroscript-gui-style');
		const commands = await vscode.commands.getCommands(true);
		for (const name of ['check', 'build', 'preview', 'selectEntry', 'showOutput']) {
			assert.ok(commands.includes(`veroscriptGui.${name}`));
		}
		const manifest = vscode.extensions.getExtension('VanstySanic.veroscript-gui')!.packageJSON;
		assert.deepEqual(manifest.extensionDependencies, ['VanstySanic.veroscript']);
		assert.ok(!manifest.contributes.languages.some((language: { id: string }) => language.id === 'veroscript'));
		assert.equal(manifest.contributes.configurationDefaults, undefined);
	});

	test('opens both branded extension Details pages with their shipped READMEs', async () => {
		for (const id of ['VanstySanic.veroscript', 'VanstySanic.veroscript-gui']) {
			const extension = vscode.extensions.getExtension(id)!;
			assert.equal(extension.packageJSON.publisher, 'VanstySanic');
			const bytes = await vscode.workspace.fs.readFile(vscode.Uri.joinPath(extension.extensionUri,
				process.env.VEROSCRIPT_GUI_TEST_VSIX ? 'readme.md' : 'README.md'));
			const readme = Buffer.from(bytes).toString('utf8');
			assert.ok(readme.startsWith('# VeroScript ') && readme.includes('Quick start'));
			try {
				await vscode.commands.executeCommand('extension.open', id);
			}
			finally
			{
				await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
			}
		}
	});

	test('offers native properties and style flags, preserves source links and highlights matching tags', async () => {
		const document = await vscode.workspace.openTextDocument(page);
		await vscode.window.showTextDocument(document);
		const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', page,
			document.positionAt(source.indexOf('Tex') + 3));
		assert.ok(completion?.items.some(item => item.label === 'Text'));
		assert.ok(completion?.items.some(item => item.label === 'TextColor'));
		const hover = await vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', page, new vscode.Position(0, 3));
		assert.ok(hover?.some(item => item.contents.some(content => content instanceof vscode.MarkdownString && content.value.includes('VS::Gui::Stack'))));
		const links = await vscode.commands.executeCommand<vscode.DocumentLink[]>('vscode.executeLinkProvider', page);
		assert.ok(links?.some(link => link.target?.fsPath === style.fsPath));
		const highlights = await vscode.commands.executeCommand<vscode.DocumentHighlight[]>('vscode.executeDocumentHighlights', page, new vscode.Position(0, 3));
		assert.equal(highlights?.length, 2);
		const styles = await vscode.workspace.openTextDocument(style);
		const properties = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', style,
			styles.positionAt(styles.getText().indexOf('fo') + 2));
		assert.ok(properties?.items.some(item => item.label === 'font-family'));
		assert.ok(!properties?.items.some(item => item.label === 'Text' || item.label === 'Value'));
	});

	test('publishes UTF-16 structural ranges and preserves markup with embedded code', async () => {
		const uri = vscode.Uri.file(path.join(root, 'structure.vswml'));
		const text = '<Stack Text="😀"></Wrong>';
		await vscode.workspace.fs.writeFile(uri, encode(text));
		await vscode.workspace.openTextDocument(uri);
		const diagnostics = await eventually(() => vscode.languages.getDiagnostics(uri), values => values.some(value => value.code === 'gui.editor.structure'), 'structural diagnostics');
		assert.ok(diagnostics.some(diagnostic => diagnostic.range.start.character === text.indexOf('</Wrong>')));
		await vscode.workspace.fs.delete(uri);
	});

	test('offers composite controls with inherited properties, events and native CSS hints', async () => {
		const complete = async (text: string, language = 'veroscript-gui-markup', offset = text.length) => {
			const document = await vscode.workspace.openTextDocument({ language, content: text });
			return vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', document.uri,
				document.positionAt(offset));
		};
		const tags = await complete('<');
		for (const tag of ['ListView', 'list-item', 'tree-view', 'TreeItem', 'table', 'TableColumn', 'table-row',
			'combo-box', 'TabView', 'tab', 'Menu', 'menu-item', 'Dialog']) {
			assert.ok(tags?.items.some(item => item.label === tag), tag);
		}
		for (const [text, label, type] of [
			['<list-view Se', 'SelectedKey', 'string'],
			['<ListItem Ke', 'Key', 'string'],
			['<table-row Se', 'SelectionColor', 'Color'],
			['<tree-item on:ex', 'on:expanded-changed', 'VS::Gui::TreeItem, bool'],
			['<table on:so', 'on:sorted', 'VS::Gui::TableView, string, bool'],
			['<dialog on:op', 'on:open-changed', 'VS::Gui::Popup, bool'],
			['<dialog on:cl', 'on:closed', 'VS::Gui::Dialog, string']
		] as const) {
			const items = await complete(text);
			const item = items?.items.find(item => item.label === label);
			assert.ok(item?.detail?.includes(type), text);
		}
		const duplicated = await complete('<ListView selected-key="first" ');
		assert.ok(!duplicated?.items.some(item => ['SelectedKey', 'selected-key', 'bind:SelectedKey', 'bind:selected-key'].includes(String(item.label))));
		const placement = '<Dialog placement="" />';
		const values = await complete(placement, 'veroscript-gui-markup', placement.indexOf('"') + 1);
		for (const value of ['below', 'right', 'center', 'Below', 'Right', 'Center']) {
			assert.ok(values?.items.some(item => item.label === value), value);
		}
		for (const [text, label] of [['TableRow { sel', 'selection-color'], ['TableColumn { col', 'column-width'],
			['ComboBox { drop', 'drop-down-height']] as const) {
			const items = await complete(text, 'veroscript-gui-style');
			assert.ok(items?.items.some(item => item.label === label), text);
		}
		const text = '<ListView selected-key="first" />';
		const document = await vscode.workspace.openTextDocument({ language: 'veroscript-gui-markup', content: text });
		const hover = await vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', document.uri,
			document.positionAt(text.indexOf('selected-key') + 2));
		assert.ok(hover?.some(item => item.contents.some(content => content instanceof vscode.MarkdownString && content.value.includes('string'))));
	});

	test('edits selection colors for collection rows and menu items', async () => {
		const text = '<Stack><ListItem SelectionColor="#336699" /><MenuItem selection-color="#11223380" /></Stack>';
		const document = await vscode.workspace.openTextDocument({ language: 'veroscript-gui-markup', content: text });
		const colors = await vscode.commands.executeCommand<vscode.ColorInformation[]>('vscode.executeDocumentColorProvider', document.uri);
		assert.equal(colors?.length, 2);
		assert.deepEqual(colors?.map(color => document.getText(color.range)), ['#336699', '#11223380']);
		assert.equal(colors?.[0]?.color.alpha, 1);
		assert.equal(colors?.[1]?.color.alpha, 128 / 255);
	});

	test('keeps markup completion inside headers and preserves spaced assignments', async () => {
		const complete = await vscode.workspace.openTextDocument({ language: 'veroscript-gui-markup', content: '<Button/>' });
		const outside = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', complete.uri,
			complete.positionAt(complete.getText().length));
		assert.equal(outside?.items.length ?? 0, 0, JSON.stringify(outside?.items.map(item => ({ label: item.label, kind: item.kind }))));
		const text = '<Button Text = "Hello" ';
		const document = await vscode.workspace.openTextDocument({ language: 'veroscript-gui-markup', content: text });
		const edit = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', document.uri,
			document.positionAt(text.indexOf('Text') + 3));
		assert.equal(edit?.items.find(item => item.label === 'Text')?.insertText, 'Text');
		const next = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', document.uri,
			document.positionAt(text.lastIndexOf('"') + 1));
		assert.ok(!next?.items.some(item => item.label === 'Text' || item.label === 'text' || item.label === 'bind:Text'));
	});

	test('completes unfinished styles and replaces numeric values without duplicating digits', async () => {
		const complete = async (text: string, offset = text.length) => {
			const document = await vscode.workspace.openTextDocument({ language: 'veroscript-gui-style', content: text });
			return vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', document.uri, document.positionAt(offset));
		};
		const properties = await complete('Button { fo');
		assert.ok(properties?.items.some(item => item.label === 'font-family'));
		const source = 'Button { width: 8';
		const values = await complete(source);
		const value = values?.items.find(item => item.label === '8px');
		assert.ok(value?.range instanceof vscode.Range);
		assert.equal(value.range.start.character, source.length - 1);
		assert.equal(value.range.end.character, source.length);
		assert.equal(value.insertText, '8px');
		const spaced = 'Button { color : #123456 }';
		const assignment = await complete(spaced, spaced.indexOf('color') + 4);
		assert.equal(assignment?.items.find(item => item.label === 'color')?.insertText, 'color');
		for (const text of ['#', '.', '.name']) {
			const suggestions = await complete(text);
			assert.equal(suggestions?.items.length ?? 0, 0, JSON.stringify(suggestions?.items.map(item => ({ label: item.label, kind: item.kind }))));
		}
		const empty = await vscode.workspace.openTextDocument({ language: 'veroscript-gui-style', content: '{' });
		const symbols = await vscode.commands.executeCommand<unknown[]>('vscode.executeDocumentSymbolProvider', empty.uri);
		assert.equal(symbols?.length ?? 0, 0);
	});

	test('resolves escaped and uppercase native CSS URLs without linking quoted fake URLs', async () => {
		const uri = vscode.Uri.file(path.join(root, 'images.vswcss'));
		const text = 'Widget { background-image: URL("icons/image.png"); --label: "url(fake.png)"; }';
		await vscode.workspace.fs.writeFile(uri, encode(text));
		await vscode.workspace.openTextDocument(uri);
		const links = await vscode.commands.executeCommand<vscode.DocumentLink[]>('vscode.executeLinkProvider', uri);
		assert.equal(links?.length, 1);
		assert.equal(links[0]?.target?.fsPath, vscode.Uri.file(path.join(root, 'icons/image.png')).fsPath);
	});

	test('does not present property metadata as inline expression semantics or pair mismatched tags', async () => {
		const uri = vscode.Uri.file(path.join(root, 'bindings.vswml'));
		const text = '<Stack><Button bind:Width="{ Width }"></Stack>';
		await vscode.workspace.fs.writeFile(uri, encode(text));
		const document = await vscode.workspace.openTextDocument(uri);
		const expression = document.positionAt(text.indexOf('{ Width') + 3);
		const hover = await vscode.commands.executeCommand<vscode.Hover[]>('vscode.executeHoverProvider', uri, expression);
		assert.equal(hover?.length ?? 0, 0);
		const highlights = await vscode.commands.executeCommand<vscode.DocumentHighlight[]>('vscode.executeDocumentHighlights', uri,
			document.positionAt(text.indexOf('Button') + 2));
		assert.equal(highlights?.length, 1);
	});

	test('discovers no tasks or native processes before an execution request', async () => {
		assert.deepEqual(await vscode.tasks.fetchTasks({ type: 'veroscript-gui' }), []);
		assert.deepEqual(vscode.extensions.getExtension('VanstySanic.veroscript')!.exports.snapshot(), []);
	});

	test('provides builtin editing hints before a markup document is saved', async () => {
		const document = await vscode.workspace.openTextDocument({ language: 'veroscript-gui-markup', content: '<Button Tex' });
		const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', document.uri,
			document.positionAt(document.getText().length));
		assert.ok(completion?.items.some(item => item.label === 'Text'));
	});

	test('keeps ordinary custom tag names distinct from JavaScript object members', async () => {
		for (const name of ['constructor', 'toString']) {
			const document = await vscode.workspace.openTextDocument({ language: 'veroscript-gui-markup', content: `<${name} Wid` });
			const completion = await vscode.commands.executeCommand<vscode.CompletionList>('vscode.executeCompletionItemProvider', document.uri,
				document.positionAt(document.getText().length));
			assert.ok(completion?.items.some(item => item.label === 'Width'), name);
		}
	});

	test('resolves a configured GUI task through the branded base tools API', async () => {
		await vscode.workspace.fs.writeFile(vscode.Uri.joinPath(vscode.workspace.workspaceFolders![0]!.uri, '.vscode/tasks.json'),
			encode(JSON.stringify({ version: '2.0.0', tasks: [{ label: 'Resolve GUI check', type: 'veroscript-gui', action: 'check', input: page.fsPath }] })));
		const tasks = await eventually(() => vscode.tasks.fetchTasks({ type: 'veroscript-gui' }),
			values => values.some(task => task.definition.input === page.fsPath), 'branded GUI task resolution');
		const task = tasks.find(task => task.definition.input === page.fsPath)!;
		assert.ok(task.execution instanceof vscode.ProcessExecution);
		assert.ok(task.execution.args.includes('gui-check'));
		assert.ok(task.execution.args.includes(page.fsPath));
	});

	test('checks and compiles a real GUI entry through the installed dependency API', async function () {
		if (process.env.VEROSCRIPT_GUI_TEST_NATIVE !== '1') {
			this.skip();
		}
		const input = vscode.Uri.file(path.join(root, 'native.vswml'));
		const artifact = vscode.Uri.file(path.join(root, 'native.vsapp'));
		await vscode.workspace.fs.writeFile(input, encode('<Stack><Text Text="Hello, GUI!" /></Stack>'));
		const actions = process.env.VEROSCRIPT_GUI_TEST_PREVIEW === '1' ? ['check', 'compile', 'preview'] as const : ['check', 'compile'] as const;
		this.timeout(actions.length * nativeTaskTimeout + 30000);
		await vscode.workspace.fs.writeFile(vscode.Uri.joinPath(vscode.workspace.workspaceFolders![0]!.uri, '.vscode/tasks.json'),
			encode(JSON.stringify({ version: '2.0.0', tasks: actions.map(action => ({
				label: `GUI ${action}`, type: 'veroscript-gui', action, input: input.fsPath, output: artifact.fsPath, frames: 2
			})) })));
		const configured = await eventually(() => vscode.tasks.fetchTasks({ type: 'veroscript-gui' }),
			tasks => actions.every(action => tasks.some(task => task.definition.input === input.fsPath && task.definition.action === action)),
			'installed GUI task provider');
		const compile = configured.find(task => task.definition.input === input.fsPath && task.definition.action === 'compile');
		assert.ok(compile?.execution instanceof vscode.ProcessExecution);
		const prefix = compile.execution.args.slice(0, compile.execution.args.indexOf('gui-compile'));
		// Validate standalone loading and reuse of the selected source environment.
		const artifactCommands = [
			['package', 'inspect', artifact.fsPath],
			[...prefix, 'gui-check', artifact.fsPath],
			['--backend=vm', '--check', artifact.fsPath],
			[...prefix, '--check', artifact.fsPath]
		];
		this.timeout((actions.length + artifactCommands.length) * nativeTaskTimeout + 30000);
		for (const action of actions) {
			const resolved = configured.find(task => task.definition.input === input.fsPath && task.definition.action === action);
			assert.ok(resolved);
			assert.ok(resolved.execution instanceof vscode.ProcessExecution);
			console.log(`GUI ${action} process: ${JSON.stringify({ executable: resolved.execution.process, args: resolved.execution.args, cwd: resolved.execution.options?.cwd })}`);
			const started = Date.now();
			let result: vscode.TaskProcessEndEvent | undefined;
			const listener = vscode.tasks.onDidEndTaskProcess(event => {
				const definition = event.execution.task.definition;
				if (definition.type === 'veroscript-gui' && definition.input === input.fsPath && definition.action === action) {
					result = event;
				}
			});
			let execution: vscode.TaskExecution | undefined;
			try {
				execution = await vscode.tasks.executeTask(resolved);
				const ended = await eventually(() => result, value => value !== undefined, `GUI ${action}`, nativeTaskTimeout);
				console.log(`GUI ${action} exited with ${ended?.exitCode} after ${Date.now() - started} ms.`);
				assert.equal(ended?.exitCode, 0);
				const nativeExecution = ended?.execution.task.execution;
				assert.ok(nativeExecution instanceof vscode.ProcessExecution);
				assert.ok(nativeExecution.args.includes(`gui-${action}`));
			}
			finally
			{
				if (!result) {
					execution?.terminate();
				}
				listener.dispose();
			}
		}
		assert.ok((await vscode.workspace.fs.stat(artifact)).size > 0);
		const bytes = await vscode.workspace.fs.readFile(artifact);
		assert.deepEqual([...bytes.slice(0, 4)], [0x50, 0x4b, 0x03, 0x04], 'GUI output is a standard application ZIP');
		for (const commandArgs of artifactCommands) {
			await promisify(execFile)(compile.execution.process, commandArgs, {
				cwd: compile.execution.options?.cwd || root, env: { ...process.env, ...compile.execution.options?.env },
				timeout: nativeTaskTimeout, windowsHide: true
			});
		}
	});
});
