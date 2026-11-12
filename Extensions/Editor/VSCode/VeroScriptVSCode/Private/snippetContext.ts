// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { continuedLineEnd } from './lexical';

interface Delimiter {
	readonly closing: string;
	readonly scope?: SnippetScope;
	readonly expression?: boolean;
	readonly declaration?: boolean;
}

export type SnippetScope = 'file' | 'namespace' | 'type' | 'statement';

// This is a conservative lexical gate for templates, not a second language
// parser. Unknown blocks, unfinished expressions and continued directives
// do not offer templates.
export function snippetScope(before: string, cancelled: () => boolean = () => false): SnippetScope | undefined {
	const tokens = /(?<lineComment>\/\/(?:\\(?:\r\n|\r|\n)|[^\r\n])*)|\/\*[\s\S]*?(?<blockEnd>\*\/|$)|\br"[^"]*(?<rawEnd>"|$)|[Ll]?"(?:\\[\s\S]|[^"\\])*(?<stringEnd>"|$)|'(?:\\[\s\S]|[^'\\])*(?<characterEnd>'|$)|(?<directive>(?:^|(?<=[\r\n]))[ \t]*#)|[A-Za-z_]\w*|[^\s]/g;
	const delimiters: Delimiter[] = [];
	let declaration: 'namespace' | 'type' | 'other' | undefined;
	let expressionBody = false;
	let previous = '';
	let directiveEnd = -1;
	for (const match of before.matchAll(tokens)) {
		if (cancelled()) return undefined;
		const groups = match.groups!;
		// A physical line beginning with # may still belong to this directive.
		// Compute each logical line boundary once, including long macro bodies.
		if (groups.directive !== undefined && match.index >= directiveEnd) {
			directiveEnd = continuedLineEnd(before, match.index);
			if (directiveEnd === before.length) return undefined;
			continue;
		}
		if (groups.lineComment !== undefined) {
			if (match.index + match[0].length === before.length) return undefined;
			continue;
		}
		if (groups.blockEnd !== undefined) {
			if (!groups.blockEnd) return undefined;
			continue;
		}
		if (groups.rawEnd !== undefined || groups.stringEnd !== undefined || groups.characterEnd !== undefined) {
			if (groups.rawEnd === '' || groups.stringEnd === '' || groups.characterEnd === '') return undefined;
			if (match.index >= directiveEnd) {
				previous = 'literal';
			}
			continue;
		}
		// Still tokenize literals/comments inside a directive: a block comment
		// may outlive the directive and must remain opaque on following lines.
		if (match.index < directiveEnd) {
			continue;
		}
		const token = match[0];
		if (token === 'namespace') {
			declaration = 'namespace';
		}
		else if (token === 'interface' || token === 'enum')
		{
			declaration = 'other';
		}
		else if ((token === 'class' || token === 'struct') && declaration !== 'other')
		{
			declaration = 'type';
		}
		else if (token === '>' && previous === '-')
		{
			expressionBody = true;
		}
		if (token === '(' || token === '[') {
			delimiters.push({ closing: token === '(' ? ')' : ']', expression: token === '(' && ['fn', 'function', ']'].includes(previous) });
		}
		else if (token === '{')
		{
			const parent = delimiters.at(-1)?.scope;
			const block = previous === ')' || expressionBody || ['else', 'do', 'try', 'finally'].includes(previous)
				|| (parent === 'statement' && ['', ';', '{', '}'].includes(previous));
			delimiters.push({ closing: '}', scope: declaration === 'other' ? undefined : declaration ?? (block ? 'statement' : undefined),
				expression: expressionBody, declaration: declaration !== undefined });
			declaration = undefined;
			expressionBody = false;
		}
		else if (token === ')' || token === ']' || token === '}')
		{
			if (delimiters.at(-1)?.closing !== token) return undefined;
			const closed = delimiters.pop()!;
			if (token === ')') {
				expressionBody = closed.expression === true;
			}
			if (token === '}') {
				declaration = undefined;
				expressionBody = false;
				// An expression's closing brace is not a new statement boundary. Type
				// declarations permit an optional semicolon in VeroScript.
				if (closed.expression || (!closed.declaration && closed.scope !== 'namespace' && closed.scope !== 'statement')) {
					previous = 'value';
					continue;
				}
			}
		}
		else if (token === ';')
		{
			declaration = undefined;
			expressionBody = false;
		}
		previous = token;
	}
	if (cancelled() || !['', ';', '{', '}'].includes(previous)) return undefined;
	const innermost = delimiters.at(-1);
	return innermost ? innermost.scope : 'file';
}
