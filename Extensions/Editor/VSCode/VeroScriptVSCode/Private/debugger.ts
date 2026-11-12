// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as path from 'node:path';
import { isIP } from 'node:net';
import * as vscode from 'vscode';
import { boolean, compileFields, compileOptions, environment, expand, mergedCompileOptions, processEnvironment, resolvePath, strings, text } from './configuration';
import { participating, saveDocuments, Services, sourceContext, targetUri } from './context';
import { requireDirectory, requireFile } from './runtime';
import { DomainDebugAdapter } from './domainDebugAdapter';

function validHost(value: string): boolean {
	return value.length > 0 && (isIP(value) !== 0 || !/[\s\0/:\\[\]]/.test(value));
}

export class VeroDebugger implements vscode.DebugConfigurationProvider, vscode.DebugAdapterDescriptorFactory {
	constructor(private readonly services: Services, private readonly input = vscode.window.showInputBox) {}

	provideDebugConfigurations(): vscode.DebugConfiguration[] {
		return [
			{ name: 'VeroScript', type: 'veroscript', request: 'launch', program: '${file}', stopOnEntry: false },
			{ name: vscode.l10n.t('VeroScript: Attach to Host'), type: 'veroscript', request: 'attach', program: '${file}', host: '127.0.0.1', port: 4711, stopOnEntry: true },
			{ name: vscode.l10n.t('VeroScript: Attach to Domain'), type: 'veroscript', request: 'attach', attachMode: 'domain', host: '127.0.0.1', port: 4711 }
		];
	}

	async attachToHost(argument?: unknown): Promise<boolean | undefined> {
		const active = argument instanceof vscode.Uri ? argument : vscode.window.activeTextEditor?.document.uri;
		const selected = active?.scheme === 'file' && path.extname(active.fsPath).toLowerCase() === '.vs' ? active
			: (await vscode.window.showOpenDialog({ canSelectFiles: true, canSelectFolders: false, canSelectMany: false,
				filters: { VeroScript: ['vs'] }, openLabel: vscode.l10n.t('Select Script') }))?.[0];
		if (!selected) return undefined;
		const uri = targetUri(selected);
		await requireFile(uri.fsPath);
		const title = vscode.l10n.t('VeroScript: Attach to Host');
		const host = await this.input({ title, value: '127.0.0.1', ignoreFocusOut: true,
			prompt: vscode.l10n.t('DAP host address. Starts a new script session in the host; does not take over a running VM.'),
			validateInput: value => validHost(value.trim()) ? undefined : vscode.l10n.t('Enter a host name or IP address without whitespace.') });
		if (host === undefined) return undefined;
		const port = await this.input({ title, value: '4711', ignoreFocusOut: true,
			prompt: vscode.l10n.t('Port of the host that is already listening for TCP DAP.'),
			validateInput: value => /^\d+$/.test(value.trim()) && Number(value) >= 1 && Number(value) <= 65535
				? undefined : vscode.l10n.t('Enter an integer port from 1 to 65535.') });
		if (port === undefined) return undefined;
		return vscode.debug.startDebugging(sourceContext(uri).folder, {
			name: title, type: 'veroscript', request: 'attach', program: uri.fsPath, host: host.trim(), port: Number(port), stopOnEntry: true
		});
	}

	async attachToDomain(): Promise<boolean | undefined> {
		const title = vscode.l10n.t('VeroScript: Attach to Domain');
		const host = await this.input({ title, value: '127.0.0.1', ignoreFocusOut: true,
			prompt: vscode.l10n.t('DAP host address. Connects to a host-prepared script domain without loading a program.'),
			validateInput: value => validHost(value.trim()) ? undefined : vscode.l10n.t('Enter a host name or IP address without whitespace.') });
		if (host === undefined) return undefined;
		const port = await this.input({ title, value: '4711', ignoreFocusOut: true,
			prompt: vscode.l10n.t('Port of the host that is already listening for TCP DAP.'),
			validateInput: value => /^\d+$/.test(value.trim()) && Number(value) >= 1 && Number(value) <= 65535
				? undefined : vscode.l10n.t('Enter an integer port from 1 to 65535.') });
		if (port === undefined) return undefined;
		const domainId = await this.input({ title, value: '', ignoreFocusOut: true,
			prompt: vscode.l10n.t('Expected domain ID. Leave empty to use the domain provided by this endpoint.'),
			validateInput: value => value.includes('\0') ? vscode.l10n.t('The domain ID must not contain NUL.') : undefined });
		if (domainId === undefined) return undefined;
		return vscode.debug.startDebugging(undefined, {
			name: title, type: 'veroscript', request: 'attach', attachMode: 'domain', host: host.trim(), port: Number(port),
			...(domainId ? { domainId } : {})
		});
	}

	async resolveDebugConfiguration(folder: vscode.WorkspaceFolder | undefined, configuration: vscode.DebugConfiguration): Promise<vscode.DebugConfiguration | undefined> {
		try {
			const config = { ...configuration };
			if (!config.type && !config.request && !config.name) {
				Object.assign(config, this.provideDebugConfigurations()[0]);
			}
			if (config.noDebug) {
				throw new Error(vscode.l10n.t('Use VeroScript: Run File to run without debugging.'));
			}
			if (config.request !== 'launch' && config.request !== 'attach') {
				throw new Error('request: expected launch or attach.');
			}
			if (config.request === 'attach' && config.attachMode === 'domain') {
				return this.domainConfiguration(config, false);
			}
			this.validateAttachMode(config);
			const active = vscode.window.activeTextEditor?.document.uri;
			const base = folder?.uri.fsPath ?? (typeof config.program === 'string' && path.isAbsolute(config.program) ? path.dirname(config.program)
				: active?.scheme === 'file' ? path.dirname(active.fsPath) : undefined);
			if (base) {
				// Resolve only deterministic variables before VS Code runs preLaunchTask.
				let program: string | undefined;
				let compileBase = base;
				try {
					const cwd = config.cwd === undefined || config.cwd === '' ? base : resolvePath(config.cwd, base, active?.fsPath);
					compileBase = cwd;
					program = resolvePath(config.program, cwd, active?.fsPath, base);
				}
				catch
				{
					program = active?.scheme === 'file' ? active.fsPath : undefined;
				}
				if (program) {
					const context = sourceContext(vscode.Uri.file(program));
					const values: Record<string, unknown> = {};
					for (const key of compileFields) {
						if (config[key] !== undefined) {
							try {
								// Leave input/command variables for VS Code's single substitution pass.
								values[key] = Array.isArray(config[key]) && key.endsWith('Paths')
									? strings(config[key], key).map(value => expand(value, base, active?.fsPath)) : config[key];
							}
							catch
							{
								values[key] = undefined;
							}
						}
					}
					const resource = config.request === 'launch' ? this.services.runtimeContext(context.uri) : undefined;
					const options = resource
						? mergedCompileOptions(resource.settings, values, compileBase, (await this.services.runtime(resource)).stdlib)
						: compileOptions(values, compileBase);
					if (!await saveDocuments(context, options)) return undefined;
				}
			}
			return config;
		}
		catch (error)
		{
			this.services.report(error);
			return undefined;
		}
	}

	async resolveDebugConfigurationWithSubstitutedVariables(folder: vscode.WorkspaceFolder | undefined, configuration: vscode.DebugConfiguration): Promise<vscode.DebugConfiguration | undefined> {
		try {
			const config = { ...configuration };
			if (config.request === 'attach' && config.attachMode === 'domain') {
				return this.domainConfiguration(config);
			}
			this.validateAttachMode(config);
			const active = vscode.window.activeTextEditor?.document.uri;
			const rawProgram = text(config.program, 'program');
			const base = folder?.uri.fsPath ?? (path.isAbsolute(rawProgram) ? path.dirname(rawProgram)
				: active?.scheme === 'file' ? path.dirname(active.fsPath) : undefined);
			if (!base && !path.isAbsolute(rawProgram)) {
				throw new Error('program: expected an absolute path without a workspace.');
			}
			const resolutionBase = base ?? path.dirname(rawProgram);
			const cwd = config.cwd === undefined || config.cwd === '' ? resolutionBase : resolvePath(config.cwd, resolutionBase);
			const program = resolvePath(rawProgram, cwd);
			await requireFile(program);
			const context = sourceContext(vscode.Uri.file(program));
			let options;
			if (config.request === 'launch') {
				await requireDirectory(cwd);
				const resource = this.services.runtimeContext(context.uri);
				const runtime = await this.services.runtime(resource);
				options = mergedCompileOptions(resource.settings, config, cwd, runtime.stdlib);
				Object.assign(config, options, { program, cwd, workspace: folder?.uri.fsPath ?? path.dirname(program), _veroscriptRuntimeRoot: runtime.root });
				config.env = environment(config.env ?? {});
			}
			else if (config.request === 'attach')
			{
				if (config.cwd !== undefined || config.workspace !== undefined || config.env !== undefined) {
					throw new Error(vscode.l10n.t('Attach does not support cwd, workspace or env overrides.'));
				}
				this.connectionConfiguration(config);
				options = compileOptions(config, resolutionBase);
				// Omitted attach settings are owned by the external host.
				for (const key of compileFields) {
					if (config[key] !== undefined) {
						config[key] = options[key];
					}
				}
				config.program = program;
			}
			else
			{
				throw new Error('request: expected launch or attach.');
			}
			if (config.args !== undefined) {
				config.args = strings(config.args, 'args');
			}
			if (config.input !== undefined) {
				config.input = text(config.input, 'input');
			}
			if (config.stopOnEntry !== undefined) {
				config.stopOnEntry = boolean(config.stopOnEntry, 'stopOnEntry');
			}
			if (config.maxCallDepth !== undefined && (!Number.isSafeInteger(config.maxCallDepth) || config.maxCallDepth < 0)) {
				throw new Error('maxCallDepth: expected a nonnegative safe integer.');
			}
			if (config.noDebug) {
				throw new Error(vscode.l10n.t('Use VeroScript: Run File to run without debugging.'));
			}
			this.rejectUnsupported(config);
			if (vscode.workspace.textDocuments.some(document => document.isDirty && participating(document, context, options))) {
				throw new Error(vscode.l10n.t('VeroScript files changed during debug preparation. Save them and start again.'));
			}
			return config;
		}
		catch (error)
		{
			this.services.report(error);
			return undefined;
		}
	}

	async createDebugAdapterDescriptor(session: vscode.DebugSession): Promise<vscode.DebugAdapterDescriptor> {
		const config = session.configuration;
		if (config.request === 'attach' && config.attachMode === 'domain') {
			return new vscode.DebugAdapterInlineImplementation(new DomainDebugAdapter(config.host, config.port, this.services.output));
		}
		if (config.request === 'attach') return new vscode.DebugAdapterServer(config.port, config.host);
		const runtime = await this.services.resolver.resolve(text(config._veroscriptRuntimeRoot, 'Runtime'));
		return new vscode.DebugAdapterExecutable(runtime.dap, [], { cwd: config.cwd, env: processEnvironment(environment(config.env ?? {})) as Record<string, string> });
	}

	private domainConfiguration(config: vscode.DebugConfiguration, substituted = true): vscode.DebugConfiguration {
		if (config.noDebug) {
			throw new Error(vscode.l10n.t('Use VeroScript: Run File to run without debugging.'));
		}
		for (const key of [...compileFields, 'program', 'args', 'input', 'stopOnEntry', 'maxCallDepth', 'cwd', 'workspace', 'env', 'debugServer']) {
			if (config[key] !== undefined) {
				throw new Error(vscode.l10n.t('Domain attachment does not support loading or execution overrides: {0}.', key));
			}
		}
		this.rejectUnsupported(config);
		if (substituted) {
			this.connectionConfiguration(config);
		}
		if (config.domainId !== undefined) {
			config.domainId = text(config.domainId, 'domainId');
		}
		return config;
	}

	private validateAttachMode(config: vscode.DebugConfiguration): void {
		if ((config.attachMode !== undefined && (config.request !== 'attach' || config.attachMode !== 'program')) || config.domainId !== undefined) {
			throw new Error(vscode.l10n.t('Use attachMode domain for a domain ID; other attachment modes only support program.'));
		}
	}

	private connectionConfiguration(config: vscode.DebugConfiguration): void {
		if (!Number.isInteger(config.port) || config.port < 1 || config.port > 65535) {
			throw new Error('port: expected an integer in 1..65535.');
		}
		config.host = text(config.host ?? '127.0.0.1', 'host').trim();
		if (!validHost(config.host)) {
			throw new Error(vscode.l10n.t('Enter a host name or IP address without whitespace.'));
		}
	}

	private rejectUnsupported(config: vscode.DebugConfiguration): void {
		for (const field of ['processId', 'pathMappings', 'source', 'sourceOverlays', 'fileLabel', 'workingDirectory', 'environment', 'stdlibPath', 'backend', 'jitBackendPath']) {
			if (config[field] !== undefined) {
				throw new Error(`Unsupported debug setting: ${field}`);
			}
		}
	}
}
