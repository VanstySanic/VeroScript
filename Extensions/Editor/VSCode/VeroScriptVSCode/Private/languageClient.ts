// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as path from 'node:path';
import { randomUUID } from 'node:crypto';
import * as vscode from 'vscode';
import { CloseAction, ErrorAction, InitializeParams, LanguageClient, Middleware, State } from 'vscode-languageclient/node';
import { containsPath, globLiteral, mergedCompileOptions, processEnvironment, watchDirectories } from './configuration';
import { AnalysisContext, locale, Services } from './context';
import { requireFile, Runtime } from './runtime';
import { continueNamespaceCompletion, presentCompletion, presentHover } from './presentation';
import { ModuleSources } from './moduleSources';

export interface ClientStatus {
	readonly key: string;
	readonly state: string;
	readonly runtime: string;
	readonly version: string;
}

interface ClientEntry {
	readonly client: ScopedLanguageClient;
	readonly context: AnalysisContext;
	readonly runtime: Runtime | undefined;
	readonly command: string;
	readonly signature: string;
	readonly moduleSource: vscode.Disposable;
	settings: string;
	watchers: vscode.Disposable[];
}

function configuredPaths(options: Record<string, unknown>, key: string): readonly string[] {
	return (options[key] as readonly string[] | undefined) ?? [];
}

function documentSelector(uris: readonly vscode.Uri[]) {
	return uris.map(uri => ({ scheme: 'file', language: 'veroscript', pattern: {
		baseUri: vscode.Uri.file(path.dirname(uri.fsPath)).toString(),
		pattern: globLiteral(path.basename(uri.fsPath))
	} }));
}

class ScopedLanguageClient extends LanguageClient {
	override async dispose(timeout?: number): Promise<void> {
		// A didClose can already be awaiting the SDK's start() when reconciliation
		// retires this client. Drain it before shutdown clears the active connection.
		this.retiring = true;
		await Promise.allSettled(this.documentClosures);
		await super.dispose(timeout);
	}

	override async start(): Promise<void> {
		// The SDK also calls start() for ordinary requests on a running client.
		// Reset registrations only when beginning a new connection lifetime.
		if (this.state === State.Running || this.state === State.Starting) {
			await super.start();
			return;
		}
		this.selectorSignature = '';
		this.synchronizationSignature = '';
		this.synchronizationUris = [];
		this.synchronizationRegistration = undefined;
		this.startupTimedOut = false;
		this.initializingCustomServer = Boolean(this.customCommand);
		let timeoutError: Error | undefined;
		const timeout = this.customCommand ? setTimeout(() => {
			this.startupTimedOut = true;
			timeoutError = new Error(vscode.l10n.t('Timed out initializing the language server at {0}.', this.customCommand));
			this.abortStartup();
		}, 10000) : undefined;
		try {
			const starting = super.start();
			if (this.initializingCustomServer) {
				// Observe the SDK's shared start promise while initialization is pending.
				// It can be cleared by the close handler before the first start() returns.
				await Promise.all([starting, super.start()]);
			}
			else
			{
				await starting;
			}
		}
		catch (error)
		{
			throw timeoutError ?? error;
		}
		finally
		{
			clearTimeout(timeout);
			this.initializingCustomServer = false;
		}
		// A connection close can clear the SDK's start promise before start() returns.
		if (timeoutError)
		{
			throw timeoutError;
		}
	}

	override async stop(timeout?: number): Promise<void> {
		if (this.state !== State.Running && (this.initializingCustomServer || this.startupTimedOut)) {
			this.abortStartup();
			return;
		}
		await super.stop(timeout);
	}

	private abortStartup(): void {
		const child = this.serverProcess;
		child?.kill('SIGKILL');
		child?.stdout?.destroy();
		child?.stdin?.destroy();
	}

	selectSynchronizedDocuments(uris: readonly vscode.Uri[]): void {
		const signature = uris.map(uri => uri.toString()).sort().join('\n');
		if (signature === this.synchronizationSignature || this.state !== State.Running || !this.initializeResult) return;
		this.synchronizationSignature = signature;
		const previous = this.synchronizationRegistration;
		const previousUris = this.synchronizationUris;
		this.synchronizationUris = [...uris];
		const id = `veroscript-sync:${++this.synchronizationSequence}`;
		this.synchronizationRegistration = id;
		const selector = documentSelector(uris);
		const raw = this.initializeResult.capabilities.textDocumentSync;
		const sync = typeof raw === 'number' ? { openClose: raw !== 0, change: raw, save: false } : raw;
		const open = this.getFeature('textDocument/didOpen');
		const close = this.getFeature('textDocument/didClose');
		const change = this.getFeature('textDocument/didChange');
		const save = this.getFeature('textDocument/didSave');
		// Add the new registrations first: the SDK then closes only documents
		// leaving the scope, retaining buffers shared by both selector sets.
		if (sync?.openClose) {
			close.register({ id, registerOptions: { documentSelector: selector } });
			open.register({ id, registerOptions: { documentSelector: selector } });
		}
		if (sync?.change) {
			change.register({ id, registerOptions: { documentSelector: selector, syncKind: sync.change } });
		}
		if (sync?.save) {
			const registerOptions = { documentSelector: selector, includeText: typeof sync.save === 'object' && sync.save.includeText === true };
			save.register({ id, registerOptions });
		}
		if (previous) {
			if (sync?.openClose) {
				// The SDK reuses a consumed selector iterator while unregistering
				// didClose. Restrict the retired registration to departing documents
				// so retained buffers cannot receive an erroneous close notification.
				const retained = new Set(uris.map(uri => uri.toString()));
				close.register({ id: previous, registerOptions: {
					documentSelector: documentSelector(previousUris.filter(uri => !retained.has(uri.toString())))
				} });
			}
			close.unregister(previous);
			open.unregister(previous);
			change.unregister(previous);
			save.unregister(previous);
		}
	}

	selectDocuments(uris: readonly vscode.Uri[]): void {
		const signature = uris.map(uri => uri.toString()).sort().join('\n');
		if (signature === this.selectorSignature || this.state !== State.Running || !this.initializeResult) return;
		this.selectorSignature = signature;
		const selector = documentSelector(uris);
		// VS Code selects a single formatter/semantic provider. Register only owned
		// open files, so nested roots cannot mask each other's providers. Rebinding
		// providers keeps the native process, semantic snapshots and index alive.
		const features = [
			this.getFeature('textDocument/completion'),
			this.getFeature('textDocument/hover'),
			this.getFeature('textDocument/signatureHelp'),
			this.getFeature('textDocument/definition'),
			this.getFeature('textDocument/declaration'),
			this.getFeature('textDocument/typeDefinition'),
			this.getFeature('textDocument/implementation'),
			this.getFeature('textDocument/references'),
			this.getFeature('textDocument/documentHighlight'),
			this.getFeature('textDocument/documentSymbol'),
			this.getFeature('textDocument/codeAction'),
			this.getFeature('textDocument/formatting'),
			this.getFeature('textDocument/rangeFormatting'),
			this.getFeature('textDocument/onTypeFormatting'),
			this.getFeature('textDocument/rename'),
			this.getFeature('textDocument/documentLink'),
			this.getFeature('textDocument/foldingRange'),
			this.getFeature('textDocument/selectionRange'),
			this.getFeature('textDocument/semanticTokens'),
			this.getFeature('textDocument/prepareTypeHierarchy'),
		];
		for (const feature of features) {
			feature.clear();
			feature.initialize(this.initializeResult.capabilities, selector);
		}
	}

	constructor(id: string, context: AnalysisContext, command: string, options: () => Record<string, unknown>, middleware: Middleware, output: vscode.LogOutputChannel,
		uriConverters: ReturnType<ModuleSources['uriConverters']>) {
		const exits: number[] = [];
		super(id, 'VeroScript', {
			command, args: [...context.settings.arguments],
			options: { cwd: context.base, env: context.settings.executablePath ? undefined : processEnvironment() }
		}, {
			documentSelector: [],
			uriConverters,
			workspaceFolder: context.folder,
			initializationOptions: () => ({ veroscript: options() }),
			initializationFailedHandler: () => false,
			outputChannel: output, traceOutputChannel: output,
			middleware: {
				...middleware,
				didClose: async (document, next) => {
					if (this.retiring) return;
					const closing = middleware.didClose ? middleware.didClose(document, next) : next(document);
					this.documentClosures.add(closing);
					try {
						await closing;
					}
					finally
					{
						this.documentClosures.delete(closing);
					}
				}
			},
			errorHandler: {
				error: (_error, _message, count) => ({ action: count !== undefined && count > 3 ? ErrorAction.Shutdown : ErrorAction.Continue }),
				closed: () => {
					if (this.startupTimedOut || this.retiring) return { action: CloseAction.DoNotRestart, handled: true };
					if (this.customCommand && this.state === State.Starting) return { action: CloseAction.DoNotRestart };
					const now = Date.now();
					while (exits[0] !== undefined && now - exits[0] > 60000) {
						exits.shift();
					}
					exits.push(now);
					if (exits.length <= 3) return { action: CloseAction.Restart };
					output.error(vscode.l10n.t('Language server repeatedly exited. Use Restart Language Server after correcting the problem.'));
					return { action: CloseAction.DoNotRestart };
				}
			}
		});
		this.standalone = context.folder === undefined;
		this.customCommand = context.settings.executablePath;
	}

	protected override getLocale(): string {
		return locale();
	}

	protected override fillInitializeParams(params: InitializeParams): void {
		super.fillInitializeParams(params);
		// Own bounded watchers cover roots and configured imports, including external directories.
		if (params.capabilities.workspace?.didChangeWatchedFiles) {
			params.capabilities.workspace.didChangeWatchedFiles.dynamicRegistration = false;
		}
		if (this.standalone) {
			params.rootUri = null;
			params.rootPath = null;
			params.workspaceFolders = [];
			if (params.capabilities.workspace) {
				params.capabilities.workspace.workspaceFolders = false;
			}
		}
	}

	private readonly standalone: boolean;
	private readonly customCommand: string;
	private startupTimedOut = false;
	private initializingCustomServer = false;
	private readonly documentClosures = new Set<Promise<void>>();
	private retiring = false;
	private selectorSignature = '';
	private synchronizationSignature = '';
	private synchronizationUris: readonly vscode.Uri[] = [];
	private synchronizationSequence = 0;
	private synchronizationRegistration: string | undefined;
}

export class LanguageServers {
	constructor(private readonly services: Services, private readonly status: vscode.StatusBarItem) {
		this.listeners.push(
			vscode.workspace.onDidOpenTextDocument(() => this.schedule()),
			vscode.workspace.onDidCloseTextDocument(() => this.schedule()),
			vscode.workspace.onDidChangeWorkspaceFolders(() => this.schedule(true)),
			vscode.workspace.onDidChangeConfiguration(event => {
				if (event.affectsConfiguration('veroscript')) {
					clearTimeout(this.timer);
					this.timer = setTimeout(() => this.schedule(), 150);
				}
			}),
			vscode.window.onDidChangeActiveTextEditor(() => this.updateStatus())
		);
		this.schedule();
	}

	async ready(uri?: vscode.Uri): Promise<LanguageClient | undefined> {
		await this.pending;
		return uri ? this.entries.get(this.key(uri))?.client : undefined;
	}

	snapshot(): ClientStatus[] {
		return [...this.entries].map(([key, entry]) => ({
			key, state: State[entry.client.state], runtime: entry.runtime?.root ?? entry.command,
			version: entry.runtime?.version ?? entry.client.initializeResult?.serverInfo?.version ?? ''
		}));
	}

	async restart(): Promise<void> {
		this.services.resolver.clear();
		this.schedule(true);
		await this.pending;
	}

	async dispose(): Promise<void> {
		this.disposed = true;
		clearTimeout(this.timer);
		for (const listener of this.listeners) {
			listener.dispose();
		}
		await this.pending;
		await this.stopAll();
		this.moduleSources.dispose();
	}

	private schedule(reset = false): void {
		if (this.disposed) return;
		this.reconciliationRequested = true;
		this.resetRequested ||= reset;
		if (this.reconciling) return;
		this.reconciling = true;
		this.pending = this.pending.then(async () => {
			try {
				// Reconcile the latest editor state once per burst. Events arriving
				// during an await require another pass; explicit resets must survive it.
				while (this.reconciliationRequested && !this.disposed) {
					const reset = this.resetRequested;
					this.reconciliationRequested = false;
					this.resetRequested = false;
					try {
						if (reset) {
							await this.stopAll();
						}
						await this.reconcile();
					}
					catch (error)
					{
						this.services.output.error(String(error));
					}
				}
			}
			finally
			{
				this.reconciling = false;
			}
		});
	}

	private key(uri: vscode.Uri): string {
		return vscode.workspace.getWorkspaceFolder(uri)?.uri.toString() ?? uri.toString();
	}

	private owns(key: string, uri: vscode.Uri): boolean {
		return uri.scheme === 'file' && this.key(uri) === key;
	}

	private synchronizes(key: string, document: vscode.TextDocument, options: Record<string, unknown>): boolean {
		if (!this.entries.has(key) || document.uri.scheme !== 'file' || document.languageId !== 'veroscript' || document.isClosed) return false;
		if (this.owns(key, document.uri)) return true;
		const directories = [...configuredPaths(options, 'importPaths'), ...configuredPaths(options, 'stdlibSearchPaths')];
		return directories.some(directory => containsPath(directory, document.uri.fsPath))
			|| configuredPaths(options, 'implicitImportPaths').some(file => path.relative(file, document.uri.fsPath) === '');
	}

	private synchronizeDocuments(key: string, entry: ClientEntry): void {
		const options = this.currentOptions(key);
		entry.client.selectSynchronizedDocuments(vscode.workspace.textDocuments
			.filter(document => this.synchronizes(key, document, options)).map(document => document.uri));
	}

	private middleware(key: string): Middleware {
		const owns = (uri: vscode.Uri) => this.owns(key, uri)
			|| (this.entries.get(key)?.client !== undefined && this.moduleSources.owner(uri) === this.entries.get(key)?.client);
		return {
			handleDiagnostics: (uri, diagnostics, next) => {
				if (owns(uri)) {
					next(uri, diagnostics);
				}
			},
			provideCompletionItem: async (document, position, context, token, next) => {
				if (!owns(document.uri)) return undefined;
				const result = await next(document, position, context, token);
				if (!result || token.isCancellationRequested) return undefined;
				if (vscode.workspace.getConfiguration('editor', document).get<boolean>('suggestOnTriggerCharacters', true)) {
					for (const item of Array.isArray(result) ? result : result.items) {
						continueNamespaceCompletion(item);
					}
				}
				return result;
			},
			resolveCompletionItem: async (item, token, next) => {
				const result = await next(item, token);
				return result && !token.isCancellationRequested ? presentCompletion(result) : result;
			},
			provideHover: async (document, position, token, next) => {
				if (!owns(document.uri)) return undefined;
				const result = await next(document, position, token);
				return result && !token.isCancellationRequested ? presentHover(result) : undefined;
			},
			provideSignatureHelp: (document, position, context, token, next) => owns(document.uri) ? next(document, position, context, token) : undefined,
			provideDefinition: (document, position, token, next) => owns(document.uri) ? next(document, position, token) : undefined,
			provideDeclaration: (document, position, token, next) => owns(document.uri) ? next(document, position, token) : undefined,
			provideTypeDefinition: (document, position, token, next) => owns(document.uri) ? next(document, position, token) : undefined,
			provideImplementation: (document, position, token, next) => owns(document.uri) ? next(document, position, token) : undefined,
			provideReferences: (document, position, context, token, next) => owns(document.uri) ? next(document, position, context, token) : undefined,
			provideDocumentHighlights: (document, position, token, next) => owns(document.uri) ? next(document, position, token) : undefined,
			provideDocumentSymbols: (document, token, next) => owns(document.uri) ? next(document, token) : undefined,
			provideCodeActions: (document, range, context, token, next) => owns(document.uri) ? next(document, range, context, token) : undefined,
			provideDocumentFormattingEdits: (document, options, token, next) => owns(document.uri) ? next(document, options, token) : undefined,
			provideDocumentRangeFormattingEdits: (document, range, options, token, next) => owns(document.uri) ? next(document, range, options, token) : undefined,
			provideOnTypeFormattingEdits: (document, position, ch, options, token, next) => owns(document.uri) ? next(document, position, ch, options, token) : undefined,
			provideRenameEdits: (document, position, name, token, next) => owns(document.uri) ? next(document, position, name, token) : undefined,
			prepareRename: (document, position, token, next) => owns(document.uri) ? next(document, position, token) : undefined,
			provideDocumentLinks: (document, token, next) => owns(document.uri) ? next(document, token) : undefined,
			provideFoldingRanges: (document, context, token, next) => owns(document.uri) ? next(document, context, token) : undefined,
			provideSelectionRanges: (document, positions, token, next) => owns(document.uri) ? next(document, positions, token) : undefined,
			provideDocumentSemanticTokens: (document, token, next) => owns(document.uri) ? next(document, token) : undefined,
			provideDocumentSemanticTokensEdits: (document, previous, token, next) => owns(document.uri) ? next(document, previous, token) : undefined,
			prepareTypeHierarchy: (document, position, token, next) => owns(document.uri) ? next(document, position, token) : undefined,
			provideWorkspaceSymbols: async (query, token, next) => {
				const symbols = await next(query, token);
				return symbols?.filter(symbol => owns(symbol.location.uri));
			},
			workspace: {
				// Settings are normalized and pushed explicitly, never forwarded twice.
				didChangeConfiguration: async () => {},
				workspaceFolders: async () => {
					const folder = vscode.workspace.workspaceFolders?.find(item => item.uri.toString() === key);
					return folder ? [{ uri: folder.uri.toString(), name: folder.name }] : [];
				},
				didChangeWorkspaceFolders: async () => {},
				configuration: async params => params.items.map(() => this.currentOptions(key))
			}
		};
	}

	private currentOptions(key: string): Record<string, unknown> {
		const entry = this.entries.get(key);
		return entry ? JSON.parse(entry.settings) as Record<string, unknown> : {};
	}

	private async reconcile(): Promise<void> {
		const contexts = new Map<string, AnalysisContext>();
		for (const document of vscode.workspace.textDocuments) {
			if (document.languageId === 'veroscript' && document.uri.scheme === 'file' && !document.isClosed) {
				try {
					const key = this.key(document.uri);
					if (!contexts.has(key)) {
						contexts.set(key, this.services.analysisContext(document.uri));
					}
				}
				catch (error)
				{
					this.services.output.error(String(error));
				}
			}
		}
		for (const [key, entry] of this.entries) {
			if (!contexts.has(key) || !contexts.get(key)?.settings.enabled) {
				await this.stop(key, entry);
			}
		}
		for (const [key, context] of contexts) {
			if (!context.settings.enabled || this.disposed) {
				continue;
			}
			try {
				const { settings, compileOverrides } = context;
				const runtime = settings.executablePath ? undefined : await this.services.runtime(context);
				const command = runtime?.lsp ?? settings.executablePath;
				const compile = runtime ? mergedCompileOptions(settings, {}, context.base, runtime.stdlib)
					: Object.fromEntries(compileOverrides.map(key => [key, settings[key]]));
				const options = { ...compile, ...settings.limits };
				const encoded = JSON.stringify(options);
				// Start a new connection when an explicit override is removed so the
				// host's original value is restored.
				const signature = JSON.stringify([command, settings.arguments, compileOverrides]);
				let entry = this.entries.get(key);
				if (entry && (entry.signature !== signature || entry.runtime !== runtime)) {
					await this.stop(key, entry);
					entry = undefined;
				}
				if (entry) {
					if (entry.settings !== encoded) {
						entry.settings = encoded;
						this.watch(entry);
						if (entry.client.state === State.Running) {
							this.synchronizeDocuments(key, entry);
						}
						await entry.client.sendNotification('workspace/didChangeConfiguration', { settings: { veroscript: options } });
					}
				}
				else
				{
					if (!runtime) {
						if (!vscode.workspace.isTrusted) {
							throw new Error(vscode.l10n.t('The configured language server requires a trusted workspace.'));
						}
						await requireFile(command, true);
					}
					const origin = randomUUID();
					const client = new ScopedLanguageClient(`veroscript:${key}`, context, command, () => this.currentOptions(key), this.middleware(key), this.services.output,
						this.moduleSources.uriConverters(origin));
					const created: ClientEntry = { client, context, runtime, command, signature, settings: encoded, watchers: [], moduleSource: this.moduleSources.register(origin, client) };
					this.entries.set(key, created);
					created.watchers.push(client.onDidChangeState(() => this.updateStatus()));
					try {
						await client.start();
						if (client.state !== State.Running)
						{
							throw new Error(`Language server did not initialize: ${command}`);
						}
						this.watch(created);
					}
					catch (error)
					{
						try {
							await this.stop(key, created);
						}
						catch (cleanupError)
						{
							this.services.output.error(String(cleanupError));
						}
						throw error;
					}
				}
				const current = this.entries.get(key);
				if (current?.client.state === State.Running) {
					this.synchronizeDocuments(key, current);
				}
				this.entries.get(key)?.client.selectDocuments(vscode.workspace.textDocuments
					.filter(document => document.languageId === 'veroscript' && this.owns(key, document.uri) && !document.isClosed)
					.map(document => document.uri));
			}
			catch (error)
			{
				const entry = this.entries.get(key);
				if (entry) {
					await this.stop(key, entry);
				}
				this.services.output.error(`${key}: ${String(error)}`);
			}
		}
		this.updateStatus();
	}

	private watch(entry: ClientEntry): void {
		for (const watcher of entry.watchers) {
			watcher.dispose();
		}
		entry.watchers = [entry.client.onDidChangeState(event => {
			this.updateStatus();
			if (event.newState === State.Running) {
				this.schedule();
			}
		})];
		const options = this.currentOptions(this.key(entry.context.uri));
		const directories = watchDirectories([entry.context.base, ...configuredPaths(options, 'importPaths'), ...configuredPaths(options, 'stdlibSearchPaths')]);
		const catalogs = configuredPaths(options, 'bindingCatalogPaths');
		const files = [...configuredPaths(options, 'implicitImportPaths'), ...catalogs];
		const seen = new Set<string>();
		const add = (base: string, pattern: string, catalog = false) => {
			const identity = `${base}\0${pattern}`;
			if (seen.has(identity)) return;
			seen.add(identity);
			const watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(base, pattern));
			const changed = (uri: vscode.Uri, type: number) => {
				if (this.disposed || entry.client.state !== State.Running) return;
				const operation = catalog
					? entry.client.sendNotification('workspace/didChangeConfiguration', { settings: { veroscript: this.currentOptions(this.key(entry.context.uri)) } })
					: entry.client.sendNotification('workspace/didChangeWatchedFiles', { changes: [{ uri: uri.toString(), type }] });
				void operation.catch(error => this.services.output.error(String(error)));
			};
			entry.watchers.push(watcher, watcher.onDidCreate(uri => changed(uri, 1)), watcher.onDidChange(uri => changed(uri, 2)), watcher.onDidDelete(uri => changed(uri, 3)));
		};
		for (const directory of directories) {
			add(directory, '**/*.{vs,[vV][sS][lL][iI][bB]}');
		}
		for (const file of files) {
			const catalog = catalogs.includes(file);
			const extension = process.platform === 'win32' ? path.extname(file).toLowerCase() : path.extname(file);
			if (!catalog && (extension === '.vs' || extension.toLowerCase() === '.vslib') && directories.some(directory => containsPath(directory, file))) {
				continue;
			}
			add(path.dirname(file), globLiteral(path.basename(file)), catalog);
		}
	}

	private async stop(key: string, entry: ClientEntry): Promise<void> {
		this.entries.delete(key);
		for (const watcher of entry.watchers) {
			watcher.dispose();
		}
		try {
			await entry.client.dispose();
		}
		finally
		{
			entry.moduleSource.dispose();
		}
	}

	private async stopAll(): Promise<void> {
		const results = await Promise.allSettled([...this.entries].map(([key, entry]) => this.stop(key, entry)));
		for (const result of results) {
			if (result.status === 'rejected') {
				this.services.output.error(String(result.reason));
			}
		}
	}

	private updateStatus(): void {
		const document = vscode.window.activeTextEditor?.document;
		if (document?.languageId !== 'veroscript') {
			this.status.hide();
			return;
		}
		const moduleOwner = this.moduleSources.owner(document.uri);
		const entry = document.uri.scheme === 'veroscript-module'
			? [...this.entries.values()].find(value => value.client === moduleOwner)
			: this.entries.get(this.key(document.uri));
		if (!entry && document.uri.scheme === 'veroscript-module') {
			this.status.hide();
			return;
		}
		const version = entry?.runtime?.version ?? entry?.client.initializeResult?.serverInfo?.version;
		const configured = vscode.workspace.getConfiguration('veroscript', document).get<unknown>('lsp.executablePath');
		const custom = typeof configured === 'string' && configured;
		this.status.text = entry?.client.state === State.Running ? (version ? `VeroScript ${version}` : 'VeroScript') : 'VeroScript $(warning)';
		this.status.tooltip = entry ? `${State[entry.client.state]} — ${entry.runtime?.root ?? entry.command}`
			: custom || vscode.l10n.t('Select a Runtime or open VeroScript output for details.');
		this.status.command = entry || custom ? 'veroscript.showOutput' : 'veroscript.selectRuntime';
		this.status.show();
	}

	private disposed = false;
	private reconciling = false;
	private reconciliationRequested = false;
	private resetRequested = false;
	private pending: Promise<void> = Promise.resolve();
	private timer: ReturnType<typeof setTimeout> | undefined;
	private readonly listeners: vscode.Disposable[] = [];
	private readonly entries = new Map<string, ClientEntry>();
	private readonly moduleSources = new ModuleSources();
}
