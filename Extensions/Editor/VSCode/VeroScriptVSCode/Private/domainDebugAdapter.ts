// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import { Socket } from 'node:net';
import * as vscode from 'vscode';

interface DapMessage extends vscode.DebugProtocolMessage {
	type: string;
	seq: number;
	command?: string;
	event?: string;
	request_seq?: number;
	success?: boolean;
	message?: string;
	arguments?: Record<string, unknown>;
	body?: Record<string, unknown>;
}

/** Bridges domain DAP with a capability check before any attach reaches the host. */
export class DomainDebugAdapter implements vscode.DebugAdapter {
	constructor(host: string, port: number, private readonly output: vscode.LogOutputChannel) {
		this.address = `${host.includes(':') ? `[${host}]` : host}:${port}`;
		this.socket.on('data', data => this.read(data));
		this.socket.on('error', error => this.fail(error.message));
		this.socket.on('close', () => this.terminate());
		this.socket.on('connect', () => {
			this.output.info(`Domain DAP ${this.address}: connected`);
		});
		this.timer = setTimeout(() => this.fail(vscode.l10n.t('The host did not complete the debug handshake in time.')), 10000);
		this.socket.connect(port, host);
	}

	readonly onDidSendMessage = (listener: (message: vscode.DebugProtocolMessage) => unknown, thisArgs?: unknown, disposables?: vscode.Disposable[]) =>
		this.messages.event(listener, thisArgs, disposables);

	handleMessage(rawMessage: vscode.DebugProtocolMessage): void {
		let message = rawMessage as DapMessage;
		if (this.disposed) return;
		if (this.ended) {
			// The transport can close before the editor sends its final disconnect.
			if (message.type === 'request') {
				if (message.command === 'disconnect') {
					this.send({ seq: 0, type: 'response', command: message.command, request_seq: message.seq, success: true });
				}
				else
				{
					this.reject(message, vscode.l10n.t('The host debug connection closed.'));
				}
			}
			return;
		}
		if (this.hostTerminated && message.type !== 'request') return;
		if (message.type === 'response' && message.request_seq !== undefined) {
			const hostSequence = this.reverseRequests.get(message.request_seq);
			if (hostSequence !== undefined) {
				this.reverseRequests.delete(message.request_seq);
				message = { ...message, request_seq: hostSequence };
			}
		}
		if (message.type === 'request') {
			this.pending.set(message.seq, message);
			if (this.hostTerminated && message.command !== 'disconnect') {
				this.reject(message, vscode.l10n.t('The host debug connection closed.'));
				return;
			}
			if (message.command === 'attach' && !this.supportsDomain) {
				this.reject(message, vscode.l10n.t('This host does not advertise VeroScript domain attachment support.'));
				return;
			}
			if (message.command === 'pause' && this.canPause === false) {
				this.reject(message, vscode.l10n.t('This host domain does not support pausing.'));
				return;
			}
			if (message.command === 'setVariable' && this.canSetVariable === false) {
				this.reject(message, vscode.l10n.t('This host domain does not support variable editing.'));
				return;
			}
			if (['next', 'stepIn', 'stepOut'].includes(message.command ?? '') && this.canStep === false) {
				this.reject(message, vscode.l10n.t('This host domain does not support stepping.'));
				return;
			}
			if (message.command === 'evaluate' && this.canEvaluate === false) {
				this.reject(message, vscode.l10n.t('This host domain does not support expression evaluation.'));
				return;
			}
			if (message.command === 'setBreakpoints' && this.canUseConditionalBreakpoints === false
				&& Array.isArray(message.arguments?.breakpoints)
				&& message.arguments.breakpoints.some(breakpoint => typeof breakpoint?.condition === 'string' && breakpoint.condition.length > 0)) {
				this.reject(message, vscode.l10n.t('This host domain does not support conditional breakpoints.'));
				return;
			}
			if (message.command === 'terminate') {
				this.reject(message, vscode.l10n.t('Disconnect from this domain; the host owns script cancellation.'));
				return;
			}
			if (message.command === 'disconnect') {
				message = { ...message, arguments: { ...message.arguments, terminateDebuggee: false } };
			}
		}
		const body = Buffer.from(JSON.stringify(message), 'utf8');
		this.socket.write(Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, 'ascii'), body]));
	}

	dispose(): void {
		this.disposed = true;
		clearTimeout(this.timer);
		this.socket.destroy();
		this.pending.clear();
		this.reverseRequests.clear();
		this.buffer = Buffer.alloc(0);
		this.messages.dispose();
	}

	private read(data: Buffer): void {
		if (this.disposed || this.ended) return;
		this.buffer = Buffer.concat([this.buffer, data]);
		try {
			while (this.buffer.length) {
				const headerEnd = this.buffer.indexOf('\r\n\r\n');
				if (headerEnd < 0) {
					if (this.buffer.length > 8192) {
						throw new Error('DAP header exceeds 8192 bytes.');
					}
					return;
				}
				if (headerEnd > 8192) {
					throw new Error('DAP header exceeds 8192 bytes.');
				}
				const lengths = [...this.buffer.subarray(0, headerEnd).toString('ascii').matchAll(/^Content-Length:\s*(\d+)\s*$/gim)];
				const length = lengths.length === 1 ? Number(lengths[0]![1]) : NaN;
				if (!Number.isSafeInteger(length) || length <= 0 || length > 16 * 1024 * 1024) {
					throw new Error('Invalid DAP Content-Length.');
				}
				const end = headerEnd + 4 + length;
				if (this.buffer.length < end) return;
				const message: DapMessage = JSON.parse(this.buffer.subarray(headerEnd + 4, end).toString('utf8'));
				this.buffer = this.buffer.subarray(end);
				if (!message || typeof message !== 'object' || typeof message.type !== 'string' || !Number.isSafeInteger(message.seq)) {
					throw new Error('Invalid DAP message.');
				}
				this.receive(message);
				if (this.ended) return;
			}
		}
		catch (error)
		{
			this.fail(error instanceof Error ? error.message : String(error));
		}
	}

	private receive(message: DapMessage): void {
		if (message.type === 'response') {
			// Requests rejected at termination may still have a late host response.
			if (this.hostTerminated && !this.pending.has(message.request_seq!)) return;
			this.pending.delete(message.request_seq!);
			if (message.command === 'initialize' && message.success) {
				clearTimeout(this.timer);
				this.supportsDomain = message.body?.veroscriptSupportsDomainAttach === true;
				if (!this.supportsDomain) {
					message = { ...message, success: false, message: vscode.l10n.t('This host does not advertise VeroScript domain attachment support.'), body: undefined };
					this.send(message);
					this.terminate();
					this.socket.destroy();
					return;
				}
				message = { ...message, body: { ...message.body, supportsTerminateRequest: false, supportTerminateDebuggee: false } };
			}
			if (message.command === 'attach' && message.success) {
				this.canPause = message.body?.veroscriptCanPause === true;
				this.canSetVariable = message.body?.veroscriptCanSetVariable === true;
				this.canStep = message.body?.veroscriptCanStep === true;
				this.canEvaluate = message.body?.veroscriptCanEvaluate === true;
				this.canUseConditionalBreakpoints = message.body?.veroscriptCanUseConditionalBreakpoints === true;
				const info = message.body ?? {};
				this.output.info(`Domain DAP ${this.address}: ${JSON.stringify(info)}`);
				this.send(message);
				this.send({ seq: 0, type: 'event', event: 'capabilities', body: { capabilities: {
					supportsSetVariable: this.canSetVariable,
					supportsEvaluateForHovers: this.canEvaluate,
					supportsConditionalBreakpoints: this.canUseConditionalBreakpoints
				} } });
				this.send({ seq: 0, type: 'event', event: 'output', body: { category: 'console', output: vscode.l10n.t('Connected to host {0}, domain {1} ({2}).',
					String(info.veroscriptHostId ?? ''), String(info.veroscriptDomainId ?? ''), String(info.veroscriptTargetName ?? '')) + '\n' } });
				return;
			}
		}
		if (message.type === 'event' && message.event === 'terminated') {
			if (this.hostTerminated) return;
			this.hostTerminated = true;
			for (const request of [...this.pending.values()]) {
				if (request.command !== 'disconnect') {
					this.reject(request, vscode.l10n.t('The host debug connection closed.'));
				}
			}
			// VS Code follows this event with disconnect. Keep transport alive until
			// that handshake completes; the host still owns execution and teardown.
			clearTimeout(this.timer);
			this.reverseRequests.clear();
		}
		this.send(message);
	}

	private reject(request: DapMessage, reason: string): void {
		this.pending.delete(request.seq);
		this.send({ seq: 0, type: 'response', command: request.command, request_seq: request.seq, success: false, message: reason });
	}

	private fail(reason: string): void {
		if (this.disposed || this.ended) return;
		this.output.error(`Domain DAP ${this.address}: ${reason}`);
		for (const request of [...this.pending.values()]) {
			this.reject(request, reason);
		}
		this.send({ seq: 0, type: 'event', event: 'output', body: { category: 'stderr', output: reason + '\n' } });
		this.terminate();
		this.socket.destroy();
	}

	private terminate(): void {
		if (this.disposed || this.ended) return;
		this.ended = true;
		clearTimeout(this.timer);
		this.buffer = Buffer.alloc(0);
		this.reverseRequests.clear();
		for (const request of [...this.pending.values()]) {
			this.reject(request, vscode.l10n.t('The host debug connection closed.'));
		}
		this.output.info(`Domain DAP ${this.address}: disconnected`);
		if (!this.hostTerminated) {
			this.send({ seq: 0, type: 'event', event: 'terminated' });
		}
	}

	private send(message: DapMessage): void {
		if (!this.disposed) {
			const seq = this.nextSequence++;
			// Local capability/output events change host numbering. Reverse-request
			// responses must restore the host's original request_seq, even out of order.
			if (message.type === 'request') {
				this.reverseRequests.set(seq, message.seq);
			}
			this.messages.fire({ ...message, seq });
		}
	}

	private readonly socket = new Socket();
	private readonly messages = new vscode.EventEmitter<vscode.DebugProtocolMessage>();
	private readonly pending = new Map<number, DapMessage>();
	private readonly reverseRequests = new Map<number, number>();
	private readonly address: string;
	private readonly timer: ReturnType<typeof setTimeout>;
	private buffer: Buffer = Buffer.alloc(0);
	private supportsDomain = false;
	private canPause: boolean | undefined;
	private canSetVariable: boolean | undefined;
	private canStep: boolean | undefined;
	private canEvaluate: boolean | undefined;
	private canUseConditionalBreakpoints: boolean | undefined;
	private nextSequence = 1;
	private hostTerminated = false;
	private ended = false;
	private disposed = false;
}
