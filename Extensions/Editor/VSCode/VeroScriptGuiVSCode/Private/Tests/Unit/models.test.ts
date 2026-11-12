// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import { test } from 'node:test';
import { decodeAttribute, editorSourceLimit, parseMarkup, tagAt } from '../../markup';
import { readSchema, SchemaIndex } from '../../schema';
import { parseStyle, styleContext } from '../../style';

test('keeps inline script, styles, comments and CDATA opaque while retaining real widget ranges', () => {
	const source = '<Stack><!-- <Fake/> --><script><![CDATA[void main(){print("</script><Fake/>");}]]></script>'
		+ '<style>:root { background-image: url("<not-a-widget>"); }</style><Button Text="a > b &amp; &#x1f600;" /></Stack>';
	const model = parseMarkup(source);
	assert.deepEqual(model.issues, []);
	assert.deepEqual(model.elements.map(element => element.tag.name), ['Stack', 'script', 'style', 'Button']);
	assert.equal(tagAt(model, source.indexOf('print(') + 2), undefined);
	assert.equal(model.elements[3]?.parent, 0);
	assert.equal(model.elements[3]?.tag.attributes[0]?.value, 'a > b & 😀');
	assert.equal(model.elements[0]?.end, source.length);
	assert.equal(decodeAttribute('&quot;&apos;&lt;&gt;&#0;&#x110000;'), '"\'<>\u0026#0;\u0026#x110000;');
});

test('recovers unfinished headers, reports structural mismatches and bounds oversized or deep documents', () => {
	const partial = '<Stack><Button bind:';
	assert.equal(tagAt(parseMarkup(partial), partial.length)?.name, 'Button');
	assert.ok(parseMarkup('<Stack></Button>').issues.some(issue => issue.message.startsWith('Closing tag')));
	assert.ok(parseMarkup('<Widget Width="1" Width="2"/>').issues.some(issue => issue.message.startsWith('Duplicate attribute')));
	assert.ok(parseMarkup('<Widget/><Widget/>').issues.some(issue => issue.message.startsWith('Only one root')));
	assert.ok(parseMarkup('<Widget>'.repeat(258)).issues.some(issue => issue.message.startsWith('Markup nesting')));
	const oversized = parseMarkup(' '.repeat(editorSourceLimit + 1));
	assert.equal(oversized.elements.length, 0);
	assert.equal(oversized.issues[0]?.message, 'Document exceeds the editor analysis limit.');
});

test('distinguishes a header being edited from the position after its closing delimiter', () => {
	for (const source of ['<Button>', '<Button/>', '<Stack></Stack>']) {
		assert.equal(tagAt(parseMarkup(source), source.length), undefined);
	}
	const source = '<Button ';
	const model = parseMarkup(source);
	assert.equal(model.tags[0]?.end, source.length);
	assert.equal(tagAt(model, source.length)?.name, 'Button');
});

test('ignores closing tags inside XML comments in raw script and style bodies', () => {
	const source = '<Stack><script><!-- </script><Fake/> -->int Value=0;</script>'
		+ '<style><!-- </style><Fake/> -->Text { color:#123456; }</style></Stack>';
	const model = parseMarkup(source);
	assert.deepEqual(model.issues, []);
	assert.deepEqual(model.elements.map(element => element.tag.name), ['Stack', 'script', 'style']);
});

test('CSS contexts ignore punctuation in comments, strings and functions', () => {
	const source = '/* } ; : */ Button:hover { background-image: url("a;b}c"); color:#123456; opacity: 0.5; }';
	const model = parseStyle(source);
	assert.equal(model.rules.length, 1);
	assert.equal(model.rules[0]?.selector, 'Button:hover');
	assert.equal(styleContext(model, source, source.indexOf('a;b') + 1), undefined);
	assert.deepEqual(styleContext(model, source, source.indexOf('#123456')), { kind: 'value', property: 'color' });
	assert.deepEqual(styleContext(model, source, source.indexOf('opacity') + 2), { kind: 'property' });
	assert.deepEqual(styleContext(model, source, source.indexOf('Button')), { kind: 'selector' });
	assert.equal(model.values.length, 3);
	assert.ok(model.values.some(span => source.slice(span.start, span.end) === '#123456'));
});

test('keeps unfinished CSS declarations in property or value context at EOF', () => {
	for (const property of ['fo', 'font-family']) {
		const source = 'Button { ' + property;
		assert.deepEqual(styleContext(parseStyle(source), source, source.length), { kind: 'property' });
	}
	const source = 'Button { width: 8';
	assert.deepEqual(styleContext(parseStyle(source), source, source.length), { kind: 'value', property: 'width' });
	const closed = source + ' }';
	assert.deepEqual(styleContext(parseStyle(closed), closed, closed.length), { kind: 'selector' });
});

test('extracts native URL references once with escapes, quoted delimiters and case preserved', () => {
	const source = String.raw`/* url(comment.png) */ Button { background-image: URL("images/a\"b.png"), url(images/a\)b.png); note: "url(fake.png)"; }`;
	const model = parseStyle(source);
	assert.deepEqual(model.references.map(reference => reference.value), ['images/a"b.png', 'images/a)b.png']);
	assert.equal(source.slice(model.references[0]!.start, model.references[0]!.end), String.raw`images/a\"b.png`);
	const unfinished = 'Button { background-image: ' + 'url('.repeat(8192);
	assert.deepEqual(parseStyle(unfinished).references, []);
});

test('native metadata retains inherited property flags and excludes ambiguous tag aliases', async () => {
	const builtin = readSchema(JSON.parse(await fs.readFile('Resources/Schemas/builtin.json', 'utf8')));
	const index = new SchemaIndex([builtin]);
	assert.equal(builtin.types.length, 42);
	assert.ok(builtin.types.some(type => type.name === 'VS::Gui::Internal::ContentHost'));
	assert.ok(builtin.types.some(type => type.name === 'VS::Gui::Internal::SelectionView'));
	assert.ok(!builtin.types.some(type => type.name.includes('Vsgui')));
	for (const name of ['ListView', 'TreeView', 'TableView']) {
		assert.deepEqual(builtin.types.find(type => type.name === `VS::Gui::${name}`)?.bases,
			['VS::Gui::Internal::SelectionView']);
	}
	assert.equal(index.widget('button')?.name, 'VS::Gui::Button');
	assert.ok(index.properties('ToggleSwitch').some(property => property.name === 'Checked' && property.bindable));
	assert.ok(index.properties('Button').some(property => property.name === 'Width' && property.style && property.bindable));
	const hit = index.properties('Button').find(property => property.name === 'HitTest');
	assert.ok(hit?.values?.includes('SelfOnly'));
	assert.ok(!hit?.values?.includes('children-only'));
	assert.ok(index.events('button').some(event => event.mark === 'clicked'));
	assert.ok(index.events('button').some(event => event.mark === 'pointer-down'));
	assert.ok(index.styleProperties().some(property => property.mark === 'columns'));
	assert.ok(!index.styleProperties().some(property => property.name === 'Text' || property.name === 'Value'));
	const custom = readSchema({ schemaVersion: 1, types: [{ name: 'Demo::Other', mark: 'button', bases: ['VS::Gui::Widget'] }] });
	const merged = new SchemaIndex([builtin, custom]);
	assert.equal(merged.widget('button'), undefined);
	assert.ok(!merged.widgets().some(widget => widget.tag === 'button'));
	assert.equal(merged.widget('Button')?.name, 'VS::Gui::Button');
	assert.throws(() => readSchema({ schemaVersion: 2, types: [] }));
	assert.throws(() => readSchema({ schemaVersion: 1, types: [{ name: 'A' }, { name: 'A' }] }));
});

test('composite controls retain unmarked ancestors, inherited events and native style/value hints', async () => {
	const builtin = readSchema(JSON.parse(await fs.readFile('Resources/Schemas/builtin.json', 'utf8')));
	const index = new SchemaIndex([builtin]);
	for (const tag of ['list-view', 'list-item', 'tree-view', 'tree-item', 'table', 'table-column', 'table-row',
		'combo-box', 'tabs', 'tab', 'menu', 'menu-item', 'dialog']) {
		assert.ok(index.widget(tag), tag);
		assert.ok(index.properties(tag).some(property => property.name === 'Width'), tag);
	}
	assert.ok(index.properties('ListView').some(property => property.mark === 'selected-key' && property.bindable));
	assert.ok(index.properties('list-item').some(property => property.mark === 'key' && property.bindable));
	assert.ok(index.properties('table-row').some(property => property.name === 'SelectionColor' && property.style));
	assert.ok(index.events('ListItem').some(event => event.mark === 'clicked'));
	assert.ok(index.events('table').some(event => event.name === 'Sorted' && event.type.includes('string, bool')));
	assert.ok(index.events('dialog').some(event => event.mark === 'closed' && event.type.includes('VS::Gui::Dialog, string')));
	assert.ok(index.events('dialog').some(event => event.mark === 'open-changed'));
	assert.ok(index.events('tree-item').some(event => event.mark === 'expanded-changed'));
	assert.deepEqual(index.properties('dialog').find(property => property.name === 'Placement')?.values,
		['below', 'right', 'center', 'Below', 'Right', 'Center']);
	assert.deepEqual(index.properties('menu-item').find(property => property.name === 'Checkable')?.values, ['true', 'false']);
	const styles = index.styleProperties().map(property => property.mark ?? property.name);
	for (const name of ['selection-color', 'column-width', 'drop-down-height']) {
		assert.ok(styles.includes(name), name);
	}
	assert.ok(!styles.includes('selected-key') && !styles.includes('modal'));
});

test('schema inheritance handles shared ancestors and cycles without revisiting branches', () => {
	const types = Array.from({ length: 50 }, (_, index) => ({ name: `Demo::Node${index}`,
		bases: index ? [`Demo::Node${index - 1}`, `Demo::Node${Math.max(0, index - 2)}`] : ['Demo::Node49'],
		properties: [{ name: `Value${index}`, type: 'int' }] }));
	const index = new SchemaIndex([readSchema({ schemaVersion: 1, types })]);
	assert.equal(index.properties('Node49').length, 50);
});
