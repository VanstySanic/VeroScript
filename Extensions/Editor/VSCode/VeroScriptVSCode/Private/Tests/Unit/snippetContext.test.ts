// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import { test } from 'node:test';
import { snippetScope } from '../../snippetContext';

test('offers templates only at known declaration and statement boundaries', () => {
	for (const source of ['', '// header\n', 'import "module";\n', 'void main() {}\n']) {
		assert.equal(snippetScope(source), 'file', source);
	}
	assert.equal(snippetScope('namespace demo {\n'), 'namespace');
	assert.equal(snippetScope('class Demo {\n'), 'type');
	assert.equal(snippetScope('enum class Demo {\n'), undefined);
	assert.equal(snippetScope('interface Demo {\n'), undefined);
	assert.equal(snippetScope('class Demo { void run() {\n'), 'statement');
	assert.equal(snippetScope('void main() {\n'), 'statement');
	assert.equal(snippetScope('void main() { if (ready) {\n'), 'statement');
	assert.equal(snippetScope('void main() { call(fn() -> void {\n'), 'statement');
	assert.equal(snippetScope('void main() { {\n'), 'statement');
});

test('suppresses templates in incomplete expressions, names, initializers and mismatched delimiters', () => {
	for (const source of ['void main() { for (int i = 0; ', 'void main() { for (\n', 'void main() { call(\n',
		'void main() { values[\n', 'void main() { int ', 'class ', 'namespace ', 'std::', 'object.',
		'void main() { return ', 'void main() { auto values = array<int>{\n', 'void main() { print("ready") ', ')\n']) {
		assert.equal(snippetScope(source), undefined, source);
	}
});

test('ignores quoted delimiters, comments and preprocessor directives and supports cancellation', () => {
	for (const source of ['// ', '/* ', '/*/', '"', 'L"', '"escaped\\"', "'", '#define VALUE ', 'void main() { // ']) {
		assert.equal(snippetScope(source), undefined, source);
	}
	assert.equal(snippetScope('void main() { print("} /*");\n'), 'statement');
	assert.equal(snippetScope('void main() { /* } */\n'), 'statement');
	assert.equal(snippetScope('/* multiline\ncomment */\n'), 'file');
	assert.equal(snippetScope('void main() { /* multiline\n} ignored\n*/\n'), 'statement');
	assert.equal(snippetScope('void main() { /* multiline\n'), undefined);
	assert.equal(snippetScope('#define VALUE {\n'), 'file');
	assert.equal(snippetScope('void main() {\n', () => true), undefined);
});

test('suppresses templates throughout continued directives and line comments, then restores the enclosing scope', () => {
	for (const eol of ['\n', '\r\n']) {
		for (const prefix of ['', 'void main() {\n']) {
			for (const header of ['#define BODY', '// continued comment']) {
				const continued = prefix + header + ' \\' + eol;
				for (const continuation of ['', '  ', 'void sample() { ', 'print(1); ', 'print(1); \\' + eol + '  ']) {
					assert.equal(snippetScope(continued + continuation), undefined, JSON.stringify(continued + continuation));
				}
				assert.equal(snippetScope(continued + 'print(1);' + eol), prefix ? 'statement' : 'file');
			}
		}
	}
});

test('keeps continued macro hashes inside the directive and resumes across independent directives', () => {
	for (const eol of ['\n', '\r\n']) {
		const macro = '#define LABELS(x) \\' + eol + ('#x + \\' + eol).repeat(8000) + '#x' + eol;
		assert.equal(snippetScope(macro), 'file');
		assert.equal(snippetScope(macro + 'void main() {' + eol), 'statement');
		assert.equal(snippetScope(macro + '#define NEXT \\' + eol + '#x; '), undefined);
		assert.equal(snippetScope(macro + '#define NEXT \\' + eol + '#x; ' + eol), 'file');
		assert.equal(snippetScope(macro, () => true), undefined);
	}
});

test('keeps block comments opened in directives opaque without swallowing following code', () => {
	for (const eol of ['\n', '\r\n']) {
		for (const header of ['#define BODY /*', '#define BODY \\' + eol + '/*']) {
			assert.equal(snippetScope(header + eol), undefined);
			assert.equal(snippetScope(header + eol + 'void sample() { '), undefined);
			assert.equal(snippetScope(header + eol + '*/' + eol), 'file');
			assert.equal(snippetScope(header + eol + '*/void main() {' + eol), 'statement');
		}
	}
	assert.equal(snippetScope('#define TEXT "/*"\n'), 'file');
	assert.equal(snippetScope('#define BODY // /*\n'), 'file');
	assert.equal(snippetScope('#define BODY /* inline */ void sample() {\n'), 'file');
});

test('distinguishes braced expressions from completed declarations and statement blocks', () => {
	for (const source of ['array<int> values = array<int>{1, 2}\n',
		'void main() { auto callback = fn() -> void {}\n', 'void main() { return fn() -> void {}\n',
		'void main() { auto callback = fn() {}\n', 'void main() { auto callback = []() {}\n']) {
		assert.equal(snippetScope(source), undefined, source);
	}
	assert.equal(snippetScope('class Demo {};\n'), 'file');
	assert.equal(snippetScope('class Demo {}\n'), 'file');
	assert.equal(snippetScope('enum Demo { Value }\n'), 'file');
	assert.equal(snippetScope('array<int> values = array<int>{1, 2};\n'), 'file');
	assert.equal(snippetScope('void main() { auto callback = fn() -> void {};\n'), 'statement');
	assert.equal(snippetScope('void main() { auto callback = fn() {};\n'), 'statement');
	assert.equal(snippetScope('void main() { auto callback = []() {\n'), 'statement');
	assert.equal(snippetScope('fn() -> void create() {}\n'), 'file');
	assert.equal(snippetScope('namespace demo {}\n'), 'file');
	assert.equal(snippetScope('void main() { if (ready) {}\n'), 'statement');
});

test('keeps multiline raw strings opaque and closes them at the first quote after a backslash', () => {
	for (const before of ['r"', 'void main() { auto text = r"first\n// { ;\n', 'void main() { auto text = r"path\\']) {
		assert.equal(snippetScope(before), undefined, before);
	}
	assert.equal(snippetScope('void main() { auto text = r"path\\";\n'), 'statement');
	assert.equal(snippetScope('void main() { auto text = r"first\n// { ;\nend";\n'), 'statement');
	assert.equal(snippetScope('void main() { auto text = l"wide";\n'), 'statement');
});
