// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { randomUUID } from 'node:crypto';
import * as vscode from 'vscode';
import { InitializeResult, State, StateChangeEvent } from 'vscode-languageclient/node';

export interface ModuleSourceClient {
	readonly initializeResult: InitializeResult | undefined;
	readonly state: State;
	readonly onDidChangeState: vscode.Event<StateChangeEvent>;
	sendRequest<T>(method: string, params: unknown, token: vscode.CancellationToken): Promise<T>;
}

interface ModuleSourceRegistration {
	readonly client: ModuleSourceClient;
	readonly states: vscode.Disposable;
	lifetime: string;
}

interface ModuleSource {
	readonly uri: string;
	readonly languageId: string;
	readonly text: string;
	readonly readOnly: boolean;
	readonly compileFingerprint: string;
}

/** Routes immutable module snapshots to the language server that returned them. */
export class ModuleSources implements vscode.Disposable, vscode.TextDocumentContentProvider {
	constructor() {
		this.provider = vscode.workspace.registerTextDocumentContentProvider('veroscript-module', this);
		this.documents = vscode.workspace.onDidOpenTextDocument(document => {
			if (this.owner(document.uri) && document.languageId !== 'veroscript') {
				// Module identities need not have a .vs suffix. Honor the source
				// language even when filename-based editor detection chose plaintext.
				void vscode.languages.setTextDocumentLanguage(document, 'veroscript').then(undefined, error => {
					if (!document.isClosed) {
						console.error(error);
					}
				});
			}
		});
	}

	register(origin: string, client: ModuleSourceClient): vscode.Disposable {
		this.clients.get(origin)?.states.dispose();
		const registration: ModuleSourceRegistration = {
			client, lifetime: randomUUID(),
			states: client.onDidChangeState(event => {
				// Automatic restarts reuse the client and can retain the fingerprint.
				// A pending response must never cross the native connection lifetime.
				if (event.newState === State.Starting) {
					registration.lifetime = randomUUID();
				}
			})
		};
		this.clients.set(origin, registration);
		return new vscode.Disposable(() => {
			registration.states.dispose();
			if (this.clients.get(origin) === registration) {
				this.clients.delete(origin);
			}
		});
	}

	uriConverters(origin: string) {
		return {
			protocol2Code: (value: string): vscode.Uri => {
				const uri = vscode.Uri.parse(value);
				if (uri.scheme !== 'veroscript-module') return uri;
				const capability = this.capability(origin);
				// Identical native URIs can refer to different modules in two roots.
				// Keep the server lifetime and compile snapshot in the editor identity.
				return uri.with({ query: new URLSearchParams({ origin, source: value,
					lifetime: this.clients.get(origin)?.lifetime ?? '',
					snapshot: capability?.compileFingerprint ?? '' }).toString() });
			},
			code2Protocol: (uri: vscode.Uri): string => {
				if (uri.scheme !== 'veroscript-module') return uri.toString();
				const parameters = new URLSearchParams(uri.query);
				return parameters.get('origin') === origin ? parameters.get('source') ?? uri.toString() : uri.toString();
			}
		};
	}

	owner(uri: vscode.Uri): ModuleSourceClient | undefined {
		if (uri.scheme !== 'veroscript-module') return undefined;
		const parameters = new URLSearchParams(uri.query);
		const origin = parameters.get('origin') ?? '';
		const registration = this.clients.get(origin);
		if (!registration || registration.client.state !== State.Running
			|| parameters.get('lifetime') !== registration.lifetime
			|| parameters.get('snapshot') !== this.capability(origin)?.compileFingerprint) {
			return undefined;
		}
		return registration.client;
	}

	async provideTextDocumentContent(uri: vscode.Uri, token: vscode.CancellationToken): Promise<string> {
		const parameters = new URLSearchParams(uri.query);
		const origin = parameters.get('origin') ?? '';
		const source = parameters.get('source');
		const snapshot = parameters.get('snapshot');
		const lifetime = parameters.get('lifetime');
		const registration = this.clients.get(origin);
		const client = registration?.client;
		const capability = this.capability(origin);
		const unavailable = () => new Error(vscode.l10n.t('The module source is unavailable or its snapshot has changed. Navigate to it again from the owning workspace.'));
		if (!client || client.state !== State.Running || lifetime !== registration?.lifetime
			|| !source || !capability || snapshot !== capability.compileFingerprint) {
			throw unavailable();
		}
		if (token.isCancellationRequested) {
			throw new vscode.CancellationError();
		}
		const result = await client.sendRequest<ModuleSource | null>(capability.moduleSourceMethod, { uri: source }, token);
		if (token.isCancellationRequested) {
			throw new vscode.CancellationError();
		}
		if (this.clients.get(origin) !== registration || registration?.lifetime !== lifetime || client.state !== State.Running
			|| this.capability(origin)?.compileFingerprint !== snapshot
			|| !result || result.uri !== source || result.compileFingerprint !== snapshot
			|| result.readOnly !== true || result.languageId !== 'veroscript' || typeof result.text !== 'string') {
			throw unavailable();
		}
		return result.text;
	}

	dispose(): void {
		for (const registration of this.clients.values()) {
			registration.states.dispose();
		}
		this.clients.clear();
		this.documents.dispose();
		this.provider.dispose();
	}

	private capability(origin: string): { compileFingerprint: string; moduleSourceMethod: string } | undefined {
		const value = this.clients.get(origin)?.client.initializeResult?.capabilities.experimental?.veroscriptModuleEnvironment;
		if (!value || typeof value.compileFingerprint !== 'string' || typeof value.moduleSourceMethod !== 'string' || !value.moduleSourceMethod) return undefined;
		return value;
	}

	private readonly clients = new Map<string, ModuleSourceRegistration>();
	private readonly provider: vscode.Disposable;
	private readonly documents: vscode.Disposable;
}
