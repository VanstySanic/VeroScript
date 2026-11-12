// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { readFileSync } from 'node:fs';
import * as vscode from 'vscode';
import { snippetScope, SnippetScope } from './snippetContext';

interface Template {
	readonly prefix: string;
	readonly body: readonly string[];
	readonly description: string;
}

export function registerSnippets(context: vscode.ExtensionContext): vscode.Disposable {
	const templates = JSON.parse(readFileSync(context.asAbsolutePath('Resources/Snippets/veroscript.code-snippets'), 'utf8')) as Record<string, Template>;
	const scopes: Record<string, readonly SnippetScope[]> = {
		main: ['file'], hello: ['file'], import: ['file'],
		fn: ['file', 'namespace', 'type'], class: ['file', 'namespace'], for: ['statement']
	};
	return vscode.languages.registerCompletionItemProvider({ language: 'veroscript' }, {
		provideCompletionItems(document, position, token) {
			if (document.uri.scheme === 'veroscript-module') return [];
			const word = document.getWordRangeAtPosition(position, /[A-Za-z_]\w*/);
			const start = word?.start ?? position;
			const prefix = document.getText(new vscode.Range(start, position));
			const candidates = Object.entries(templates).filter(([, template]) => template.prefix.startsWith(prefix));
			if (token.isCancellationRequested || candidates.length === 0) return [];
			const scope = snippetScope(document.getText(new vscode.Range(new vscode.Position(0, 0), start)), () => token.isCancellationRequested);
			if (!scope) return [];
			return candidates.filter(([, template]) => scopes[template.prefix]?.includes(scope)).map(([name, template]) => {
				const item = new vscode.CompletionItem(template.prefix, vscode.CompletionItemKind.Snippet);
				item.detail = name;
				item.documentation = template.description;
				item.insertText = new vscode.SnippetString(template.body.join('\n'));
				item.range = new vscode.Range(start, word?.end ?? position);
				item.filterText = template.prefix;
				item.sortText = `zz_${template.prefix}`;
				return item;
			});
		}
	});
}
