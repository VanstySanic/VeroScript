// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as assert from 'node:assert/strict';
import * as net from 'node:net';
import * as path from 'node:path';
import * as vscode from 'vscode';
import { suite, test, suiteSetup, suiteTeardown } from 'mocha';
import { Services } from '../../context';
import { VeroDebugger } from '../../debugger';
import { DomainDebugAdapter } from '../../domainDebugAdapter';
import { extensionUnderTest } from './host';

interface Message {
	seq: number;
	type: string;
	command?: string;
	event?: string;
	request_seq?: number;
	body?: Record<string, any>;
	arguments?: Record<string, any>;
	success?: boolean;
}

async function until(ready: () => boolean, description: string): Promise<void> {
	const deadline = Date.now() + 10000;
	while (!ready()) {
		if (Date.now() >= deadline) {
			throw new Error(`Timed out: ${description}`);
		}
		await new Promise(resolve => setTimeout(resolve, 25));
	}
}

/** Protocol fixture only: it deliberately does not implement or execute a VM. */
class DomainHost {
	async listen(): Promise<number> {
		this.server.on('connection', socket => {
			this.socket = socket;
			this.sockets.add(socket);
			let buffer = Buffer.alloc(0);
			socket.on('close', () => this.sockets.delete(socket));
			socket.on('error', () => {});
			socket.on('data', data => {
				buffer = Buffer.concat([buffer, data]);
				for (;;) {
					const offset = buffer.indexOf('\r\n\r\n');
					if (offset < 0) return;
					const length = Number(/Content-Length:\s*(\d+)/i.exec(buffer.subarray(0, offset).toString())![1]);
					if (buffer.length < offset + 4 + length) return;
					const request: Message = JSON.parse(buffer.subarray(offset + 4, offset + 4 + length).toString());
					buffer = buffer.subarray(offset + 4 + length);
					this.requests.push(request);
					this.dispatch(request);
				}
			});
		});
		await new Promise<void>(resolve => this.server.listen(0, '127.0.0.1', resolve));
		return (this.server.address() as net.AddressInfo).port;
	}

	stop(): void {
		this.activeContext = true;
		this.event('stopped', { reason: 'breakpoint', threadId: 1, allThreadsStopped: true });
	}

	finish(): void {
		this.activeContext = false;
		this.event('terminated', {});
	}

	completeRequest(command: string, body: object): void {
		const request = this.requests.find(message => message.type === 'request' && message.command === command);
		assert.ok(request);
		this.send({ type: 'response', request_seq: request.seq, command, success: true, body });
	}

	requestClient(command: string): number {
		const seq = this.sequence;
		this.send({ type: 'request', command, arguments: {} });
		return seq;
	}

	async close(): Promise<void> {
		for (const socket of this.sockets) {
			socket.destroy();
		}
		await new Promise<void>(resolve => this.server.close(() => resolve()));
	}

	private send(message: object): void {
		const body = Buffer.from(JSON.stringify({ seq: this.sequence++, ...message }));
		this.socket!.write(Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]));
	}

	private event(event: string, body: object): void {
		this.send({ type: 'event', event, body });
	}

	private dispatch(request: Message): void {
		if (request.type !== 'request') return;
		if (request.command === this.ignoreCommand) return;
		const reply = (body: object = {}, success = true, message?: string) => this.send({ type: 'response', request_seq: request.seq, command: request.command, success, body, message });
		switch (request.command) {
			case 'initialize': {
				if (this.malformed) {
					this.socket!.write('Content-Length: nope\r\n\r\n{}');
					return;
				}
				// Fragment both the header and a multibyte UTF-8 body across TCP writes.
				const body = Buffer.from(JSON.stringify({ seq: this.sequence++, type: 'response', request_seq: request.seq, command: 'initialize', success: true,
					body: { supportsConfigurationDoneRequest: true, supportsSetVariable: true, supportsTerminateRequest: true, supportTerminateDebuggee: true,
						...(this.advertise === undefined ? {} : { veroscriptSupportsDomainAttach: this.advertise }), name: '宿主' } }));
				const frame = Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]);
				const socket = this.socket!;
				socket.write(frame.subarray(0, 9));
				setTimeout(() => {
					if (socket.destroyed) return;
					socket.write(frame.subarray(9, frame.length - 2));
					setTimeout(() => {
						if (socket.destroyed) return;
						socket.write(frame.subarray(frame.length - 2));
						this.event('initialized', {});
					}, 5);
				}, 5);
				return;
			}
			case 'attach': {
				if (request.arguments?.domainId && request.arguments.domainId !== 'game') {
					reply({}, false, 'domain identity does not match');
					return;
				}
				reply({ veroscriptHostId: 'fixture-host', veroscriptDomainId: 'game', veroscriptTargetName: '游戏',
					veroscriptSourceRevision: 'source-7', veroscriptBindingRevision: 'bindings-3',
					veroscriptSources: [{ path: '/host-only/source.vs', version: 'opaque-version' }],
					...this.capabilities });
				return;
			}
			case 'configurationDone': {
				reply();
				this.stop();
				return;
			}
			case 'threads': return reply({ threads: this.activeContext ? [{ id: 1, name: 'host' }] : [] });
			case 'stackTrace': return reply({ stackFrames: [{ id: 10, name: 'host callback', line: 8, column: 1,
				source: { path: path.join(process.env.VEROSCRIPT_TEST_SESSION!, 'workspace A/hello world 中文.vs') } }], totalFrames: 1 });
			case 'scopes': return reply({ scopes: [{ name: 'Locals', variablesReference: 1, expensive: false }] });
			case 'variables': return reply({ variables: [
				{ name: 'existingValue', value: '73', type: 'int64', variablesReference: 0,
					memoryReference: '18446744073709551615', veroMemory: { objectId: '9007199254740993', byteLength: '8' } },
				{ name: 'snapshot', value: '7', type: 'int', variablesReference: 0,
					presentationHint: { kind: 'data', visibility: 'public', attributes: ['readOnly'] } }
			] });
			case 'evaluate': return reply({ result: '73', variablesReference: 0 });
			case 'continue': {
				this.activeContext = false;
				reply({ allThreadsContinued: true });
				this.event('continued', { threadId: 1, allThreadsContinued: true });
				return;
			}
			case 'setBreakpoints': return reply({ breakpoints: [] });
			case 'disconnect': {
				if (this.finishOnDisconnect) {
					this.finish();
				}
				reply();
				this.socket!.end();
				return;
			}
			default: return reply();
		}
	}

	advertise: boolean | undefined = true;
	malformed = false;
	finishOnDisconnect = false;
	ignoreCommand: string | undefined;
	readonly capabilities: Record<string, boolean> = {
		veroscriptCanPause: false, veroscriptCanStep: false, veroscriptCanEvaluate: true,
		veroscriptCanUseConditionalBreakpoints: false, veroscriptCanSetVariable: false, veroscriptCanTerminate: false
	};
	readonly requests: Message[] = [];
	private readonly server = net.createServer();
	private readonly sockets = new Set<net.Socket>();
	private socket: net.Socket | undefined;
	private sequence = 1;
	private activeContext = false;
}

suite('VeroScript domain attachment', () => {
	const resources: vscode.Disposable[] = [];
	let output: vscode.LogOutputChannel;
	suiteSetup(async () => {
		await (await extensionUnderTest()).activate();
		output = vscode.window.createOutputChannel('VeroScript domain tests', { log: true });
		resources.push(output);
	});
	suiteTeardown(() => {
		for (const resource of resources) {
			resource.dispose();
		}
	});

	test('normalizes domain configurations without Runtime, source files or execution settings', async () => {
		const services = new Services(output);
		services.runtime = async () => { throw new Error('Domain must not resolve Runtime'); };
		const errors: unknown[] = [];
		services.report = error => errors.push(error);
		const provider = new VeroDebugger(services);
		const base = { name: 'domain', type: 'veroscript', request: 'attach', attachMode: 'domain', port: 4711 };
		const config = await provider.resolveDebugConfigurationWithSubstitutedVariables(undefined, base);
		assert.deepEqual(config, { ...base, host: '127.0.0.1' });
		const unresolved = { ...base, host: '${input:host}', port: '${input:port}', domainId: '${input:domain}' };
		assert.deepEqual(await provider.resolveDebugConfiguration(undefined, unresolved), unresolved, 'defer input variables to VS Code');
		for (const key of ['program', 'cwd', 'workspace', 'env', 'importPaths', 'defines', 'bindingCatalogPaths', 'args', 'input', 'stopOnEntry', 'maxCallDepth', 'debugServer', 'jitBackendPath']) {
			assert.equal(await provider.resolveDebugConfigurationWithSubstitutedVariables(undefined, { ...base, [key]: '' }), undefined, key);
		}
		assert.equal(await provider.resolveDebugConfigurationWithSubstitutedVariables(undefined, { ...base, domainId: '\0' }), undefined);
		assert.equal(await provider.resolveDebugConfigurationWithSubstitutedVariables(undefined, { ...base, attachMode: 'typo' }), undefined);
		assert.ok(errors.length >= 14);
		const prompts: vscode.InputBoxOptions[] = [];
		const canceled = new VeroDebugger(services, async options => { prompts.push(options!); return undefined; });
		assert.equal(await canceled.attachToDomain(), undefined);
		assert.equal(prompts.length, 1, 'domain command needs no file picker');
	});

	test('uses a persistent domain, reports host capabilities and disconnects without termination', async () => {
		const host = new DomainHost();
		host.finishOnDisconnect = true;
		const port = await host.listen();
		const messages: Message[] = [];
		const tracker = vscode.debug.registerDebugAdapterTrackerFactory('veroscript', { createDebugAdapterTracker: () => ({ onDidSendMessage: message => messages.push(message) }) });
		try {
			assert.ok(await vscode.debug.startDebugging(undefined, { name: 'Live domain fixture', type: 'veroscript', request: 'attach', attachMode: 'domain', domainId: 'game', port }));
			await until(() => messages.some(message => message.event === 'stopped'), 'first host call');
			const session = vscode.debug.activeDebugSession!;
			assert.equal(session.configuration.program, undefined);
			assert.deepEqual(host.requests.find(message => message.command === 'attach')?.arguments?.domainId, 'game');
			assert.ok(!host.requests.some(message => message.command === 'launch'));
			const capabilities = messages.find(message => message.command === 'initialize' && message.success)?.body;
			assert.equal(capabilities?.supportsTerminateRequest, false);
			assert.equal(capabilities?.supportTerminateDebuggee, false);
			assert.ok(messages.some(message => message.event === 'capabilities' && message.body?.capabilities.supportsSetVariable === false));
			assert.ok(messages.some(message => message.event === 'output' && message.body?.output.includes('fixture-host')));
			assert.equal((await session.customRequest('evaluate', { expression: 'existingValue', frameId: 10, context: 'watch' })).result, '73');
			const variables = await session.customRequest('variables', { variablesReference: 1 });
			assert.equal(variables.variables[0].memoryReference, '18446744073709551615');
			assert.deepEqual(variables.variables[0].veroMemory, { objectId: '9007199254740993', byteLength: '8' }, 'domain forwarding preserves uint64 decimal strings');
			assert.equal(variables.variables[0].presentationHint, undefined, 'unmarked variables remain unmarked');
			assert.deepEqual(variables.variables[1].presentationHint,
				{ kind: 'data', visibility: 'public', attributes: ['readOnly'] }, 'domain forwarding preserves the complete native presentation hint');
			await assert.rejects(Promise.resolve(session.customRequest('pause', { threadId: 1 })));
			await assert.rejects(Promise.resolve(session.customRequest('setVariable', { variablesReference: 1, name: 'existingValue', value: '74' })));
			await assert.rejects(Promise.resolve(session.customRequest('terminate')));
			assert.ok(!host.requests.some(message => message.command === 'pause' || message.command === 'setVariable' || message.command === 'terminate'));
			await session.customRequest('continue', { threadId: 1 });
			assert.deepEqual((await session.customRequest('threads')).threads, [], 'a completed host call does not end the connection');
			host.stop();
			await until(() => messages.filter(message => message.event === 'stopped').length === 2, 'second host call in the same connection');
			assert.equal(vscode.debug.activeDebugSession?.id, session.id);
			const ended = new Promise<void>(resolve => {
				const listener = vscode.debug.onDidTerminateDebugSession(terminated => {
					if (terminated.id === session.id) { listener.dispose(); resolve(); }
				});
			});
			await vscode.debug.stopDebugging(session);
			await ended;
			assert.equal(host.requests.find(message => message.command === 'disconnect')?.arguments?.terminateDebuggee, false);
			assert.ok(messages.some(message => message.command === 'disconnect' && message.success), 'termination before the disconnect response preserves that response');
			assert.ok(!host.requests.some(message => message.command === 'terminate'));
		}
		finally
		{
			if (vscode.debug.activeDebugSession?.name === 'Live domain fixture') {
				await vscode.debug.stopDebugging(vscode.debug.activeDebugSession);
			}
			tracker.dispose();
			await host.close();
		}
	});

	test('completes the editor disconnect handshake after the host terminates its debug session', async () => {
		const host = new DomainHost();
		const port = await host.listen();
		const messages: Message[] = [];
		const tracker = vscode.debug.registerDebugAdapterTrackerFactory('veroscript', { createDebugAdapterTracker: () => ({ onDidSendMessage: message => messages.push(message) }) });
		let session: vscode.DebugSession | undefined;
		let terminated = false;
		const ended = vscode.debug.onDidTerminateDebugSession(value => {
			if (value.id === session?.id) {
				terminated = true;
			}
		});
		try {
			assert.ok(await vscode.debug.startDebugging(undefined, { name: 'Terminating domain fixture', type: 'veroscript', request: 'attach', attachMode: 'domain', port }));
			await until(() => messages.some(message => message.event === 'stopped'), 'host context');
			session = vscode.debug.activeDebugSession!;
			host.finish();
			await until(() => host.requests.some(message => message.command === 'disconnect'), 'editor disconnect after host termination');
			await until(() => terminated, 'editor debug session released');
			assert.equal(host.requests.find(message => message.command === 'disconnect')!.arguments!.terminateDebuggee, false);
			assert.equal(messages.filter(message => message.event === 'terminated').length, 1);
			assert.ok(!host.requests.some(message => message.command === 'terminate'));
		}
		finally
		{
			if (session && !terminated) {
				await vscode.debug.stopDebugging(session);
			}
			ended.dispose();
			tracker.dispose();
			await host.close();
		}
	});

	test('settles requests once across host termination, late replies and termination before disconnect completes', async () => {
		const host = new DomainHost();
		host.ignoreCommand = 'evaluate';
		host.finishOnDisconnect = true;
		const port = await host.listen();
		const adapter = new DomainDebugAdapter('127.0.0.1', port, output);
		const messages: Message[] = [];
		const listener = adapter.onDidSendMessage(message => messages.push(message as Message));
		try {
			adapter.handleMessage({ seq: 1, type: 'request', command: 'initialize' });
			await until(() => messages.some(message => message.command === 'initialize'), 'initialize');
			adapter.handleMessage({ seq: 2, type: 'request', command: 'attach', arguments: { attachMode: 'domain' } });
			await until(() => messages.some(message => message.command === 'attach'), 'attach');
			adapter.handleMessage({ seq: 3, type: 'request', command: 'evaluate', arguments: { expression: 'existingValue' } });
			await until(() => host.requests.some(message => message.seq === 3), 'pending evaluation');
			host.finish();
			await until(() => messages.some(message => message.event === 'terminated'), 'host termination');
			assert.equal(messages.find(message => message.request_seq === 3)?.success, false);
			adapter.handleMessage({ seq: 4, type: 'request', command: 'evaluate', arguments: { expression: 'existingValue' } });
			assert.equal(messages.find(message => message.request_seq === 4)?.success, false);
			host.completeRequest('evaluate', { result: 'late response', variablesReference: 0 });
			adapter.handleMessage({ seq: 5, type: 'request', command: 'disconnect', arguments: { terminateDebuggee: true } });
			await until(() => messages.some(message => message.request_seq === 5), 'disconnect response');
			assert.equal(messages.find(message => message.request_seq === 5)?.success, true);
			assert.equal(messages.filter(message => message.request_seq === 3).length, 1, 'late replies do not complete rejected requests again');
			assert.equal(messages.filter(message => message.event === 'terminated').length, 1);
			assert.ok(!host.requests.some(message => message.seq === 4), 'terminated sessions reject new operations locally');
			assert.equal(host.requests.find(message => message.command === 'disconnect')!.arguments!.terminateDebuggee, false);
		}
		finally
		{
			listener.dispose();
			adapter.dispose();
			await host.close();
		}
	});

	test('rejects legacy hosts before sending attach and handles malformed framing', async () => {
		for (const mode of ['missing', 'false', 'malformed']) {
			const host = new DomainHost();
			host.advertise = mode === 'missing' ? undefined : false;
			host.malformed = mode === 'malformed';
			const port = await host.listen();
			const adapter = new DomainDebugAdapter('127.0.0.1', port, output);
			const messages: Message[] = [];
			const listener = adapter.onDidSendMessage(message => messages.push(message as Message));
			try {
				adapter.handleMessage({ seq: 1, type: 'request', command: 'initialize', arguments: {} });
				await until(() => messages.some(message => message.event === 'terminated'), `reject ${mode} host`);
				assert.ok(messages.some(message => message.command === 'initialize' && message.success === false));
				assert.ok(!host.requests.some(message => message.command === 'attach' || message.command === 'launch'));
			}
			finally
			{
				listener.dispose();
				adapter.dispose();
				await host.close();
			}
		}
	});

	test('honors enabled and disabled domain capabilities and preserves domain identity failures', async () => {
		for (const enabled of [false, true]) {
			const host = new DomainHost();
			for (const key of Object.keys(host.capabilities)) {
				host.capabilities[key] = enabled;
			}
			const port = await host.listen();
			const adapter = new DomainDebugAdapter('127.0.0.1', port, output);
			const messages: Message[] = [];
			const listener = adapter.onDidSendMessage(message => messages.push(message as Message));
			let sequence = 1;
			const request = async (command: string, args: object = {}) => {
				const seq = sequence++;
				adapter.handleMessage({ seq, type: 'request', command, arguments: args });
				await until(() => messages.some(message => message.type === 'response' && message.request_seq === seq), command);
				return messages.find(message => message.type === 'response' && message.request_seq === seq)!;
			};
			try {
				assert.equal((await request('initialize')).success, true);
				assert.equal((await request('attach', { attachMode: 'domain', domainId: 'wrong-domain' })).success, false);
				assert.ok(!host.requests.some(message => message.command === 'launch'));
				assert.equal((await request('attach', { attachMode: 'domain', domainId: 'game' })).success, true);
				const capabilities = messages.find(message => message.event === 'capabilities')!.body!.capabilities;
				assert.equal(capabilities.supportsSetVariable, enabled);
				assert.equal(capabilities.supportsEvaluateForHovers, enabled);
				assert.equal(capabilities.supportsConditionalBreakpoints, enabled);
				const variables = (await request('variables', { variablesReference: 1 })).body!.variables;
				assert.equal(variables[0].presentationHint, undefined);
				assert.deepEqual(variables[1].presentationHint.attributes, ['readOnly'], 'per-variable readonly status is independent of the domain editing capability');
				const operations: [string, object][] = [
					['pause', { threadId: 1 }], ['next', { threadId: 1 }], ['stepIn', { threadId: 1 }], ['stepOut', { threadId: 1 }],
					['evaluate', { expression: 'existingValue' }], ['setVariable', { variablesReference: 1, name: 'existingValue', value: '74' }],
					['setBreakpoints', { source: { path: '/host-only/source.vs' }, breakpoints: [{ line: 1, condition: 'existingValue == 73' }] }]
				];
				for (const [command, args] of operations) {
					const before = host.requests.length;
					assert.equal((await request(command, args)).success, enabled, command);
					assert.equal(host.requests.length - before, enabled ? 1 : 0, command);
				}
				assert.equal((await request('setBreakpoints', { source: { path: '/host-only/source.vs' }, breakpoints: [] })).success, true);
				assert.equal((await request('terminate')).success, false, 'domain cancellation stays host-owned even if supported');
				assert.equal((await request('disconnect', { terminateDebuggee: true })).success, true);
				await until(() => messages.some(message => message.event === 'terminated'), 'disconnect');
				assert.equal(host.requests.find(message => message.command === 'disconnect')!.arguments!.terminateDebuggee, false);
				assert.equal(new Set(messages.map(message => message.seq)).size, messages.length, 'injected events keep adapter sequences unique');
			}
			finally
			{
				listener.dispose();
				adapter.dispose();
				await host.close();
			}
		}
	});

	test('maps reverse-request replies back to host sequences after injecting capability events', async () => {
		const host = new DomainHost();
		const port = await host.listen();
		const adapter = new DomainDebugAdapter('127.0.0.1', port, output);
		const messages: Message[] = [];
		const listener = adapter.onDidSendMessage(message => messages.push(message as Message));
		try {
			adapter.handleMessage({ seq: 1, type: 'request', command: 'initialize' });
			await until(() => messages.some(message => message.command === 'initialize'), 'reverse initialize');
			adapter.handleMessage({ seq: 2, type: 'request', command: 'attach', arguments: { attachMode: 'domain' } });
			await until(() => messages.some(message => message.event === 'capabilities'), 'reverse attach');
			const hostSequences = [host.requestClient('runInTerminal'), host.requestClient('startDebugging')];
			await until(() => messages.filter(message => message.type === 'request').length === 2, 'host reverse requests');
			const forwarded = messages.filter(message => message.type === 'request');
			assert.notEqual(forwarded[0]!.seq, hostSequences[0], 'injected events changed adapter numbering');
			for (const [index, request] of [...forwarded.entries()].reverse()) {
				adapter.handleMessage({ seq: 10 + index, type: 'response', request_seq: request.seq, command: request.command,
					success: index === 0, body: { value: `reply-${index}` } });
			}
			await until(() => host.requests.filter(message => message.type === 'response').length === 2, 'reverse replies');
			for (const [index, seq] of hostSequences.entries()) {
				const reply = host.requests.find(message => message.type === 'response' && message.request_seq === seq);
				assert.ok(reply, `host request ${seq} receives its matching reply`);
				assert.equal(reply.success, index === 0);
				assert.deepEqual(reply.body, { value: `reply-${index}` });
			}
		}
		finally
		{
			listener.dispose();
			adapter.dispose();
			await host.close();
		}
	});

	test('rejects pending requests and ends the session once when the host connection closes', async () => {
		const host = new DomainHost();
		host.ignoreCommand = 'evaluate';
		const port = await host.listen();
		const adapter = new DomainDebugAdapter('127.0.0.1', port, output);
		const messages: Message[] = [];
		const listener = adapter.onDidSendMessage(message => messages.push(message as Message));
		try {
			adapter.handleMessage({ seq: 1, type: 'request', command: 'initialize' });
			await until(() => messages.some(message => message.command === 'initialize'), 'initialize');
			adapter.handleMessage({ seq: 2, type: 'request', command: 'attach', arguments: { attachMode: 'domain' } });
			await until(() => messages.some(message => message.command === 'attach'), 'attach');
			adapter.handleMessage({ seq: 3, type: 'request', command: 'evaluate', arguments: { expression: 'existingValue' } });
			await until(() => host.requests.some(message => message.command === 'evaluate'), 'pending evaluation');
			await host.close();
			await until(() => messages.some(message => message.event === 'terminated'), 'closed host');
			assert.ok(messages.some(message => message.request_seq === 3 && message.success === false));
			adapter.handleMessage({ seq: 4, type: 'request', command: 'disconnect' });
			assert.equal(messages.find(message => message.request_seq === 4)?.success, true, 'a closed connection still completes the editor disconnect');
			assert.equal(messages.filter(message => message.event === 'terminated').length, 1);
		}
		finally
		{
			listener.dispose();
			adapter.dispose();
			await host.close();
		}
	});
});
