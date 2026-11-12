// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as path from 'node:path';
import type { VeroScriptToolchain } from '../../VeroScriptVSCode/Public/api';

export interface GuiInvocation {
	readonly action: 'check' | 'compile' | 'preview';
	readonly input: string;
	readonly output?: string;
	readonly name: string;
	readonly frames: number;
	readonly assets: readonly string[];
	readonly args: readonly string[];
	readonly backends: Readonly<Record<string, string>>;
	readonly environmentPath: string;
}

export class GuiArgumentError extends Error {
	constructor(message: string, ...args: string[]) {
		super(message);
		this.args = args;
	}

	readonly args: readonly string[];
}

export function stringValue(value: unknown, key: string): string {
	if (typeof value !== 'string' || value.includes('\0')) {
		throw new GuiArgumentError('Expected a string for {0}.', key);
	}
	return value;
}

export function stringList(value: unknown, key: string): string[] {
	if (!Array.isArray(value)) {
		throw new GuiArgumentError('Expected an array of strings for {0}.', key);
	}
	return value.map(item => stringValue(item, key));
}

export function backendValues(value: unknown): Record<string, string> {
	if (!value || typeof value !== 'object' || Array.isArray(value)) {
		throw new GuiArgumentError('Expected an object for {0}.', 'backends');
	}
	const result: Record<string, string> = {};
	for (const [key, item] of Object.entries(value)) {
		if (!['platform', 'graphics', 'gui', 'image', 'font', 'text-layout'].includes(key)) {
			throw new GuiArgumentError('Unsupported GUI backend role: {0}.', key);
		}
		const backend = stringValue(item, `backends.${key}`);
		if (backend) {
			result[key] = backend;
		}
	}
	return result;
}

/** Compiler flags precede GUI backend choices; choices precede package environment loading. */
export function invocationArguments(toolchain: VeroScriptToolchain, request: GuiInvocation): string[] {
	const extension = path.extname(request.input);
	if (!['.vs', '.vswml', '.vsapp'].includes(extension)) {
		throw new GuiArgumentError('Expected a .vs, .vswml or .vsapp GUI entry.');
	}
	if (!Number.isSafeInteger(request.frames) || request.frames < 0) {
		throw new GuiArgumentError('Preview frames must be a nonnegative safe integer.');
	}
	if (request.action === 'preview' && request.frames && extension !== '.vswml') {
		throw new GuiArgumentError('Frame limits apply only to .vswml previews. VS entries own their execution loop.');
	}
	if (request.action === 'compile' && (extension === '.vsapp' || !request.output || path.extname(request.output) !== '.vsapp')) {
		throw new GuiArgumentError('GUI compilation needs source input and a .vsapp output.');
	}
	if (request.action === 'compile' && request.output && path.relative(request.input, request.output) === '') {
		throw new GuiArgumentError('The GUI output must differ from the input.');
	}
	const result = [...toolchain.compileArguments,
		...(request.action === 'preview' ? toolchain.executionArguments : ['--backend=vm'])];
	for (const role of ['platform', 'graphics', 'gui', 'image', 'font', 'text-layout']) {
		if (request.backends[role]) {
			result.push(`--${role}-backend`, stringValue(request.backends[role], `backends.${role}`));
		}
	}
	if (request.environmentPath) {
		result.push('--path', stringValue(request.environmentPath, 'environmentPath'));
	}
	result.push(`gui-${request.action}`, stringValue(request.input, 'input'));
	if (request.action === 'compile') {
		result.push('--output', request.output!, '--name', stringValue(request.name, 'name'));
		for (const asset of request.assets) {
			result.push('--asset', stringValue(asset, 'assets'));
		}
	}
	if (request.action === 'preview') {
		if (request.frames) {
			result.push('--frames', String(request.frames));
		}
		if (request.args.length) {
			result.push('--', ...request.args.map(value => stringValue(value, 'args')));
		}
	}
	return result;
}
