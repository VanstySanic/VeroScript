// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { moduleRoot, repositoryRoot } from './paths.mjs';

function maskedSource(text) {
	return text.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|r"[^"]*"|[Ll]?"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,
		value => value.replace(/[^\r\n]/g, ' '));
}

function namespaceAt(code, position) {
	const scopes = [''];
	for (const token of code.matchAll(/\bnamespace\s+(\w+(?:::\w+)*)\s*\{|[{}]/g)) {
		if (token.index >= position) {
			break;
		}
		if (token[0] === '}') {
			scopes.pop();
		}
		else
		{
			scopes.push(token[1] ? [scopes.at(-1), token[1]].filter(Boolean).join('::') : scopes.at(-1));
		}
	}
	return scopes.at(-1);
}

export function guiSchema(sources) {
	const list = {
		Layout: ['Vertical', 'Horizontal', 'Overlay', 'vertical', 'horizontal', 'overlay'],
		Visibility: ['Visible', 'Hidden', 'Collapsed', 'visible', 'hidden', 'collapsed'],
		HitTest: ['SelfAndChildren', 'SelfOnly', 'ChildrenOnly', 'None', 'selfandchildren', 'selfonly', 'childrenonly', 'none'],
		Cursor: ['auto', 'default', 'text', 'wait', 'crosshair', 'progress', 'nwse-resize', 'nesw-resize', 'ew-resize', 'ns-resize', 'move', 'not-allowed', 'pointer', 'none'],
		FontStyle: ['normal', 'italic'], TextAlign: ['left', 'right', 'center', 'justify'],
		WhiteSpace: ['normal', 'pre', 'pre-wrap', 'pre-line', 'nowrap'], TextOverflow: ['clip', 'ellipsis'],
		Direction: ['auto', 'ltr', 'rtl'], Width: ['auto', '0', '100%', '8px', '8dp'], Height: ['auto', '0', '100%', '8px', '8dp'],
		Placement: ['below', 'right', 'center', 'Below', 'Right', 'Center'],
		Padding: ['0', '4px', '4px 8px']
	};
	const values = (name, type) => list[name] ?? (type === 'bool' ? ['true', 'false'] : undefined);
	const table = sources.types.slice(sources.types.indexOf('Definitions{'), sources.types.indexOf('}}};') + 4);
	const properties = [...table.matchAll(/\{\s*"(\w+)",\s*"([\w-]+)",[\s\S]*?,\s*(true|false)(?:,\s*"[^"]*")?\s*\}/g)].map(match => {
		const setter = new RegExp('void\\s+Set' + match[1] + '\\((?!std::attribute)([^)]+?)\\s+value\\)').exec(sources.widget);
		if (!setter) {
			throw new Error('No native Widget setter: ' + match[1]);
		}
		const type = setter[1].trim();
		return { name: match[1], mark: match[2], type, bindable: new RegExp('void\\s+Set' + match[1] + '\\(std::attribute<').test(sources.widget), style: match[3] === 'true', values: values(match[1], type) };
	});
	if (properties.length !== 36) {
		throw new Error('Review changed native base properties: ' + properties.length);
	}
	const types = [{ name: 'VS::Gui::Widget', bases: [], properties, events: [] }];
	const code = maskedSource(sources.controls);
	const classes = [...code.matchAll(/@\s*GuiWidget(?:\s*\(([^)]*)\))?\s+export\s+class\s+(\w+)\s*:\s*(?:public\s+)?(VS::Gui::\w+(?:::\w+)*)\s*\{/gd)];
	const mark = options => /Mark\s*=\s*"([^"]+)"/i.exec(options ?? '')?.[1];
	for (let index = 0; index < classes.length; ++index) {
		const item = classes[index];
		const start = item.index + item[0].length;
		let end = start, depth = 1;
		while (end < code.length && depth) {
			if (code[end] === '{') {
				++depth;
			}
			else if (code[end] === '}')
			{
				--depth;
			}
			++end;
		}
		if (depth) {
			throw new Error('Unclosed GUI widget: ' + item[2]);
		}
		const body = sources.controls.slice(start, end - 1);
		const properties = [...body.matchAll(/@\s*GuiProperty(?:\(([^)]*)\))?\s+([^=;]+?)\s+(\w+)\s*=/g)].map(match => {
			const storage = match[2].trim();
			const attribute = /^(?:std::)?attribute<([\s\S]+)>$/.exec(storage);
			const type = attribute?.[1] ?? storage;
			return { name: match[3], mark: mark(match[1]), type, bindable: !!attribute, style: /(?:^|,)\s*Style\s*(?:,|$)/i.test(match[1] ?? ''), values: values(match[3], type) };
		});
		const events = [...body.matchAll(/@\s*GuiEvent(?:\(([^)]*)\))?\s+(std::multicast_delegate<[\s\S]*?>)\s+(\w+)\s*=/g)].map(match =>
			({ name: match[3], mark: mark(match[1]), type: match[2].replace(/\s+/g, ' ') }));
		const options = item.indices[1] ? sources.controls.slice(...item.indices[1]) : '';
		types.push({ name: namespaceAt(code, item.index) + '::' + item[2], mark: mark(options), bases: [item[3]], properties, events });
	}
	if (classes.length < 20) {
		throw new Error('Review changed native controls: ' + classes.length);
	}
	const declared = new Set(types.map(type => type.name));
	if (declared.size !== types.length || types.some(type => type.bases.some(base => !declared.has(base)))) {
		throw new Error('Review duplicate or missing native widget declarations.');
	}
	const routedEvents = [...sources.events.matchAll(/\{Platform::EEventType::\w+,\s*"(\w+)",\s*"([\w-]+)"/g)].map(match =>
		({ name: match[1], mark: match[2], type: 'VS::Gui::Event' }));
	return { schemaVersion: 1, productVersion: '0.1.1', generatedFrom: sources.generatedFrom, types, routedEvents };
}

const files = {
	types: 'Modules/Multimedia/VeroScriptGui/Private/VeroScriptGuiTypes.cpp',
	events: 'Modules/Multimedia/VeroScriptGui/Private/VeroScriptGuiEvent.cpp',
	widget: 'Modules/Multimedia/VeroScriptGui/Resources/Scripts/Gui/Widget.vs'
};
const sources = Object.fromEntries(await Promise.all(Object.entries(files).map(async ([name, file]) =>
	[name, await fs.readFile(path.join(repositoryRoot, file), 'utf8')])));
const scriptRoot = 'Modules/Multimedia/VeroScriptGui/Resources/Scripts/';
const scripts = new Map();

// Follow the public library rather than a fixed control file so split declarations retain their bases.
async function readLibrary(file, required = false) {
	if (scripts.has(file)) return;
	let source;
	try {
		source = await fs.readFile(path.join(repositoryRoot, file), 'utf8');
	}
	catch (error)
	{
		// Standard-library exports are resolved by the native compiler, outside the GUI library.
		if (!required && error.code === 'ENOENT') return;
		throw error;
	}
	scripts.set(file, source);
	for (const imported of source.matchAll(/^\s*export\s+import\s+"([^"]+)"\s*;/gm)) {
		const target = path.posix.normalize(path.posix.join(path.posix.dirname(file), imported[1]));
		if (target.startsWith(scriptRoot)) {
			await readLibrary(target);
		}
	}
}

await readLibrary(scriptRoot + 'VeroScriptGui.vs', true);
sources.controls = [...scripts.values()].join('\n');
sources.generatedFrom = [...new Set([...Object.values(files), ...scripts.keys()])];
const serialized = JSON.stringify(guiSchema(sources), null, 2) + '\n';
const destination = path.join(moduleRoot, 'Resources/Schemas/builtin.json');
if (process.argv.includes('--check')) {
	const current = await fs.readFile(destination, 'utf8');
	if (current.replaceAll('\r\n', '\n') !== serialized) {
		throw new Error('GUI editor metadata is stale. Run npm run schema:update and review the changed native declarations.');
	}
}
else
{
	await fs.writeFile(destination, serialized);
}
