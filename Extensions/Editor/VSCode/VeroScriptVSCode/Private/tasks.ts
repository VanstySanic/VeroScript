// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as path from 'node:path';
import * as vscode from 'vscode';
import { backend, cliArguments, compileFields, environment, mergedCompileOptions, processEnvironment, requireWorkspaceContext, resolvePath, strings, text } from './configuration';
import { locale, participating, ResourceContext, saveDocuments, Services } from './context';
import { requireDirectory, requireFile } from './runtime';

export interface VeroTaskDefinition extends vscode.TaskDefinition {
	type: 'veroscript';
	action: 'run' | 'check';
	program: string;
}

export class VeroTasks implements vscode.TaskProvider {
	constructor(private readonly services: Services) {}

	provideTasks(): vscode.Task[] {
		// Discovering tasks must never start a Runtime or prompt to save a document.
		return [];
	}

	async resolveTask(task: vscode.Task): Promise<vscode.Task | undefined> {
		try {
			const folder = typeof task.scope === 'object' ? task.scope : undefined;
			const active = vscode.window.activeTextEditor?.document.uri;
			const baseUri = folder?.uri ?? active;
			if (!baseUri) {
				throw new Error(vscode.l10n.t('This task needs a workspace folder or an active file.'));
			}
			const context = this.services.context(baseUri, active?.scheme === 'file' ? active.fsPath : undefined);
			const execution = await this.execution(task.definition as VeroTaskDefinition, context, active?.fsPath);
			const resolved = new vscode.Task(task.definition, task.scope ?? vscode.TaskScope.Workspace, task.name, task.source, execution, task.problemMatchers);
			resolved.group = task.group;
			resolved.presentationOptions = task.presentationOptions;
			resolved.runOptions = task.runOptions;
			resolved.isBackground = task.isBackground;
			resolved.detail = task.detail;
			return resolved;
		}
		catch (error)
		{
			this.services.report(error);
			return undefined;
		}
	}

	async run(uri: vscode.Uri, action: 'run' | 'check'): Promise<vscode.TaskExecution | undefined> {
		const context = this.services.context(uri);
		const runtime = await this.services.runtime(context);
		const options = mergedCompileOptions(context.settings, {}, context.base, runtime.stdlib);
		if (!await saveDocuments(context, options)) return undefined;
		const definition: VeroTaskDefinition = { type: 'veroscript', action, program: uri.fsPath };
		const task = new vscode.Task(definition, context.folder ?? vscode.TaskScope.Global,
			`${action}: ${path.basename(uri.fsPath)}`, 'VeroScript', await this.execution(definition, context, uri.fsPath), []);
		task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };
		if (vscode.workspace.textDocuments.some(document => document.isDirty && participating(document, context, options))) return undefined;
		return vscode.tasks.executeTask(task);
	}

	private async execution(definition: VeroTaskDefinition, context: ResourceContext, activeFile?: string): Promise<vscode.ProcessExecution> {
		if (definition.action !== 'run' && definition.action !== 'check') {
			throw new Error('task.action: expected run or check.');
		}
		for (const key of ['program', 'cwd', 'jitBackendPath', ...compileFields.filter(field => field.endsWith('Paths'))]) {
			requireWorkspaceContext(definition[key], context.folder !== undefined);
		}
		const cwd = definition.cwd === undefined ? context.settings.cwd
			: definition.cwd === '' ? context.base : resolvePath(definition.cwd, context.base, activeFile);
		const program = resolvePath(definition.program, cwd, activeFile, context.base);
		await Promise.all([requireDirectory(cwd), requireFile(program)]);
		const runtime = await this.services.runtime(context);
		const options = mergedCompileOptions(context.settings, definition, context.base, runtime.stdlib);
		const mode = definition.backend === undefined ? context.settings.backend : backend(definition.backend);
		const jitBackendPath = definition.jitBackendPath === undefined ? context.settings.jitBackendPath
			: text(definition.jitBackendPath, 'task.jitBackendPath') === '' ? '' : resolvePath(definition.jitBackendPath, context.base, activeFile);
		const args = definition.args === undefined ? context.settings.args : strings(definition.args, 'task.args');
		const env = definition.env === undefined ? context.settings.env : environment(definition.env);
		return new vscode.ProcessExecution(runtime.cli, cliArguments(options, program, definition.action, mode, args, jitBackendPath), {
			cwd, env: processEnvironment({ VERO_LANG: locale(), ...env }) as Record<string, string>
		});
	}
}
