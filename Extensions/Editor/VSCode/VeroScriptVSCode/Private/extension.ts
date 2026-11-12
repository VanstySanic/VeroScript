// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as vscode from 'vscode';
import { targetUri, Services } from './context';
import { VeroDebugger } from './debugger';
import { registerEditing } from './editing';
import { LanguageServers } from './languageClient';
import { registerSnippets } from './snippets';
import { VeroTasks } from './tasks';
import { toolchain } from './toolchain';

export function activate(context: vscode.ExtensionContext) {
	const output = vscode.window.createOutputChannel('VeroScript', { log: true });
	const status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 10);
	const services = new Services(output);
	const languages = new LanguageServers(services, status);
	languageServers = languages;
	const tasks = new VeroTasks(services);
	const debuggerProvider = new VeroDebugger(services);
	context.subscriptions.push(output, status, registerSnippets(context), registerEditing(),
		vscode.tasks.registerTaskProvider('veroscript', tasks),
		vscode.debug.registerDebugConfigurationProvider('veroscript', debuggerProvider),
		vscode.debug.registerDebugAdapterDescriptorFactory('veroscript', debuggerProvider));

	const command = (id: string, action: (argument?: unknown) => unknown) => {
		context.subscriptions.push(vscode.commands.registerCommand(`veroscript.${id}`, async (argument?: unknown) => {
			try {
				return await action(argument);
			}
			catch (error)
			{
				services.report(error);
				return undefined;
			}
		}));
	};
	command('runFile', argument => tasks.run(targetUri(argument), 'run'));
	command('checkFile', argument => tasks.run(targetUri(argument), 'check'));
	command('debugFile', argument => {
		const uri = targetUri(argument);
		const resource = services.runtimeContext(uri);
		return vscode.debug.startDebugging(resource.folder, {
			name: 'VeroScript', type: 'veroscript', request: 'launch', program: uri.fsPath,
			cwd: resource.base, stopOnEntry: false
		});
	});
	command('attachToHost', argument => debuggerProvider.attachToHost(argument));
	command('attachToDomain', () => debuggerProvider.attachToDomain());
	command('selectRuntime', async argument => {
		const uri = argument instanceof vscode.Uri ? argument : vscode.window.activeTextEditor?.document.uri;
		let folder = uri ? vscode.workspace.getWorkspaceFolder(uri) : undefined;
		if (!folder && !uri && vscode.workspace.workspaceFolders?.length) {
			folder = vscode.workspace.workspaceFolders.length === 1 ? vscode.workspace.workspaceFolders[0] : await vscode.window.showWorkspaceFolderPick();
			if (!folder) return;
		}
		const selected = await vscode.window.showOpenDialog({ canSelectFiles: false, canSelectFolders: true, canSelectMany: false, openLabel: vscode.l10n.t('Select Runtime') });
		if (!selected?.[0]) return;
		const runtime = await services.resolver.resolve(selected[0].fsPath);
		await vscode.workspace.getConfiguration('veroscript', folder?.uri ?? uri).update('runtimePath', runtime.root,
			folder ? vscode.ConfigurationTarget.WorkspaceFolder : vscode.ConfigurationTarget.Global);
		await languages.restart();
	});
	command('restartLanguageServer', () => languages.restart());
	command('showOutput', () => output.show());
	command('showEnvironment', async argument => {
		const resource = services.runtimeContext(targetUri(argument));
		const runtime = await services.runtime(resource);
		output.info(`Extension ${context.extension.packageJSON.version}; ${process.platform}/${process.arch}; VS Code ${vscode.version}; locale ${vscode.env.language}`);
		output.info(`Workspace: ${resource.base}\nRuntime: ${runtime.root}\n${runtime.description}\nCLI: ${runtime.cli}\nLSP: ${runtime.lsp}\nDAP: ${runtime.dap}`);
		for (const client of languages.snapshot()) {
			output.info(JSON.stringify(client));
		}
		output.show();
	});
	return { apiVersion: 1 as const, getToolchain: (uri: vscode.Uri, sourceFile?: vscode.Uri) => toolchain(services, uri, sourceFile),
		ready: (uri?: vscode.Uri) => languages.ready(uri), snapshot: () => languages.snapshot() };
}

export async function deactivate(): Promise<void> {
	await languageServers?.dispose();
	languageServers = undefined;
}

let languageServers: LanguageServers | undefined;
