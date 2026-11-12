// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import org.eclipse.lsp4j.debug.*
import org.eclipse.lsp4j.debug.launch.DSPLauncher
import org.eclipse.lsp4j.debug.services.IDebugProtocolClient
import org.eclipse.lsp4j.debug.services.IDebugProtocolServer
import org.eclipse.lsp4j.jsonrpc.debug.DebugLauncher
import org.eclipse.lsp4j.jsonrpc.json.JsonRpcMethod
import java.io.Closeable
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Owns framing and transport. Protocol stdout is never attached to an IDE console reader. */
class DapConnection private constructor(
	input: InputStream, output: OutputStream, private val ownedProcess: Process?,
	private val socket: Socket?, private val client: IDebugProtocolClient,
	private val onClosed: (Throwable?) -> Unit, private val onStderr: (String) -> Unit,
	domain: Boolean = false
) : Closeable {
	private val closed = AtomicBoolean()
	/** Close initiates bounded asynchronous cleanup; callers removing the child's
	 * working directory must await its actual exit without blocking the IDE UI. */
	internal val processExit: CompletableFuture<Int>? = ownedProcess?.onExit()?.thenApply { it.exitValue() }
	private val io: ExecutorService = Executors.newCachedThreadPool { task ->
		Thread(task, "VeroScript DAP I/O").apply { isDaemon = true }
	}
	private val launcher = if (!domain) DSPLauncher.createClientLauncher(client, input, output, io) { it }
		else object : DebugLauncher.Builder<IDebugProtocolServer>() {
			override fun getSupportedMethods(): MutableMap<String, JsonRpcMethod> = super.getSupportedMethods().apply {
				put("initialize", JsonRpcMethod.request("initialize", VeroScriptDapCapabilities::class.java, InitializeRequestArguments::class.java))
				put("attach", JsonRpcMethod.request("attach", DomainDebugInfo::class.java, Map::class.java))
			}
		}.setLocalService(client).setRemoteInterface(IDebugProtocolServer::class.java)
			.setInput(input).setOutput(output).setExecutorService(io).create()
	val server: IDebugProtocolServer = launcher.remoteProxy
	private val listening = launcher.startListening()

	/** Standard attach returns Void; the native domain response carries required metadata. */
	fun attachDomain(arguments: Map<String, Any>): CompletableFuture<DomainDebugInfo> =
		launcher.remoteEndpoint.request("attach", arguments).thenApply { result ->
			requireNotNull(result as? DomainDebugInfo) { "Missing host domain attachment metadata" }
		}

	init {
		io.execute {
			val failure = runCatching { listening.get() }.exceptionOrNull()
			if (!closed.get()) onClosed(failure)
		}
		val process = ownedProcess
		if (process != null) io.execute {
			runCatching { process.errorStream.bufferedReader(Charsets.UTF_8).use { reader ->
				val buffer = CharArray(2048)
				while (!closed.get()) {
					val count = reader.read(buffer)
					if (count < 0) break
					onStderr(String(buffer, 0, count))
				}
			} }.onFailure { if (!closed.get()) onClosed(it) }
		}
	}

	fun disconnect(terminateDebuggee: Boolean): CompletableFuture<Void> =
		server.disconnect(DisconnectArguments().apply { this.terminateDebuggee = terminateDebuggee })
			.orTimeout(3, TimeUnit.SECONDS)

	override fun close() {
		if (!closed.compareAndSet(false, true)) return
		runCatching { socket?.close() }
		runCatching { ownedProcess?.outputStream?.close() }
		runCatching { ownedProcess?.inputStream?.close() }
		listening.cancel(true)
		// An external TCP host is never an owned process.
		ownedProcess?.let { process ->
			if (process.isAlive) {
				val reaper = Thread({
					if (!process.waitFor(2, TimeUnit.SECONDS)) process.destroyForcibly()
				}, "VeroScript DAP cleanup")
				// Keep shutdown alive for this bounded grace period, then reap the child.
				reaper.isDaemon = false
				reaper.start()
			}
		}
		io.shutdownNow()
	}

	companion object {
		fun launch(command: List<String>, cwd: java.nio.file.Path, client: IDebugProtocolClient,
			onClosed: (Throwable?) -> Unit, onStderr: (String) -> Unit,
			inheritParentEnvironment: Boolean = true): DapConnection {
			val builder = ProcessBuilder(command).directory(cwd.toFile())
			if (!inheritParentEnvironment) builder.environment().clear()
			val process = builder.start()
			return try { DapConnection(process.inputStream, process.outputStream, process, null, client, onClosed, onStderr) }
			catch (exception: Throwable)
			{
				process.destroyForcibly()
				throw exception
			}
		}

		fun attach(host: String, port: Int, client: IDebugProtocolClient,
			onClosed: (Throwable?) -> Unit, onStderr: (String) -> Unit, domain: Boolean = false): DapConnection {
			val socket = Socket()
			return try {
				socket.connect(InetSocketAddress(host, port), 5000)
				socket.tcpNoDelay = true
				DapConnection(socket.getInputStream(), socket.getOutputStream(), null, socket, client, onClosed, onStderr, domain)
			}
			catch (exception: Throwable)
			{
				socket.close()
				throw exception
			}
		}
	}
}
