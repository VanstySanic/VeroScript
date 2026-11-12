// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { randomUUID } from 'node:crypto';
import { downloadAndUnzipVSCode } from '@vscode/test-electron';
import { moduleRoot, temporaryRoot, runNode } from './paths.mjs';

if ((process.env.VEROSCRIPT_GUI_TEST_NATIVE === '1' || process.env.VEROSCRIPT_GUI_TEST_PREVIEW === '1')
	&& !process.env.VEROSCRIPT_TEST_RUNTIME) {
	throw new Error('Native GUI acceptance requires an explicit matching VEROSCRIPT_TEST_RUNTIME.');
}
if (process.env.VEROSCRIPT_GUI_TEST_PREVIEW === '1' && process.env.VEROSCRIPT_GUI_TEST_NATIVE !== '1') {
	throw new Error('Preview acceptance also requires VEROSCRIPT_GUI_TEST_NATIVE=1.');
}
if (process.env.VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH && !path.isAbsolute(process.env.VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH)) {
	throw new Error('VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH must be an absolute prepared package environment path.');
}
const testDirectory = process.env.VEROSCRIPT_TEST_DIRECTORY || path.join(temporaryRoot, 'tests');
if (!path.isAbsolute(testDirectory)) {
	throw new Error('VEROSCRIPT_TEST_DIRECTORY must be absolute.');
}
if (!process.argv.includes('--skip-build')) {
	await runNode(path.resolve(moduleRoot, '../VeroScriptVSCode/Resources/Tools/build.mjs'), ['--release']);
	await runNode(path.join(moduleRoot, 'Resources/Tools/build.mjs'), ['--tests']);
}
const executable = process.env.VEROSCRIPT_TEST_VSCODE_EXECUTABLE || await downloadAndUnzipVSCode({
	version: process.env.VEROSCRIPT_TEST_VSCODE_VERSION || '1.106.0', cachePath: path.join(temporaryRoot, 'downloads')
});
const directory = path.join(testDirectory, randomUUID());
const workspace = path.join(directory, 'workspace 中文');
const user = path.join(directory, 'user');
const extensions = path.join(directory, 'extensions');
await Promise.all([workspace, path.join(user, 'User'), extensions].map(file => fs.mkdir(file, { recursive: true })));
await fs.mkdir(path.join(workspace, '.vscode'));
await fs.writeFile(path.join(workspace, '.vscode/settings.json'), JSON.stringify({ 'veroscript.run.backend': 'vm',
	...(process.env.VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH ? { 'veroscriptGui.environmentPath': process.env.VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH } : {}) }));
await fs.writeFile(path.join(user, 'User/settings.json'), JSON.stringify({ 'workbench.startupEditor': 'none',
	'files.hotExit': 'off', 'workbench.localHistory.enabled': false, 'editor.wordBasedSuggestions': 'off', 'veroscript.lsp.enabled': false,
	...(process.env.VEROSCRIPT_TEST_RUNTIME ? { 'veroscript.runtimePath': process.env.VEROSCRIPT_TEST_RUNTIME } : {}) }));
const packaged = process.argv.includes('--packaged');
let harness;
let baseVsix;
let guiVsix;
if (packaged) {
	const base = JSON.parse(await fs.readFile(path.resolve(moduleRoot, '../VeroScriptVSCode/package.json'), 'utf8'));
	const gui = JSON.parse(await fs.readFile(path.join(moduleRoot, 'package.json'), 'utf8'));
	baseVsix = path.resolve(moduleRoot, `../VeroScriptVSCode/out/${base.name}-${base.version}.vsix`);
	guiVsix = path.join(moduleRoot, 'out', `${gui.name}-${gui.version}.vsix`);
	await Promise.all([baseVsix, guiVsix].map(file => fs.access(file)));
	harness = path.join(directory, 'harness');
	await fs.mkdir(harness);
	await fs.writeFile(path.join(harness, 'package.json'), JSON.stringify({ name: 'veroscript-gui-package-tests', publisher: 'local-tests', version: '0.0.0',
		engines: { vscode: '^1.106.0' }, main: 'extension.cjs', activationEvents: [] }));
	await fs.writeFile(path.join(harness, 'extension.cjs'), 'exports.activate = () => {};\n');
}
const cli = JSON.parse(await fs.readFile(path.join(moduleRoot, 'node_modules/@vscode/test-cli/package.json'), 'utf8'));
console.log(`GUI test workspace: ${workspace}\nVS Code: ${executable}\nPackaged: ${packaged}`);
await runNode(path.join(moduleRoot, 'node_modules/@vscode/test-cli', typeof cli.bin === 'string' ? cli.bin : Object.values(cli.bin)[0]), [], {
	env: { ...process.env, VEROSCRIPT_TEST_VSCODE_EXECUTABLE: executable,
		VEROSCRIPT_GUI_TEST_WORKSPACE: workspace, VEROSCRIPT_GUI_TEST_USER: user, VEROSCRIPT_GUI_TEST_EXTENSIONS: extensions,
		VEROSCRIPT_GUI_TEST_HARNESS: harness, VEROSCRIPT_GUI_TEST_BASE_VSIX: baseVsix, VEROSCRIPT_GUI_TEST_VSIX: guiVsix }
});
