// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as path from 'node:path';
import * as vscode from 'vscode';
import { suite, test } from 'mocha';
import { extensionUnderTest } from './host';

suite('VeroScript without a workspace', () => {
	test('edits, runs, checks and debugs a standalone file', async () => {
		assert.equal(vscode.workspace.workspaceFolders, undefined);
		const uri = vscode.Uri.file(path.join(process.env.VEROSCRIPT_TEST_SESSION!, 'outside/outside [x].vs'));
		const document = await vscode.workspace.openTextDocument(uri);
		await vscode.window.showTextDocument(document);
		const extension = await extensionUnderTest<{ ready(uri: vscode.Uri): Promise<unknown> }>();
		const api = await extension.activate();
		await api.ready(uri);
		const tokens = await vscode.commands.executeCommand<vscode.SemanticTokens>('vscode.provideDocumentSemanticTokens', uri);
		assert.ok(tokens?.data.length);
		for (const command of ['checkFile', 'runFile']) {
			const ended = new Promise<number | undefined>(resolve => {
				const listener = vscode.tasks.onDidEndTaskProcess(event => {
					if (event.execution.task.definition.program === uri.fsPath) {
						listener.dispose();
						resolve(event.exitCode);
					}
				});
			});
			const execution = await vscode.commands.executeCommand<vscode.TaskExecution>(`veroscript.${command}`, uri);
			assert.ok(execution);
			assert.equal(await ended, 0, command);
		}
		const terminated = new Promise<void>(resolve => {
			const listener = vscode.debug.onDidTerminateDebugSession(session => {
				if (session.type === 'veroscript') {
					listener.dispose();
					resolve();
				}
			});
		});
		assert.equal(await vscode.commands.executeCommand('veroscript.debugFile', uri), true);
		await terminated;
	});
});
