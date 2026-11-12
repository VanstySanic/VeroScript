#!/usr/bin/env node
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// sharp is a development dependency used only to export these static resources.
const fs = require('node:fs');
const path = require('node:path');
const {parseArgs} = require('node:util');

const sourceRoot = path.resolve(__dirname, '../../..');
const assetRoot = path.join(sourceRoot, 'Resources/Assets');
const pngSizes = [16, 24, 32, 48, 64, 128, 256, 512, 1024];
const icoSizes = [16, 24, 32, 48, 64, 128, 256];
const icnsTypes = [[16, 'icp4'], [32, 'icp5'], [64, 'icp6'], [128, 'ic07'],
	[256, 'ic08'], [512, 'ic09'], [1024, 'ic10']];

const productMarkColor = '#60A5FA';
const fileMarkColor = '#5D75AE';
// The arms curve gently while both inner and outer joins remain pointed.
const markPath = [
	'M24 32H78',
	'C91 65 111 115 128 157',
	'C145 115 165 65 178 32H232',
	'C205 83 168 154 128 226',
	'C88 154 51 83 24 32Z'
].join('');

function positionMark(x, y, scale)
{
	return `translate(${x} ${y}) scale(${scale}) translate(-128 -129)`;
}

function mark(color, transform = '')
{
	return `
	<path d="${markPath}"${transform ? ` transform="${transform}"` : ''} fill="${color}"/>`;
}

const fileMark = mark(fileMarkColor, positionMark(128, 154, 0.55));

const icons = [
	{
		directory: 'icons',
		name: 'veroscript-icon',
		label: 'VeroScript application icon',
		body: mark(productMarkColor)
	},
	{
		directory: 'file-associations/icons',
		name: 'veroscript-source',
		label: 'VeroScript source file',
		body: `
	<path d="M56 20H164L212 68V224C212 231 207 236 200 236H56C49 236 44 231 44 224V32C44 25 49 20 56 20Z" fill="#F4F7FC" stroke="#8195B7" stroke-width="3" stroke-linejoin="round"/>
	<path d="M164 20V60C164 65 167 68 172 68H212Z" fill="#CBD7ED"/>
${fileMark}`
	},
	{
		directory: 'file-associations/icons',
		name: 'veroscript-app',
		label: 'VeroScript runnable application package',
		body: `
	<rect x="20" y="36" width="216" height="192" rx="12" fill="#F4F7FC" stroke="#8195B7" stroke-width="3"/>
	<path d="M32 36H224C231 36 236 41 236 48V76H20V48C20 41 25 36 32 36Z" fill="#CBD7ED"/>
	<path d="M20 76H236M36 56H64" fill="none" stroke="#8195B7" stroke-width="3" stroke-linecap="round"/>
${fileMark}`
	},
	{
		directory: 'file-associations/icons',
		name: 'veroscript-package',
		label: 'VeroScript installable package',
		body: `
	<path d="M52 28H204L232 76V220C232 227 227 232 220 232H36C29 232 24 227 24 220V76Z" fill="#F4F7FC" stroke="#8195B7" stroke-width="3" stroke-linejoin="round"/>
	<path d="M52 28H204L232 76H24Z" fill="#CBD7ED"/>
	<path d="M24 76H232M128 28V76" fill="none" stroke="#8195B7" stroke-width="3" stroke-linejoin="round"/>
${fileMark}`
	}
];

const vscodeIcons = 'Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Assets/Icons';
const jetbrainsIcons = 'Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Assets/Icons';
const guiJetbrainsIcons = 'Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Assets/Icons';
const lightThemeColor = '#1769AA';
const darkThemeColor = '#60A5FA';
// Keep each editor's native canvas, optical alignment and theme colors.
const editorIcons = [
	{
		file: `${vscodeIcons}/veroscript-light.svg`, size: 16,
		comment: "Match the default file-icon font's optical column within the padded label.",
		body: mark(lightThemeColor, positionMark(10, 8.5, 0.05625))
	},
	{
		file: `${vscodeIcons}/veroscript-dark.svg`, size: 16,
		comment: "Match the default file-icon font's optical column within the padded label.",
		body: mark(darkThemeColor, positionMark(10, 8.5, 0.05625))
	},
	{
		file: `${jetbrainsIcons}/veroscript.svg`, size: 16,
		body: mark(lightThemeColor, positionMark(8, 8.5, 0.05625))
	},
	{
		file: `${jetbrainsIcons}/veroscript_dark.svg`, size: 16,
		body: mark(darkThemeColor, positionMark(8, 8.5, 0.05625))
	},
	{
		file: `${jetbrainsIcons}/pluginIcon.svg`, size: 40,
		body: mark(lightThemeColor, positionMark(20, 21.25, 0.140625))
	},
	{
		file: `${jetbrainsIcons}/pluginIcon_dark.svg`, size: 40,
		body: mark(darkThemeColor, positionMark(20, 21.25, 0.140625))
	},
	{
		file: `${guiJetbrainsIcons}/gui.svg`, size: 16,
		body: `
	<path fill="none" stroke="#1769AA" stroke-width="1.25" d="M2 2.5h12v11H2zM2 5h12"/>${mark(lightThemeColor, positionMark(8, 10, 0.03125))}`
	},
	{
		file: `${guiJetbrainsIcons}/gui_dark.svg`, size: 16,
		body: `
	<path fill="none" stroke="${darkThemeColor}" stroke-width="1.25" d="M2 2.5h12v11H2zM2 5h12"/>${mark(darkThemeColor, positionMark(8, 10, 0.03125))}`
	},
	{
		file: `${guiJetbrainsIcons}/pluginIcon.svg`, size: 40,
		body: `
	<path fill="none" stroke="#1769AA" stroke-width="2.5" d="M5 5h30v30H5zM5 11h30"/>${mark(lightThemeColor, positionMark(20, 25, 0.09375))}`
	},
	{
		file: `${guiJetbrainsIcons}/pluginIcon_dark.svg`, size: 40,
		body: `
	<path fill="none" stroke="${darkThemeColor}" stroke-width="2.5" d="M5 5h30v30H5zM5 11h30"/>${mark(darkThemeColor, positionMark(20, 25, 0.09375))}`
	}
];

function encodeIco(frames)
{
	const header = Buffer.alloc(6 + 16 * icoSizes.length);
	header.writeUInt16LE(1, 2);
	header.writeUInt16LE(icoSizes.length, 4);
	let offset = header.length;
	for (const [index, size] of icoSizes.entries())
	{
		const entry = 6 + index * 16;
		const frame = frames.get(size);
		header[entry] = size === 256 ? 0 : size;
		header[entry + 1] = size === 256 ? 0 : size;
		header.writeUInt16LE(1, entry + 4);
		header.writeUInt16LE(32, entry + 6);
		header.writeUInt32LE(frame.length, entry + 8);
		header.writeUInt32LE(offset, entry + 12);
		offset += frame.length;
	}
	return Buffer.concat([header, ...icoSizes.map(size => frames.get(size))]);
}

function encodeIcns(frames)
{
	const chunks = icnsTypes.map(([size, type]) =>
	{
		const header = Buffer.alloc(8);
		header.write(type, 0, 4, 'ascii');
		header.writeUInt32BE(8 + frames.get(size).length, 4);
		return Buffer.concat([header, frames.get(size)]);
	});
	const header = Buffer.alloc(8);
	header.write('icns', 0, 4, 'ascii');
	header.writeUInt32BE(8 + chunks.reduce((total, chunk) => total + chunk.length, 0), 4);
	return Buffer.concat([header, ...chunks]);
}

async function generate()
{
	const {values} = parseArgs({
		options: {
			'sharp-module': {type: 'string'},
			'brand-only': {type: 'boolean', default: false}
		}
	});
	const sharp = require(values['sharp-module'] ? path.resolve(values['sharp-module']) : 'sharp');
	const selectedIcons = values['brand-only'] ? icons.slice(0, 1) : icons;
	for (const icon of selectedIcons)
	{
		const directory = path.join(assetRoot, icon.directory);
		fs.mkdirSync(directory, {recursive: true});
		const prefix = path.join(directory, icon.name);
		const svg = `<?xml version="1.0" encoding="UTF-8"?>
<!-- SPDX-FileCopyrightText: VanstySanic -->
<!-- SPDX-License-Identifier: MIT -->

<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256" role="img" aria-label="${icon.label}">
	<title>${icon.label}</title>
${icon.body}
</svg>
`;
		fs.writeFileSync(`${prefix}.svg`, svg);
		const frames = new Map();
		for (const size of pngSizes)
		{
			const png = await sharp(Buffer.from(svg), {density: 288})
				.resize(size, size, {kernel: 'lanczos3'})
				.png({compressionLevel: 9})
				.toBuffer();
			frames.set(size, png);
			fs.writeFileSync(`${prefix}-${size}.png`, png);
		}
		fs.writeFileSync(`${prefix}.ico`, encodeIco(frames));
		fs.writeFileSync(`${prefix}.icns`, encodeIcns(frames));
		console.log(`${icon.name}: SVG, ${pngSizes.length} transparent PNGs, ICO and ICNS`);
	}
	if (!values['brand-only'])
	{
		for (const icon of editorIcons)
		{
			const svg = `<!-- SPDX-FileCopyrightText: VanstySanic -->
<!-- SPDX-License-Identifier: MIT -->

<svg xmlns="http://www.w3.org/2000/svg" width="${icon.size}" height="${icon.size}" viewBox="0 0 ${icon.size} ${icon.size}">${icon.comment ? `\n\t<!-- ${icon.comment} -->` : ''}${icon.body}
</svg>
`;
			const destination = path.join(sourceRoot, icon.file);
			fs.mkdirSync(path.dirname(destination), {recursive: true});
			fs.writeFileSync(destination, svg);
		}
		console.log(`Editor icons: ${editorIcons.length} SVGs with their existing theme colors`);
	}
}

generate().catch(error =>
{
	console.error(error.message);
	process.exitCode = 1;
});
