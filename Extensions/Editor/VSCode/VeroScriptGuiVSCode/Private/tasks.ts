// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as path from 'node:path';
import * as vscode from 'vscode';
import type { VeroScriptToolchain } from '../../VeroScriptVSCode/Public/api';
import { baseExtension } from './dependency';
import { backendValues, GuiArgumentError, GuiInvocation, invocationArguments, stringList, stringValue } from './invocation';
import { containsPath, localContext, resolveSetting } from './paths';

interface GuiTaskDefinition extends vscode.TaskDefinition {
	readonly type: 'veroscript-gui';
	readonly action: GuiInvocation['action'];
	readonly input: string;
}

interface PreparedTask {
	readonly invocation: GuiInvocation;
	readonly toolchain: VeroScriptToolchain;
	readonly execution: vscode.ProcessExecution;
}

export function report(error: unknown, output: vscode.LogOutputChannel): void {
	const message = error instanceof GuiArgumentError ? vscode.l10n.t(error.message, ...error.args)
		: error instanceof Error ? error.message : String(error);
	output.error(message);
	void vscode.window.showErrorMessage(vscode.l10n.t('VeroScript GUI: {0}', message));
}

export class GuiTasks implements vscode.TaskProvider {
	constructor(private readonly output: vscode.LogOutputChannel) {}

	provideTasks(): vscode.Task[] {
		return [];
	}

	async resolveTask(task: vscode.Task): Promise<vscode.Task | undefined> {
		try {
			const folder = typeof task.scope === 'object' ? task.scope : undefined;
			const active = vscode.window.activeTextEditor?.document.uri;
			const anchor = folder?.uri ?? active;
			if (!anchor) {
				throw new Error(vscode.l10n.t('This GUI task needs a workspace folder or an active file.'));
			}
			const prepared = await this.prepare(task.definition as GuiTaskDefinition, anchor, active);
			const resolved = new vscode.Task(task.definition, task.scope ?? vscode.TaskScope.Workspace,
				task.name, task.source, prepared.execution, task.problemMatchers.length ? task.problemMatchers : ['$veroscript-gui']);
			resolved.group = task.group;
			resolved.presentationOptions = task.presentationOptions;
			resolved.runOptions = task.runOptions;
			resolved.isBackground = task.isBackground;
			resolved.detail = task.detail;
			return resolved;
		}
		catch (error)
		{
			report(error, this.output);
			return undefined;
		}
	}

	async selectEntry(): Promise<void> {
		const selected = (await vscode.window.showOpenDialog({ canSelectFiles: true, canSelectFolders: false, canSelectMany: false,
			filters: { 'VeroScript GUI': ['vs', 'vswml', 'vsapp'] }, openLabel: vscode.l10n.t('Select GUI Entry') }))?.[0];
		if (!selected) return;
		const { folder, base } = localContext(selected);
		await vscode.workspace.getConfiguration('veroscriptGui', selected).update('entry', folder ? path.relative(base, selected.fsPath) : selected.fsPath,
			folder ? vscode.ConfigurationTarget.WorkspaceFolder : vscode.ConfigurationTarget.Global);
	}

	async run(action: GuiInvocation['action'], argument?: unknown): Promise<vscode.TaskExecution | undefined> {
		let uri = argument instanceof vscode.Uri ? argument : vscode.window.activeTextEditor?.document.uri;
		if (!uri || uri.scheme !== 'file' || !['.vs', '.vswml', '.vswcss', '.vsapp'].includes(path.extname(uri.fsPath))) {
			uri = (await vscode.window.showOpenDialog({ canSelectFiles: true, canSelectFolders: false, canSelectMany: false,
				filters: { 'VeroScript GUI': ['vs', 'vswml', 'vsapp'] }, openLabel: vscode.l10n.t('Select GUI Entry') }))?.[0];
		}
		if (!uri) return undefined;
		const entry = vscode.workspace.getConfiguration('veroscriptGui', uri).get<string>('entry', '');
		const input = entry ? resolveSetting(entry, uri) : path.extname(uri.fsPath) === '.vswcss'
			? uri.fsPath.slice(0, -'.vswcss'.length) + '.vswml' : uri.fsPath;
		const definition: GuiTaskDefinition = { type: 'veroscript-gui', action, input };
		if (action === 'compile') {
			if (path.extname(input) === '.vsapp') {
				throw new GuiArgumentError('GUI compilation needs source input and a .vsapp output.');
			}
			const output = await vscode.window.showSaveDialog({ defaultUri: vscode.Uri.file(input.slice(0, -path.extname(input).length) + '.vsapp'),
				filters: { 'VeroScript GUI application': ['vsapp'] }, saveLabel: vscode.l10n.t('Compile GUI Application') });
			if (!output) return undefined;
			definition.output = output.fsPath;
		}
		const prepared = await this.prepare(definition, uri, uri);
		if (!await this.saveSources(prepared)) return undefined;
		const { folder } = localContext(uri);
		const task = new vscode.Task(definition, folder ?? vscode.TaskScope.Global,
			`gui-${action}: ${path.basename(input)}`, 'VeroScript GUI', prepared.execution, ['$veroscript-gui']);
		task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };
		this.output.info(`${prepared.invocation.action}: ${input}`);
		return vscode.tasks.executeTask(task);
	}

	private async prepare(definition: GuiTaskDefinition, anchor: vscode.Uri, active = anchor): Promise<PreparedTask> {
		if (!['check', 'compile', 'preview'].includes(definition.action)) {
			throw new GuiArgumentError('Unsupported GUI action: {0}.', String(definition.action));
		}
		const input = resolveSetting(definition.input, anchor, active);
		if (!['.vs', '.vswml', '.vsapp'].includes(path.extname(input))) {
			throw new GuiArgumentError('Expected a .vs, .vswml or .vsapp GUI entry.');
		}
		const source = vscode.Uri.file(input);
		if (!((await vscode.workspace.fs.stat(source)).type & vscode.FileType.File)) {
			throw new Error(vscode.l10n.t('The GUI input must be a file: {0}', input));
		}
		const settings = vscode.workspace.getConfiguration('veroscriptGui', anchor);
		const value = (key: string, fallback: unknown) => definition[key] === undefined ? settings.get(key === 'frames' ? 'previewFrames' : key === 'name' ? 'packageName' : key, fallback) : definition[key];
		const toolchain = await (await baseExtension()).getToolchain(anchor, source);
		const environmentPath = stringValue(value('environmentPath', ''), 'environmentPath');
		const invocation: GuiInvocation = {
			action: definition.action, input,
			output: definition.output === undefined ? undefined : resolveSetting(definition.output, anchor, active),
			name: stringValue(value('name', 'gui-app'), 'name'), frames: definition.action === 'preview' ? value('frames', 0) as number : 0,
			assets: stringList(value('assets', []), 'assets').map(asset => resolveSetting(asset, anchor, active)),
			args: definition.args === undefined ? toolchain.scriptArguments : stringList(definition.args, 'args'),
			backends: backendValues(value('backends', {})), environmentPath: environmentPath ? resolveSetting(environmentPath, anchor, active) : ''
		};
		const args = invocationArguments(toolchain, invocation);
		if (!((await vscode.workspace.fs.stat(vscode.Uri.file(toolchain.cwd))).type & vscode.FileType.Directory)) {
			throw new Error(vscode.l10n.t('The GUI working directory must be a directory: {0}', toolchain.cwd));
		}
		return { invocation, toolchain, execution: new vscode.ProcessExecution(toolchain.executable, args,
			{ cwd: toolchain.cwd, env: { ...toolchain.environment } as Record<string, string> }) };
	}

	private dirtySources(prepared: PreparedTask): vscode.TextDocument[] {
		const directories = [...prepared.toolchain.sourceDirectories, path.dirname(prepared.invocation.input)];
		const explicit = [...prepared.toolchain.sourceFiles, prepared.invocation.input, ...prepared.invocation.assets];
		return vscode.workspace.textDocuments.filter(document => document.isDirty && document.uri.scheme === 'file'
			&& (explicit.some(file => path.relative(file, document.uri.fsPath) === '')
				|| (['.vs', '.vswml', '.vswcss'].includes(path.extname(document.uri.fsPath))
					&& directories.some(directory => containsPath(directory, document.uri.fsPath)))));
	}

	private async saveSources(prepared: PreparedTask): Promise<boolean> {
		const dirty = this.dirtySources(prepared);
		if (!dirty.length) return true;
		const save = vscode.l10n.t('Save and Continue');
		if (await vscode.window.showWarningMessage(vscode.l10n.t('Save {0} GUI source document(s) before starting?', dirty.length), { modal: true }, save) !== save) return false;
		for (const document of dirty) {
			if (!await document.save()) return false;
		}
		return this.dirtySources(prepared).length === 0;
	}
}
