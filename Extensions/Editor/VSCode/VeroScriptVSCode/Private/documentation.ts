// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

function backtickFence(value: string, minimum: number): string {
	let length = minimum;
	for (const match of value.matchAll(/`+/g)) {
		length = Math.max(length, match[0].length + 1);
	}
	return '`'.repeat(length);
}

function inlineCode(value: string): string {
	const fence = backtickFence(value, 1);
	return `${fence} ${value} ${fence}`;
}

export function signatureMarkdown(value: string): string {
	const fence = backtickFence(value, 3);
	return `${fence}veroscript\n${value}\n${fence}`;
}

export function isBuiltinTypeDetail(value: string): boolean {
	return builtinTypeDetails.has(value);
}

export function builtinNoteMarkdown(value: string, label: string): string | undefined {
	const match = /^(?:\(soft builtin\)|soft builtin(?=$|[ \t]))(.*)$/.exec(value);
	if (!match) return undefined;
	const detail = match[1]!.trim();
	return `*${label}*${detail ? ' ' + inlineCode(detail) : ''}`;
}

function typeSummary(kind: string, name: string, detail: string | undefined): string | undefined {
	if (!detail || !builtinTypeDetails.has(detail) || !typeKinds.has(kind)) return undefined;
	return `**${detail}** ${inlineCode(name)}`;
}

const builtinTypeDetails = new Set(['builtin type', '内置类型', '內建型別', 'type intégré', '組み込み型', '기본 제공 형식']);
const typeKinds = new Set(['keyword', 'type alias', 'class', 'struct', 'interface', 'enum', 'enum class']);

// Present native plain/Markdown headers and metadata consistently. Preserve
// declarations, documentation and fenced examples while avoiding whole-header
// code spans and redundant built-in type categories.
export function hoverMarkdown(value: string, builtinLabel = 'Built-in symbol.'): string | undefined {
	const paragraphs = value.replaceAll('\r\n', '\n').split(/(\n[ \t]*\n+)/);
	const first = paragraphs[0];
	if (!first) return undefined;
	const codeHeader = /^\*\*(`+) ([^\n]+) \1\*\*$/;
	const plainHeader = /^(enum member|enum class|type alias|annotation argument|import|namespace|function|method|constructor|destructor|struct|class|interface|enum|field|variable|constant|label|keyword|annotation|macro|symbol) ([^\n]+)(?:\n|$)/;
	const formattedTitle = codeHeader.exec(first);
	const header = plainHeader.exec(formattedTitle?.[2] ?? first);
	if (!header) return undefined;
	if (!formattedTitle && first.split('\n').slice(1).some(line => /^ {0,3}(?:`{3,}|~{3,})/.test(line))) return undefined;
	const title = `${header[1]} ${header[2]}`;
	let fence: { marker: string; length: number } | undefined;
	let wroteSummary = false;
	const consumed = new Set<number>();
	return paragraphs.map((paragraph, index) => {
		if (consumed.has(index)) return '';
		if (index % 2 !== 0) return paragraph;
		if (formattedTitle && !fence && (index === 0 || paragraphs[index - 2] === '---')) {
			const formatted = codeHeader.exec(paragraph);
			const summary = formatted && plainHeader.exec(formatted[2]!);
			if (summary) {
				// Built-in primitive and container types share a localized description,
				// regardless of their semantic keyword/type-alias/class classification.
				const builtin = typeSummary(summary[1]!, summary[2]!, paragraphs[index + 2]);
				if (builtin) {
					consumed.add(index + 1);
					consumed.add(index + 2);
					return builtin;
				}
				return `**${summary[1]}** ${inlineCode(summary[2]!)}`;
			}
		}
		if (!formattedTitle && !fence && (paragraph === title || paragraph.startsWith(title + '\n'))) {
			const lines = paragraph.split('\n').slice(1);
			const note = lines.at(-1) === '(soft builtin)' ? lines.pop() : undefined;
			const detail = lines.join('\n');
			const builtin = typeSummary(header[1]!, header[2]!, detail);
			const summary = [builtin ?? `**${header[1]}** ${inlineCode(header[2]!)}`,
				...(!builtin && detail ? [builtinNoteMarkdown(detail, builtinLabel) ?? signatureMarkdown(detail)] : []),
				...(note ? [builtinNoteMarkdown(note, builtinLabel)!] : [])].join('\n\n');
			const separator = wroteSummary ? '---\n\n' : '';
			wroteSummary = true;
			return separator + summary;
		}
		if (!formattedTitle && !fence) {
			const location = /^(exported \| )?defined at ([^\n]+:\d+)$/.exec(paragraph);
			if (location) return `${location[1] ?? ''}defined at ${inlineCode(location[2]!)}`;
		}
		for (const line of paragraph.split('\n')) {
			const marker = /^ {0,3}(`{3,}|~{3,})(.*)$/.exec(line);
			if (!marker) {
				continue;
			}
			if (!fence) {
				fence = { marker: marker[1]![0]!, length: marker[1]!.length };
			}
			else if (marker[1]![0] === fence.marker && marker[1]!.length >= fence.length && !marker[2]!.trim())
			{
				fence = undefined;
			}
		}
		return paragraph;
	}).join('');
}
