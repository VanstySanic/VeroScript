// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { editorSourceLimit, Span } from './markup';

export interface StyleRule extends Span {
	readonly selector: string;
	readonly bodyStart: number;
	readonly bodyEnd: number;
}

export interface StyleReference extends Span {
	readonly value: string;
}

export interface StyleModel {
	readonly rules: readonly StyleRule[];
	readonly opaque: readonly Span[];
	readonly separators: readonly { readonly at: number; readonly value: string }[];
	readonly values: readonly Span[];
	readonly references: readonly StyleReference[];
}

/** Consume each URL once, matching native escape handling even in incomplete documents. */
function styleReference(source: string, at: number): { next: number; reference?: StyleReference } {
	while (source[at] === ' ' || source[at] === '\t') {
		++at;
	}
	const quote = source[at] === '"' || source[at] === "'" ? source[at++] : undefined;
	const start = at;
	let closed = false;
	while (at < source.length) {
		const character = source[at++]!;
		if (character === (quote ?? ')')) {
			closed = true;
			break;
		}
		if (character === '\\' && at < source.length) {
			++at;
		}
	}
	const end = closed ? at - 1 : at;
	if (quote) {
		while (source[at] === ' ' || source[at] === '\t') {
			++at;
		}
		closed = closed && source[at] === ')';
		if (closed) {
			++at;
		}
	}
	const decoded = source.slice(start, end).replace(/\\([\s\S])/g, '$1');
	const value = quote ? decoded : decoded.replace(/[ \t]+$/, '');
	return { next: at, reference: closed && value ? { start, end, value } : undefined };
}

/** Locate CSS boundaries while keeping comments, strings and function arguments opaque. */
export function parseStyle(source: string): StyleModel {
	const rules: StyleRule[] = [];
	const opaque: Span[] = [];
	const separators: { at: number; value: string }[] = [];
	const values: Span[] = [];
	const references: StyleReference[] = [];
	if (source.length > editorSourceLimit) return { rules, opaque: [{ start: 0, end: source.length }], separators, values, references };
	let start = 0;
	let body = -1;
	let depth = 0;
	let parentheses = 0;
	let valueStart = -1;
	for (let at = 0; at < source.length; ++at) {
		const character = source[at]!;
		if (source.startsWith('/*', at)) {
			const end = source.indexOf('*/', at + 2);
			const next = end < 0 ? source.length : end + 2;
			opaque.push({ start: at, end: next });
			at = next - 1;
			continue;
		}
		if (depth && valueStart >= 0 && (character === 'u' || character === 'U') && source.slice(at, at + 4).toLowerCase() === 'url(') {
			const result = styleReference(source, at + 4);
			if (result.reference) {
				references.push(result.reference);
			}
			opaque.push({ start: at, end: result.next });
			at = result.next - 1;
			continue;
		}
		if (character === '"' || character === "'") {
			const begin = at++;
			while (at < source.length && source[at] !== character) {
				at += source[at] === '\\' ? 2 : 1;
			}
			opaque.push({ start: begin, end: Math.min(at + 1, source.length) });
			continue;
		}
		if (character === '(') {
			++parentheses;
		}
		else if (character === ')')
		{
			parentheses = Math.max(0, parentheses - 1);
		}
		if (parentheses) {
			continue;
		}
		if (character === '{') {
			if (depth++ === 0) {
				body = at + 1;
			}
		}
		else if (character === '}')
		{
			if (depth && --depth === 0) {
				if (valueStart >= 0) {
					values.push({ start: valueStart, end: at });
					valueStart = -1;
				}
				rules.push({ start, end: at + 1, bodyStart: body, bodyEnd: at, selector: source.slice(start, body - 1).replace(/\/\*[\s\S]*?\*\//g, '').trim() });
				start = at + 1;
				body = -1;
			}
		}
		else if (depth === 1 && (character === ':' || character === ';'))
		{
			separators.push({ at, value: character });
			if (character === ':' && valueStart < 0) {
				valueStart = at + 1;
			}
			else if (character === ';' && valueStart >= 0)
			{
				values.push({ start: valueStart, end: at });
				valueStart = -1;
			}
		}
	}
	if (body >= 0) {
		if (valueStart >= 0) {
			values.push({ start: valueStart, end: source.length });
		}
		rules.push({ start, end: source.length, bodyStart: body, bodyEnd: source.length, selector: source.slice(start, body - 1).replace(/\/\*[\s\S]*?\*\//g, '').trim() });
	}
	return { rules, opaque, separators, values, references };
}

export function styleContext(model: StyleModel, source: string, offset: number): { kind: 'selector' | 'property' | 'value'; property?: string } | undefined {
	if (model.opaque.some(span => span.start <= offset && offset < span.end)) return undefined;
	const rule = model.rules.find(rule => offset >= rule.bodyStart && offset <= rule.bodyEnd);
	if (!rule) return { kind: 'selector' };
	const separators = model.separators.filter(separator => separator.at >= rule.bodyStart && separator.at < offset);
	const boundary = separators.findLast(separator => separator.value === ';')?.at ?? rule.bodyStart - 1;
	const colon = separators.find(separator => separator.value === ':' && separator.at > boundary)?.at;
	return colon === undefined ? { kind: 'property' }
		: { kind: 'value', property: source.slice(boundary + 1, colon).replace(/\/\*[\s\S]*?\*\//g, '').trim() };
}

export const styleStates = ['root', 'hover', 'active', 'focus', 'focus-within', 'disabled', 'checked', 'read-only'];
