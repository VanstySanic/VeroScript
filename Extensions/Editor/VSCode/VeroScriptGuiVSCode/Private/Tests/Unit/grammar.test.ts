// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { test } from 'node:test';
import { INITIAL, parseRawGrammar, Registry } from 'vscode-textmate';
import { loadWASM, OnigScanner, OnigString } from 'vscode-oniguruma';

test('markup grammar respects self-closing directives and embedded CDATA languages', async () => {
	await loadWASM(await fs.readFile('node_modules/vscode-oniguruma/release/onig.wasm'));
	const files: Record<string, string> = {
		'text.xml.vswml': path.resolve('Resources/Syntaxes/markup.tmLanguage.json'),
		'source.vswcss': path.resolve('Resources/Syntaxes/style.tmLanguage.json'),
		'source.veroscript': path.resolve('../VeroScriptVSCode/Resources/Syntaxes/veroscript.tmLanguage.json')
	};
	const registry = new Registry({ onigLib: Promise.resolve({ createOnigScanner: patterns => new OnigScanner(patterns), createOnigString: text => new OnigString(text) }),
		loadGrammar: async scope => files[scope] ? parseRawGrammar(await fs.readFile(files[scope]!, 'utf8'), files[scope]!) : null });
	try {
		const markup = await registry.loadGrammar('text.xml.vswml');
		assert.ok(markup);
		const styleImport = markup.tokenizeLine('<style src="page.vswcss" />', INITIAL);
		const button = markup.tokenizeLine('<Button Text="Hello" />', styleImport.ruleStack);
		assert.ok(button.tokens.some(token => token.scopes.includes('entity.name.tag.xml')));
		assert.ok(button.tokens.every(token => !token.scopes.includes('meta.embedded.block.vswcss')));
		const script = markup.tokenizeLine('<script><![CDATA[', INITIAL);
		const code = markup.tokenizeLine('constexpr int count = 4;', script.ruleStack);
		assert.ok(code.tokens.some(token => token.scopes.some(scope => scope.startsWith('keyword.control.veroscript'))));
		const end = markup.tokenizeLine(']]></script>', code.ruleStack);
		const after = markup.tokenizeLine('<Text />', end.ruleStack);
		assert.ok(after.tokens.some(token => token.scopes.includes('entity.name.tag.xml')));
		assert.ok(after.tokens.every(token => !token.scopes.includes('meta.embedded.block.veroscript')));
		const comment = markup.tokenizeLine('<style><!-- </style><Fake/> -->', INITIAL);
		assert.ok(comment.tokens.some(token => token.scopes.includes('comment.block.xml')));
		const inlineStyle = markup.tokenizeLine('Text { color: #ff0000; }', comment.ruleStack);
		assert.ok(inlineStyle.tokens.some(token => token.scopes.includes('meta.embedded.block.vswcss')));
		const styleEnd = markup.tokenizeLine('</style>', inlineStyle.ruleStack);
		assert.ok(markup.tokenizeLine('<Button/>', styleEnd.ruleStack).tokens.every(token => !token.scopes.includes('meta.embedded.block.vswcss')));
		const style = await registry.loadGrammar('source.vswcss');
		assert.ok(style);
		const css = style.tokenizeLine('Button:hover { color: #ff0000; }', INITIAL);
		assert.ok(css.tokens.some(token => token.scopes.includes('support.type.property-name.css')));
		assert.ok(css.tokens.some(token => token.scopes.includes('constant.other.color.rgb-value.css')));
	}
	finally
	{
		registry.dispose();
	}
});
