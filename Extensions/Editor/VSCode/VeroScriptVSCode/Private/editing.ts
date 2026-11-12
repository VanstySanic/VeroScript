// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as vscode from 'vscode';
import { continuedLineEnd } from './lexical';

// Skip literals, comments and directives before recognizing the opening brace.
// This runs only for an otherwise eligible empty block, never on every keystroke.
function areCodeBraces(before: string, offsets: Set<number>): boolean {
	const tokens = /\/\/(?:\\(?:\r\n|\r|\n)|[^\r\n])*|\/\*[\s\S]*?(?:\*\/|$)|\br"[^"]*(?:"|$)|[Ll]?"(?:\\[\s\S]|[^"\\])*(?:"|$)|'(?:\\[\s\S]|[^'\\])*(?:'|$)|(?<directive>(?:^|(?<=[\r\n]))[ \t]*#)|\{/g;
	let directiveEnd = -1;
	for (const match of before.matchAll(tokens)) {
		if (match.groups?.directive !== undefined && match.index >= directiveEnd) {
			directiveEnd = continuedLineEnd(before, match.index);
		}
		else if (match[0] === '{' && match.index >= directiveEnd)
		{
			offsets.delete(match.index);
		}
	}
	return offsets.size === 0;
}

export function registerEditing(): vscode.Disposable {
	return vscode.commands.registerCommand('veroscript.insertNewline', async () => {
		const editor = vscode.window.activeTextEditor;
		const document = editor?.document;
		const fallback = () => vscode.commands.executeCommand('type', { text: '\n' });
		if (!editor || !document || document.languageId !== 'veroscript' || document.uri.scheme === 'veroscript-module'
			|| !vscode.workspace.getConfiguration('veroscript').get<boolean>('bracesOnNewLine', true)) {
			return fallback();
		}
		const ranges: vscode.Range[] = [];
		const offsets = new Set<number>();
		let indentation: string | undefined;
		let closing: string | undefined;
		let end = new vscode.Position(0, 0);
		for (const selection of editor.selections) {
			if (!selection.isEmpty) return fallback();
			const line = document.lineAt(selection.active.line);
			const before = line.text.slice(0, selection.active.character);
			const after = line.text.slice(selection.active.character);
			const header = /^([ \t]*)(\S.*?)[ \t]*\{[ \t]*$/.exec(before);
			// Ambiguous initializers and several statements on one line use ordinary
			// Enter. Allow the unfinished void declaration shown while typing, too.
			if (!header || /[{}]/.test(header[2]!) || (/;/.test(header[2]!) && !/^for[ \t]*\(/.test(header[2]!))
				|| !/^[ \t]*(?:\}[ \t]*)?$/.test(after)
				|| !(/\)(?:[ \t]+(?:const|override|final))*$/.test(header[2]!)
					|| /^(?:class|struct|interface|enum|namespace)\b/.test(header[2]!)
					|| /^(?:else|do|try|finally)$/.test(header[2]!) || /^void[ \t]+[A-Za-z_]\w*$/.test(header[2]!))) {
				return fallback();
			}
			// The indentation of a continued header belongs to an earlier line.
			// Do not move its brace using the continuation's indentation. Quoted
			// parentheses may conservatively keep ordinary Enter as well.
			if (header[2]!.startsWith(':')) return fallback();
			let depth = 0;
			for (const character of header[2]!) {
				if (character === '(') {
					++depth;
				}
				else if (character === ')' && --depth < 0)
				{
					return fallback();
				}
			}
			if (depth !== 0) return fallback();
			const brace = before.lastIndexOf('{');
			const offset = document.offsetAt(new vscode.Position(line.lineNumber, brace + 1));
			// Bound work for unusually large buffers; ordinary Enter remains available.
			if (offset > 1024 * 1024) {
				return fallback();
			}
			const suffix = after.trimStart();
			if ((closing !== undefined && closing !== suffix) || (indentation !== undefined && indentation !== header[1])) return fallback();
			indentation = header[1]!;
			closing = suffix;
			offsets.add(offset - 1);
			end = end.isBefore(selection.active) ? selection.active : end;
			ranges.push(new vscode.Range(line.lineNumber, header[1]!.length + header[2]!.length, line.lineNumber, line.text.length));
		}
		if (!areCodeBraces(document.getText(new vscode.Range(new vscode.Position(0, 0), end)), offsets)) return fallback();
		const tabSize = typeof editor.options.tabSize === 'number' ? editor.options.tabSize : 4;
		const indentSize = typeof editor.options.indentSize === 'number' ? editor.options.indentSize : tabSize;
		const unit = editor.options.insertSpaces ? ' '.repeat(indentSize) : '\t';
		// Explicit whitespace avoids VS Code rebasing later cursors against the
		// first insertion. Snippets retain native EOLs, undo and final caret state.
		const eol = document.eol === vscode.EndOfLine.CRLF ? '\r\n' : '\n';
		const snippet = new vscode.SnippetString(`${eol}${indentation}{${eol}${indentation}${unit}$0` + (closing ? `${eol}${indentation}${closing}` : ''));
		return editor.insertSnippet(snippet, ranges, { keepWhitespace: true, undoStopBefore: true, undoStopAfter: true });
	});
}
