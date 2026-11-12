// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export interface Span {
	readonly start: number;
	readonly end: number;
}

export interface Attribute extends Span {
	readonly name: string;
	readonly value?: string;
	readonly valueStart?: number;
	readonly valueEnd?: number;
}

export interface Tag extends Span {
	readonly name: string;
	readonly nameStart: number;
	readonly nameEnd: number;
	readonly closing: boolean;
	readonly selfClosing: boolean;
	readonly complete: boolean;
	readonly attributes: readonly Attribute[];
}

export interface Element {
	readonly tag: Tag;
	readonly parent?: number;
	end: number;
	closingStart?: number;
}

export interface MarkupIssue extends Span {
	readonly message: string;
	readonly args: readonly string[];
}

export interface Markup {
	readonly tags: readonly Tag[];
	readonly elements: readonly Element[];
	readonly opaque: readonly Span[];
	readonly issues: readonly MarkupIssue[];
}

export const editorSourceLimit = 2 * 1024 * 1024;

export function decodeAttribute(value: string): string {
	const names: Record<string, string> = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'" };
	return value.replace(/&(#x[\da-fA-F]+|#\d+|amp|lt|gt|quot|apos);/g, (original, entity: string) => {
		if (!entity.startsWith('#')) return names[entity]!;
		const code = entity.startsWith('#x') ? Number.parseInt(entity.slice(2), 16) : Number(entity.slice(1));
		return code > 0 && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff) ? String.fromCodePoint(code) : original;
	});
}

/** Tolerant structural model only. Native Gui owns schemas, expressions and compilation. */
export function parseMarkup(source: string): Markup {
	const tags: Tag[] = [];
	const elements: Element[] = [];
	const opaque: Span[] = [];
	const issues: MarkupIssue[] = [];
	const stack: number[] = [];
	let at = source.charCodeAt(0) === 0xfeff ? 1 : 0;
	let roots = 0;
	const issue = (message: string, start: number, end: number, ...args: string[]) => {
		if (issues.length < 100) {
			issues.push({ message, start, end, args });
		}
	};
	const whitespace = () => {
		while (at < source.length && /[ \t\r\n]/.test(source[at]!)) {
			++at;
		}
	};
	const name = () => {
		const start = at;
		while (at < source.length && (/[A-Za-z0-9_.:-]/.test(source[at]!) || source.charCodeAt(at) >= 128)) {
			++at;
		}
		return source.slice(start, at);
	};
	if (source.length > editorSourceLimit) {
		issue('Document exceeds the editor analysis limit.', 0, 0);
		return { tags, elements, opaque: [{ start: 0, end: source.length }], issues };
	}
	while (at < source.length) {
		if (source[at] !== '<') {
			++at;
			continue;
		}
		const start = at;
		const block = source.startsWith('<!--', at) ? ['-->', 4, 'Unterminated comment.']
			: source.startsWith('<![CDATA[', at) ? [']]>', 9, 'Unterminated CDATA section.']
			: source.startsWith('<?', at) ? ['?>', 2, 'Unterminated processing instruction.'] : undefined;
		if (block) {
			const end = source.indexOf(block[0] as string, at + (block[1] as number));
			at = end < 0 ? source.length : end + (block[0] as string).length;
			opaque.push({ start, end: at });
			if (end < 0) {
				issue(block[2] as string, start, at);
			}
			continue;
		}
		++at;
		const closing = source[at] === '/';
		if (closing) {
			++at;
		}
		const nameStart = at;
		const tagName = name();
		const nameEnd = at;
		const attributes: Attribute[] = [];
		const attributeNames = new Set<string>();
		let selfClosing = false;
		let complete = false;
		if (!tagName) {
			// Leave an unfinished '<' available to tag completion.
			tags.push({ start, end: at, name: '', nameStart, nameEnd, closing, selfClosing, complete, attributes });
			continue;
		}
		while (at < source.length) {
			whitespace();
			if (at === source.length) {
				break;
			}
			if (source.startsWith('/>', at) || source[at] === '>') {
				selfClosing = source[at] === '/';
				at += selfClosing ? 2 : 1;
				complete = true;
				break;
			}
			if (source[at] === '<') {
				break;
			}
			const attributeStart = at;
			const attributeName = name();
			if (!attributeName) {
				++at;
				continue;
			}
			whitespace();
			if (source[at] !== '=') {
				attributes.push({ name: attributeName, start: attributeStart, end: at });
				// Names in a partially typed header are not semantic errors.
				continue;
			}
			++at;
			whitespace();
			const quote = source[at];
			if (quote !== '"' && quote !== "'") {
				issue('Expected a quoted value for {0}.', attributeStart, at, attributeName);
				continue;
			}
			const valueStart = ++at;
			const end = source.indexOf(quote, at);
			const valueEnd = end < 0 ? source.length : end;
			at = end < 0 ? source.length : end + 1;
			if (attributeNames.has(attributeName)) {
				issue('Duplicate attribute: {0}.', attributeStart, at, attributeName);
			}
			attributeNames.add(attributeName);
			attributes.push({ name: attributeName, start: attributeStart, end: at,
				value: decodeAttribute(source.slice(valueStart, valueEnd)), valueStart, valueEnd });
		}
		const tag: Tag = { start, end: at, name: tagName, nameStart, nameEnd, closing, selfClosing, complete, attributes };
		tags.push(tag);
		if (closing) {
			const current = stack.at(-1);
			if (current === undefined || elements[current]!.tag.name !== tagName) {
				issue('Closing tag {0} does not match {1}.', start, at, tagName, current === undefined ? '' : elements[current]!.tag.name);
			}
			const matching = stack.findLastIndex(index => elements[index]!.tag.name === tagName);
			if (matching >= 0) {
				for (const index of stack.splice(matching)) {
					elements[index]!.end = at;
					elements[index]!.closingStart = start;
				}
			}
			continue;
		}
		const parent = stack.at(-1);
		if (parent === undefined && ++roots > 1) {
			issue('Only one root element is supported.', start, at);
		}
		const index = elements.length;
		elements.push({ tag, parent, end: selfClosing ? at : source.length });
		if (selfClosing || !complete) {
			continue;
		}
		if (stack.length >= 256) {
			issue('Markup nesting exceeds 256 levels.', start, at);
			break;
		}
		stack.push(index);
		if (tagName === 'script' || tagName === 'style') {
			// CDATA may contain a literal closing tag. It is one opaque XML token.
			const bodyStart = at;
			while (at < source.length && !(source.startsWith(`</${tagName}`, at) && /[\s>]/.test(source[at + tagName.length + 2] ?? ''))) {
				if (source.startsWith('<![CDATA[', at) || source.startsWith('<!--', at)) {
					const cdata = source.startsWith('<![CDATA[', at);
					const terminator = cdata ? ']]>' : '-->';
					const end = source.indexOf(terminator, at + (cdata ? 9 : 4));
					if (end < 0) {
						issue(cdata ? 'Unterminated CDATA section.' : 'Unterminated comment.', at, source.length);
					}
					at = end < 0 ? source.length : end + 3;
				}
				else
				{
					++at;
				}
			}
			opaque.push({ start: bodyStart, end: at });
		}
	}
	for (const index of stack) {
		const tag = elements[index]!.tag;
		issue('Unclosed tag: {0}.', tag.start, tag.end, tag.name);
	}
	return { tags, elements, opaque, issues };
}

export function tagAt(model: Markup, offset: number): Tag | undefined {
	if (model.opaque.some(span => span.start <= offset && offset < span.end)) return undefined;
	return model.tags.findLast(tag => tag.start < offset && (offset < tag.end || (!tag.complete && offset === tag.end)));
}

export function containingElements(model: Markup, offset: number): readonly Element[] {
	return model.elements.filter(element => element.tag.start < offset && !element.tag.selfClosing
		&& (element.closingStart ?? element.end) >= offset);
}
