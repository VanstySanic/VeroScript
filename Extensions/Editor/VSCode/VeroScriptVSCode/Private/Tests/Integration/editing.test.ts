// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as vscode from 'vscode';
import { suite, test, suiteSetup, setup, teardown } from 'mocha';
import { extensionUnderTest } from './host';

suite('VeroScript local Enter editing', () => {
	let document: vscode.TextDocument;
	let editor: vscode.TextEditor;
	suiteSetup(async () => { await (await extensionUnderTest()).activate(); });
	setup(async () => {
		document = await vscode.workspace.openTextDocument({ language: 'veroscript', content: '' });
		editor = await vscode.window.showTextDocument(document);
		editor.options = { tabSize: 4, insertSpaces: true };
	});
	teardown(async () => {
		await vscode.workspace.getConfiguration('veroscript', document).update('bracesOnNewLine', undefined, vscode.ConfigurationTarget.Global);
		await vscode.window.showTextDocument(document);
		await editor.edit(edit => edit.delete(new vscode.Range(0, 0, document.lineCount, 0)));
		await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
	});
	const set = async (marked: string, eol = vscode.EndOfLine.LF) => {
		await editor.edit(edit => {
			edit.setEndOfLine(eol);
			edit.replace(new vscode.Range(0, 0, document.lineCount, 0), marked.replaceAll('|', ''));
		});
		const offsets = [...marked.matchAll(/\|/g)].map((match, index) => match.index - index);
		editor.selections = offsets.map(offset => new vscode.Selection(document.positionAt(offset), document.positionAt(offset)));
		// Rapid fixture replacements must settle TextMate state before comparing
		// native Enter indentation in multiline comments/continued directives.
		await vscode.commands.executeCommand('editor.action.forceRetokenize');
	};
	const verify = (marked: string) => {
		assert.equal(document.getText(), marked.replaceAll('|', ''));
		const offsets = [...marked.matchAll(/\|/g)].map((match, index) => match.index - index);
		assert.deepEqual(editor.selections.map(selection => document.offsetAt(selection.active)), offsets);
		assert.ok(editor.selections.every(selection => selection.isEmpty));
	};

	test('lays out empty declaration and control blocks with native indentation, carets and undo', async () => {
		for (const header of ['void a', 'void main()', 'class Demo', 'enum class Color', 'namespace demo', 'if (ready)', 'for (int i = 0; i < 3; ++i)', 'else', 'try', 'catch (any error)', 'int value() const']) {
			await set(`    ${header} {|}`);
			await vscode.commands.executeCommand('veroscript.insertNewline');
			const expected = `    ${header}\n    {\n        |\n    }`;
			verify(expected);
			await vscode.commands.executeCommand('undo');
			verify(`    ${header} {|}`);
			await vscode.commands.executeCommand('redo');
			verify(expected);
			await vscode.commands.executeCommand('leaveSnippet');
		}
		await set('void main() {|');
		await vscode.commands.executeCommand('veroscript.insertNewline');
		verify('void main()\n{\n    |');
		for (const prefix of ['/* earlier comment */\n', 'auto text = r"first\nlast";\n']) {
			await set(prefix + 'void a{ | }');
			await vscode.commands.executeCommand('veroscript.insertNewline');
			verify(prefix + 'void a\n{\n    |\n}');
		}
	});

	test('preserves CRLF, tab indentation and multiple independent cursors', async () => {
		editor.options = { tabSize: 4, insertSpaces: false };
		await set('\tvoid a(){|}\r\n\tvoid b(){|}', vscode.EndOfLine.CRLF);
		await vscode.commands.executeCommand('veroscript.insertNewline');
		verify('\tvoid a()\r\n\t{\r\n\t\t|\r\n\t}\r\n\tvoid b()\r\n\t{\r\n\t\t|\r\n\t}');
		await vscode.commands.executeCommand('undo');
		verify('\tvoid a(){|}\r\n\tvoid b(){|}');
	});

	test('publishes one window-scoped switch and returns to layout when re-enabled', async () => {
		const extension = await extensionUnderTest();
		const contributions = extension.packageJSON.contributes;
		const setting = contributions.configuration.properties['veroscript.bracesOnNewLine'];
		assert.equal(setting.scope, 'window', 'config.* keybinding contexts read window configuration');
		assert.equal(setting.default, true);
		assert.equal(setting.type, 'boolean');
		const binding = contributions.keybindings.find((entry: { command: string }) => entry.command === 'veroscript.insertNewline');
		for (const condition of ['config.veroscript.bracesOnNewLine', '!suggestWidgetVisible', '!inSnippetMode', '!inlineSuggestionVisible', '!renameInputVisible', '!inComposition', '!editorReadonly']) {
			assert.ok(binding.when.includes(condition), condition);
		}
		const configuration = vscode.workspace.getConfiguration('veroscript');
		await configuration.update('bracesOnNewLine', false, vscode.ConfigurationTarget.Global);
		assert.equal(vscode.workspace.getConfiguration('veroscript').get('bracesOnNewLine'), false);
		await set('void a{|}');
		await vscode.commands.executeCommand('type', { text: '\n' });
		const expected = document.getText();
		await set('void a{|}');
		await vscode.commands.executeCommand('veroscript.insertNewline');
		assert.equal(document.getText(), expected);
		await configuration.update('bracesOnNewLine', true, vscode.ConfigurationTarget.Global);
		await set('void a{|}');
		await vscode.commands.executeCommand('veroscript.insertNewline');
		verify('void a\n{\n    |\n}');
	});

	test('uses ordinary Enter for mixed cursor contexts and preserves selected text behavior', async () => {
		for (const marked of ['void a(){|}\n    void b(){|}', 'void a(){|}\n// comment|', 'void a(){|}\nvoid b(){|']) {
			await set(marked);
			await vscode.commands.executeCommand('type', { text: '\n' });
			const expected = document.getText();
			const positions = editor.selections.map(selection => selection.active);
			await set(marked);
			await vscode.commands.executeCommand('veroscript.insertNewline');
			assert.equal(document.getText(), expected, marked);
			assert.deepEqual(editor.selections.map(selection => selection.active), positions, marked);
		}
		await set('void a{|}');
		editor.selection = new vscode.Selection(0, 5, 0, 6);
		await vscode.commands.executeCommand('veroscript.insertNewline');
		assert.ok(!document.getText().includes('a'));
	});

	test('keeps ordinary Enter for strings, comments, directives, initializers and disabled layout', async () => {
		const cases = ['// void a{|}', '// continued \\\nvoid a{|}', '// continued \\\ncomment \\\nvoid a{|}', '/* comment\nvoid a{|}', 'auto text = r"first\nvoid a{|}',
			'#define BLOCK \\\n#value \\\nvoid a{|}', '#define FIRST \\\n#value\n#define SECOND \\\nvoid a{|}',
			'#define BLOCK void a{|}', '#define BLOCK /*\nvoid a{|}', '#define BLOCK \\\n/*\nvoid a{|}', '#define BLOCK \\\nvoid a{|}', 'array<int> values {|}', 'print("void a{|}");',
			'void main() { print("ready"); if (ready) {|}', 'void main()\n{|}', 'void main() {| print("ready"); }',
			'void main(\n    int value) {|}', 'void main(\n    int value = nested()) {|}',
			'Demo(int value)\n    : base(value) {|}'];
		for (const marked of cases) {
			await set(marked);
			await vscode.commands.executeCommand('type', { text: '\n' });
			const expected = document.getText();
			const position = editor.selection.active;
			await set(marked);
			await vscode.commands.executeCommand('veroscript.insertNewline');
			assert.equal(document.getText(), expected, marked);
			assert.deepEqual(editor.selection.active, position, marked);
		}
		await vscode.workspace.getConfiguration('veroscript', document).update('bracesOnNewLine', false, vscode.ConfigurationTarget.Global);
		assert.equal(vscode.workspace.getConfiguration('veroscript', document).get('bracesOnNewLine'), false);
		await set('void a{|}');
		await vscode.commands.executeCommand('type', { text: '\n' });
		const expected = document.getText();
		const position = editor.selection.active;
		await set('void a{|}');
		await vscode.commands.executeCommand('veroscript.insertNewline');
		assert.equal(document.getText(), expected);
		assert.deepEqual(editor.selection.active, position);
	});
});
