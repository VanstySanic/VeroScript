// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as vscode from 'vscode';
import { builtinNoteMarkdown, hoverMarkdown, isBuiltinTypeDetail, signatureMarkdown } from './documentation';

function markdown(value: string, original?: vscode.MarkdownString): vscode.MarkdownString {
	const result = new vscode.MarkdownString(value);
	if (original) {
		result.baseUri = original.baseUri;
		result.isTrusted = original.isTrusted;
		result.supportHtml = original.supportHtml;
		result.supportThemeIcons = original.supportThemeIcons;
	}
	return result;
}

export function presentHover(hover: vscode.Hover): vscode.Hover {
	const contents = hover.contents.map(content => {
		if (typeof content !== 'string' && !(content instanceof vscode.MarkdownString)) return content;
		const value = hoverMarkdown(typeof content === 'string' ? content : content.value, vscode.l10n.t('Built-in symbol.'));
		return value === undefined ? content : markdown(value, typeof content === 'string' ? undefined : content);
	});
	return new vscode.Hover(contents, hover.range);
}

export function presentCompletion(item: vscode.CompletionItem): vscode.CompletionItem {
	const detail = item.detail;
	if (!detail) return item;
	// Metadata labels describe the symbol; only actual declarations use a code
	// block. Keep soft-builtin metadata literal and localized like native hover.
	const builtinNote = builtinNoteMarkdown(detail, vscode.l10n.t('Built-in symbol.'));
	const signature = builtinNote ?? (isBuiltinTypeDetail(detail) ? `**${detail}**` : signatureMarkdown(detail));
	const documentation = item.documentation;
	if (documentation instanceof vscode.MarkdownString && documentation.value.startsWith(signature)) return item;
	const result = markdown(signature, documentation instanceof vscode.MarkdownString ? documentation : undefined);
	const text = typeof documentation === 'string' ? documentation : documentation?.value;
	// Resolve may return the signature again as documentation. Do not display it
	// twice; genuine descriptions retain the server's plain-text/Markdown kind.
	if (text && text !== detail && !(builtinNote && text === 'soft builtin')) {
		result.appendMarkdown('\n\n');
		if (typeof documentation === 'string') {
			const note = documentation === 'soft builtin' ? builtinNoteMarkdown(documentation, vscode.l10n.t('Built-in symbol.')) : undefined;
			if (note) {
				result.appendMarkdown(note);
			}
			else
			{
				result.appendText(documentation);
			}
		}
		else
		{
			result.appendMarkdown(text);
		}
	}
	item.documentation = result;
	return item;
}

export function continueNamespaceCompletion(item: vscode.CompletionItem): void {
	if (item.command || item.kind !== vscode.CompletionItemKind.Module) return;
	const insertion = typeof item.insertText === 'string' ? item.insertText : item.insertText?.value;
	if (!insertion || !/^[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*::$/.test(insertion)) return;
	// An accepted text edit is not a typed ':' trigger. Ask VS Code to show the
	// namespace's members after insertion, preserving native edits and ranking.
	item.command = { title: 'Suggest', command: 'editor.action.triggerSuggest' };
}
