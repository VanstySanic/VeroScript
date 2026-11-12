// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { builtinNoteMarkdown, hoverMarkdown, signatureMarkdown } from '../../documentation';

test('combines localized built-in type categories without hiding descriptions or keyword help', () => {
	for (const [detail, description] of [['builtin type', 'Built-in VeroScript type.'], ['内置类型', 'VeroScript 内置类型。'],
		['內建型別', 'VeroScript 內建型別。'], ['type intégré', 'Type VeroScript intégré.'],
		['組み込み型', 'VeroScript の組み込み型。'], ['기본 제공 형식', 'VeroScript 기본 제공 형식.']]) {
		const expected = `**${detail}** \` int \`\n\n${description}`;
		for (const kind of ['keyword', 'type alias', 'class', 'struct', 'interface', 'enum', 'enum class']) {
			assert.equal(hoverMarkdown(`**\` ${kind} int \`**\n\n${detail}\n\n${description}`), expected);
			assert.equal(hoverMarkdown(`${kind} int\n${detail}\n\n${description}`), expected);
		}
		assert.equal(hoverMarkdown(expected), undefined);
	}
	assert.equal(hoverMarkdown('**` keyword return `**\n\nReturn from a function.'), '**keyword** ` return `\n\nReturn from a function.');
	assert.equal(hoverMarkdown('**` keyword int `**\n\n```veroscript\nint\n```'), '**keyword** ` int `\n\n```veroscript\nint\n```');
});

test('normalizes later built-in type summaries while preserving aliases and fenced examples', () => {
	const first = '**` type alias array `**\n\nbuiltin type\n\nBuilt-in VeroScript type.';
	const second = '**` keyword int `**\n\nbuiltin type\n\nBuilt-in VeroScript type.';
	const example = '\n\n```text\n---\n\n**` type alias example `**\n\nbuiltin type\n```';
	const result = hoverMarkdown(first + '\n\n---\n\n' + second + example);
	assert.equal(result, '**builtin type** ` array `\n\nBuilt-in VeroScript type.\n\n---\n\n**builtin type** ` int `\n\nBuilt-in VeroScript type.' + example);
	assert.equal(hoverMarkdown(result!), undefined);
	const alias = '**` type alias Count `**\n\n```veroscript\nusing Count = int64;\n```\n\nUser alias documentation.\n\n` defined at count.vs:1 `';
	assert.equal(hoverMarkdown(alias), alias.replace('**` type alias Count `**', '**type alias** ` Count `'));
	assert.equal(hoverMarkdown('**` function user `**\n\nbuiltin type'), '**function** ` user `\n\nbuiltin type', 'a documentation paragraph cannot change a function category');
});

test('renders builtin metadata as notes without interpreting generic notation or examples', () => {
	assert.equal(builtinNoteMarkdown('soft builtin', '内置符号。'), '*内置符号。*');
	assert.equal(builtinNoteMarkdown('soft builtin (delegate.vs; delegate<R>)', 'Built-in symbol.'), '*Built-in symbol.* ` (delegate.vs; delegate<R>) `');
	assert.equal(builtinNoteMarkdown('soft builtin value `T`', 'Built-in symbol.'), '*Built-in symbol.* `` value `T` ``');
	assert.equal(builtinNoteMarkdown('soft builtins are documented here.', 'Built-in symbol.'), undefined);
	assert.equal(builtinNoteMarkdown('void print(any value);', 'Built-in symbol.'), undefined);
	assert.equal(hoverMarkdown('struct optional<T>\nsoft builtin value type', '内置符号。'), '**struct** ` optional<T> `\n\n*内置符号。* ` value type `');
});

test('renders native hover signatures without interpreting arrays and generic types as Markdown', () => {
	const result = hoverMarkdown('function collect\r\narray<string>(map<string, int64> values)\r\n\r\nDescription.\r\n\r\ndefined at generic_[x].vs:12');
	assert.ok(result?.includes('**function** ` collect `'));
	assert.ok(result?.includes('```veroscript\narray<string>(map<string, int64> values)\n```'));
	assert.ok(result?.includes('\n\nDescription.\n\n'));
	assert.ok(result?.endsWith('defined at ` generic_[x].vs:12 `'));
});

test('separates overloads and retains multiline builtins, notes and localized documentation', () => {
	const result = hoverMarkdown('function add\nint64(int64 left, int64 right)\n\n参数 `left`： first value\n\n返回： sum\n\nfunction add\ndouble(double left, double right)\n(soft builtin)');
	assert.ok(result?.includes('\n\n---\n\n**function**'));
	assert.ok(result?.includes('参数 `left`： first value\n\n返回： sum'));
	assert.ok(result?.endsWith('double(double left, double right)\n```\n\n*Built-in symbol.*'));
	const builtin = hoverMarkdown('function print\nvoid print(any value);\nvoid print(string fmt, ...);\n(soft builtin)');
	assert.ok(builtin?.includes('void print(any value);\nvoid print(string fmt, ...);\n```'));
});

test('aligns current native Markdown headers while preserving signatures, notes and examples', () => {
	const body = '\n\n```veroscript\nvoid std::print(any value);\nvoid std::print(string fmt, ...);\n```\n\n*Built-in symbol.*\n\n```text\n---\n\n**` function example `**\n```\n\n**` function quoted `**';
	const result = hoverMarkdown('**` function std::print `**' + body);
	assert.equal(result, '**function** ` std::print `' + body);
	assert.equal(hoverMarkdown(result!), undefined, 'presentation is idempotent');
	const overload = '\n\n---\n\n**` method Owner.print `**\n\n```veroscript\nvoid Owner.print(string value);\n```';
	assert.equal(hoverMarkdown('**` function print `**' + overload), '**function** ` print `' + overload.replace('**` method Owner.print `**', '**method** ` Owner.print `'));
	assert.equal(hoverMarkdown('**`` macro VALUE` ``**\n\nDescription.'), '**macro** `` VALUE` ``\n\nDescription.');
	assert.equal(hoverMarkdown('**` unknown title `**\n\nDescription.'), undefined);
});

test('preserves documentation examples, whitespace, links and unrecognized formats', () => {
	const documentation = '[Guide](https://example.com)\n\n~~~text\nfunction add\nnot a signature\n\n\n~~~\n\n```text\nfunction add\nstill an example\n```\n\n@custom Keep this.';
	const result = hoverMarkdown('function add\nint64(int64 left)\n\n' + documentation);
	assert.ok(result?.endsWith(documentation));
	assert.equal(hoverMarkdown('```veroscript\nint64 add(int64 left)\n```\n\nAlready formatted.'), undefined);
	assert.equal(hoverMarkdown('function add\n```veroscript\nint64 add(int64 left)\n```'), undefined);
	assert.equal(hoverMarkdown('A future server format.'), undefined);
	assert.equal(hoverMarkdown(''), undefined);
	assert.equal(hoverMarkdown(result!), undefined, 'presentation is idempotent');
});

test('code fences preserve literal backticks in signatures and symbol names', () => {
	assert.equal(signatureMarkdown('string value = "```";'), '````veroscript\nstring value = "```";\n````');
	const result = hoverMarkdown('macro VALUE`\n#define VALUE` "```"');
	assert.ok(result?.startsWith('**macro** `` VALUE` ``'));
	assert.ok(result?.includes('````veroscript\n#define VALUE` "```"\n````'));
});
