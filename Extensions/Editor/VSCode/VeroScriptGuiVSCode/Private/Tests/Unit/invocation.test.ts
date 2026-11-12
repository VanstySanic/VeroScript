// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as path from 'node:path';
import { test } from 'node:test';
import type { VeroScriptToolchain } from '../../../../VeroScriptVSCode/Public/api';
import { backendValues, GuiInvocation, invocationArguments } from '../../invocation';

const root = path.resolve('scratch 中文');
const toolchain: VeroScriptToolchain = {
	executable: path.join(root, 'veroscript'), runtimeRoot: root, base: root, cwd: root, environment: {},
	compileArguments: ['--stdlib-path', path.join(root, 'stdlib'), '-I', path.join(root, 'imports')],
	executionArguments: ['--backend=jit', '--jit-backend', path.join(root, 'backend.so')], scriptArguments: [], sourceDirectories: [root], sourceFiles: []
};
const request: GuiInvocation = { action: 'preview', input: path.join(root, 'page.vswml'), name: 'app', frames: 3,
	assets: [], args: ['hello world', '中文', '$(literal)', '--flag'], backends: { gui: 'rmlui', platform: 'sdl3' }, environmentPath: path.join(root, 'project') };

test('orders compiler/backend/environment/task options and preserves exact script arguments', () => {
	const args = invocationArguments(toolchain, request);
	assert.deepEqual(args.slice(0, toolchain.compileArguments.length), toolchain.compileArguments);
	assert.ok(args.indexOf('--platform-backend') < args.indexOf('--path'));
	assert.ok(args.indexOf('--gui-backend') < args.indexOf('--path'));
	assert.ok(args.indexOf('--path') < args.indexOf('gui-preview'));
	assert.equal(args[args.indexOf('gui-preview') + 1], request.input);
	assert.deepEqual(args.slice(args.indexOf('--') + 1), request.args);
	assert.deepEqual(args.slice(args.indexOf('--frames'), args.indexOf('--frames') + 2), ['--frames', '3']);
});

test('check and compile keep JIT modules and preview arguments out of offline tasks', () => {
	for (const action of ['check', 'compile'] as const) {
		const args = invocationArguments(toolchain, { ...request, action, output: path.join(root, 'app.vsapp'), assets: [path.join(root, 'a b.tga')] });
		assert.ok(args.includes('--backend=vm'));
		for (const option of ['--jit-backend', '--backend=jit', '--frames', '--check', '--']) {
			assert.ok(!args.includes(option), option);
		}
		if (action === 'compile') {
			assert.equal(args[args.indexOf('--asset') + 1], path.join(root, 'a b.tga'));
		}
	}
});

test('selects font and text-layout providers independently before loading the environment', () => {
	const backends = backendValues({ font: 'font-backend', 'text-layout': 'alternate-layout' });
	const args = invocationArguments(toolchain, { ...request, backends });
	assert.equal(args[args.indexOf('--font-backend') + 1], 'font-backend');
	assert.equal(args[args.indexOf('--text-layout-backend') + 1], 'alternate-layout');
	assert.ok(args.indexOf('--text-layout-backend') < args.indexOf('--path'));
});

test('preserves explicit automatic and disabled service selections while empty values inherit the host', () => {
	const backends = backendValues({ platform: '', graphics: 'auto', gui: 'auto', image: 'none', font: 'none', 'text-layout': 'auto' });
	assert.equal(backends.platform, undefined);
	for (const action of ['check', 'compile', 'preview'] as const) {
		const args = invocationArguments(toolchain, { ...request, action, backends, output: path.join(root, 'app.vsapp') });
		assert.ok(!args.includes('--platform-backend'));
		for (const [role, value] of Object.entries(backends)) {
			const offset = args.indexOf(`--${role}-backend`);
			assert.ok(offset >= 0 && offset < args.indexOf('--path'), role);
			assert.equal(args[offset + 1], value);
		}
	}
	const inherited = invocationArguments(toolchain, { ...request, backends: backendValues({}) });
	for (const role of ['platform', 'graphics', 'gui', 'image', 'font', 'text-layout']) {
		assert.ok(!inherited.includes(`--${role}-backend`), role);
	}
});

test('rejects unsupported native inputs, invalid frame counts and non-application outputs', () => {
	for (const frames of [-1, 1.5, Number.MAX_SAFE_INTEGER + 1, NaN]) {
		assert.throws(() => invocationArguments(toolchain, { ...request, frames }));
	}
	assert.throws(() => invocationArguments(toolchain, { ...request, input: path.join(root, 'main.vs') }));
	assert.throws(() => invocationArguments(toolchain, { ...request, input: path.join(root, 'page.vswcss') }));
	assert.throws(() => invocationArguments(toolchain, { ...request, action: 'compile', output: path.join(root, 'page.vswml') }));
	assert.throws(() => invocationArguments(toolchain, { ...request, action: 'compile', input: path.join(root, 'app.vsapp'), output: path.join(root, 'app.vsapp') }));
	assert.ok(!invocationArguments(toolchain, { ...request, frames: 0, input: path.join(root, 'main.vs') }).includes('--frames'));
	for (const role of ['unknown', 'vm', 'jit']) {
		assert.throws(() => backendValues({ [role]: 'auto' }), /Unsupported GUI backend role/, role);
	}
	assert.throws(() => backendValues({ gui: 1 }));
});
