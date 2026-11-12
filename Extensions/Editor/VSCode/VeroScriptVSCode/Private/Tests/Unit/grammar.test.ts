// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { test } from 'node:test';
import { loadWASM, OnigScanner, OnigString } from 'vscode-oniguruma';
import { INITIAL, parseRawGrammar, Registry } from 'vscode-textmate';

test('TextMate grammar follows native keywords and contains strings and comments', async () => {
	const grammarFile = path.resolve('Resources/Syntaxes/veroscript.tmLanguage.json');
	const raw = await fs.readFile(grammarFile, 'utf8');
	const wasm = await fs.readFile(path.resolve('node_modules/vscode-oniguruma/release/onig.wasm'));
	await loadWASM(wasm);
	const registry = new Registry({ onigLib: Promise.resolve({ createOnigScanner: sources => new OnigScanner(sources), createOnigString: value => new OnigString(value) }),
		loadGrammar: async scope => scope === 'source.veroscript' ? parseRawGrammar(raw, grammarFile) : null });
	const grammar = await registry.loadGrammar('source.veroscript');
	assert.ok(grammar);
	const scopes = (line: string) => grammar.tokenizeLine(line, INITIAL).tokens.map(token => ({ value: line.slice(token.startIndex, token.endIndex), scopes: token.scopes.join(' ') }));
	assert.ok(scopes('constexpr int count = 4;').some(token => token.value === 'constexpr' && token.scopes.includes('keyword')));
	assert.ok(scopes('int64 count = 0xFFu64;').some(token => token.value === 'int64' && token.scopes.includes('storage.type')));
	assert.ok(scopes('int64 count = 0xFFu64;').some(token => token.value === '0xFFu64' && token.scopes.includes('constant.numeric')));
	assert.ok(scopes('print("if // else");').filter(token => token.value.includes('if')).every(token => token.scopes.includes('string.quoted')));
	const rawLine = grammar.tokenizeLine('auto text = r"path\\', INITIAL).ruleStack;
	const rawBody = 'if // still raw \\';
	const rawTokens = grammar.tokenizeLine(rawBody, rawLine);
	assert.ok(rawTokens.tokens.every(token => token.scopes.includes('string.quoted.double.raw.veroscript')));
	const rawClose = '"; int value = 0;';
	const rawClosed = grammar.tokenizeLine(rawClose, rawTokens.ruleStack);
	assert.ok(rawClosed.tokens.some(token => rawClose.slice(token.startIndex, token.endIndex) === 'int' && token.scopes.includes('storage.type.veroscript')));
	assert.ok(rawClosed.ruleStack.equals(INITIAL) || rawClosed.ruleStack.equals(grammar.tokenizeLine('', INITIAL).ruleStack));
	const rawPath = scopes('auto text = r"C:\\new\\"; int value = 0;');
	assert.ok(rawPath.every(token => !token.scopes.includes('constant.character.escape')));
	assert.ok(rawPath.some(token => token.value === 'int' && token.scopes.includes('storage.type')));
	assert.ok(scopes('l"wide";').some(token => token.value === 'l"' && token.scopes.includes('string.quoted')));
	assert.ok(scopes('// int64 return').every(token => token.scopes.includes('comment')));
	for (const slashCount of [1, 2]) {
		const continuedComment = grammar.tokenizeLine('// continued ' + '\\'.repeat(slashCount), INITIAL).ruleStack;
		const commentBody = grammar.tokenizeLine('int ignored; \\', continuedComment);
		assert.ok(commentBody.tokens.every(token => token.scopes.includes('comment.line.double-slash.veroscript')));
		const commentEnd = grammar.tokenizeLine('void ignored() {}', commentBody.ruleStack);
		assert.ok(commentEnd.tokens.every(token => token.scopes.includes('comment.line.double-slash.veroscript')));
		const afterComment = grammar.tokenizeLine('int value;', commentEnd.ruleStack);
		assert.ok(afterComment.tokens.some(token => token.scopes.includes('storage.type.veroscript')));
		assert.ok(afterComment.tokens.every(token => !token.scopes.includes('comment.line.double-slash.veroscript')));
	}
	const state = grammar.tokenizeLine('/* multiline', INITIAL).ruleStack;
	assert.ok(grammar.tokenizeLine('int return */ int', state).tokens[0]?.scopes.join(' ').includes('comment.block'));
	const signature = scopes('bool std::algorithm::contains(array<T> values, T value);');
	for (const name of ['std', 'algorithm']) {
		assert.ok(signature.some(token => token.value === name && token.scopes.includes('entity.name.scope-resolution')), name);
	}
	assert.ok(signature.some(token => token.value === 'contains' && token.scopes.includes('entity.name.function')));
	assert.equal(signature.filter(token => token.value === 'T' && token.scopes.includes('entity.name.type')).length, 2);
	for (const name of ['values', 'value']) {
		assert.ok(signature.some(token => token.value === name && token.scopes.includes('variable.other')), name);
	}
	const arrays = scopes('T[][] copy(T[] values);');
	assert.equal(arrays.filter(token => token.value === 'T' && token.scopes.includes('entity.name.type')).length, 2);
	assert.ok(arrays.some(token => token.value === 'copy' && token.scopes.includes('entity.name.function')));
	assert.ok(arrays.some(token => token.value === 'values' && token.scopes.includes('variable.other')));
	const nested = scopes('array<map<string, domain::Item>> values;');
	assert.ok(nested.some(token => token.value === 'Item' && token.scopes.includes('entity.name.type')));
	assert.ok(nested.some(token => token.value === 'values' && token.scopes.includes('variable.other') && !token.scopes.includes('meta.type.parameters')));
	for (const name of ['handle', 'shared', 'weak']) {
		const owner = scopes(`std::${name}<array<domain::Item>> owner;`);
		assert.ok(owner.some(token => token.value === name && token.scopes.includes('entity.name.type')), name);
		assert.ok(owner.some(token => token.value === 'Item' && token.scopes.includes('entity.name.type')), name);
		assert.ok(owner.some(token => token.value === 'owner' && token.scopes.includes('variable.other') && !token.scopes.includes('meta.type.parameters')), name);
	}
	const ownership = scopes('weak<shared<domain::Item>> observer;');
	for (const name of ['weak', 'shared', 'Item']) {
		assert.ok(ownership.some(token => token.value === name && token.scopes.includes('entity.name.type')), name);
	}
	for (const name of ['buffer', 'view', 'readonly_view', 'byte_order']) {
		const memory = scopes(`std::memory::${name} value;`);
		assert.ok(memory.some(token => token.value === name && token.scopes.includes('entity.name.type')), name);
		assert.ok(memory.some(token => token.value === 'value' && token.scopes.includes('variable.other')), name);
		assert.ok(scopes(`int ${name} = 0;`).every(token => !token.scopes.includes('entity.name.type')), `${name} remains contextual`);
	}
	for (const name of ['std::bigint', 'std::rational', 'std::numeric::rounding_mode']) {
		const leaf = name.split('::').at(-1)!;
		const numeric = scopes(`${name} value;`);
		assert.ok(numeric.some(token => token.value === leaf && token.scopes.includes('entity.name.type')), name);
		assert.ok(numeric.some(token => token.value === 'value' && token.scopes.includes('variable.other')), name);
		assert.ok(scopes(`int ${leaf} = 0;`).every(token => !token.scopes.includes('entity.name.type')), `${leaf} is not a reserved type keyword`);
	}
	const numericArguments = scopes('map<std::bigint, std::rational> values;');
	for (const name of ['bigint', 'rational']) {
		assert.ok(numericArguments.some(token => token.value === name && token.scopes.includes('entity.name.type')), name);
	}
	const softNames = scopes('int handle = 1; int shared = 2; int weak = 3; int Handle = 4;');
	assert.ok(softNames.every(token => !token.scopes.includes('entity.name.type') && !token.scopes.includes('meta.type.parameters')));
	const comparison = scopes('if (left < right && right > limit) { return result; }');
	assert.ok(comparison.every(token => !token.scopes.includes('entity.name.type') && !token.scopes.includes('meta.type.parameters')));
	const commented = scopes('array</* > */ T> values;');
	assert.ok(commented.some(token => token.value.includes('>') && token.scopes.includes('comment.block')));
	assert.ok(commented.some(token => token.value === 'values' && token.scopes.includes('variable.other') && !token.scopes.includes('meta.type.parameters')));
	const multiline = grammar.tokenizeLine('array< // > remains a comment', INITIAL).ruleStack;
	const closing = 'map<string, T>> values;';
	const closed = grammar.tokenizeLine(closing, multiline);
	assert.ok(closed.tokens.some(token => closing.slice(token.startIndex, token.endIndex) === 'values' && token.scopes.includes('variable.other.veroscript') && !token.scopes.includes('meta.type.parameters.veroscript')));
	assert.ok(closed.ruleStack.equals(grammar.tokenizeLine('', INITIAL).ruleStack));
	const language = JSON.parse(await fs.readFile('Resources/Language/language-configuration.json', 'utf8')) as { autoClosingPairs: { open: string }[] };
	assert.ok(!language.autoClosingPairs.some(pair => pair.open === '<'));
	const native = await fs.readFile('../../../../VeroScript/Private/VeroScriptLexer.cpp', 'utf8');
	const words = [...native.split('Keywords[] = {')[1]!.split('};')[0]!.matchAll(/"(\w+)"/g)].map(match => match[1]!);
	for (const word of words) {
		assert.ok(scopes(word).some(token => /storage.type|keyword.control|constant.language/.test(token.scopes)), word);
	}
	registry.dispose();
});
