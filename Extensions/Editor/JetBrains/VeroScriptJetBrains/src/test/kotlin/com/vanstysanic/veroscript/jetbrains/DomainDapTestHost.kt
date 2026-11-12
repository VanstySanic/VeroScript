// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/** TCP protocol fixture only; it deliberately does not execute a VM. */
internal class DomainDapTestHost(
	private val advertise: Boolean?, private val domainId: String, private val enableOperations: Boolean = false
) : AutoCloseable {
	private val listener = ServerSocket(0)
	val port = listener.localPort
	val requests = CopyOnWriteArrayList<JsonObject>()
	@Volatile var rejectNextBreakpointUpdate = false
	@Volatile var fragmentResponses = false
	@Volatile var stopDuringAttach = false
	@Volatile var holdNextStackTrace = false
	@Volatile private var heldStackTrace: JsonObject? = null
	@Volatile var holdNextControl = false
	@Volatile private var heldControl: JsonObject? = null
	val isControlHeld: Boolean get() = heldControl != null
	@Volatile var holdNextBreakpointUpdate = false
	@Volatile var holdDisconnect = false
	@Volatile private var heldBreakpointUpdate: JsonObject? = null
	@Volatile private var heldDisconnect: JsonObject? = null
	val isBreakpointUpdateHeld: Boolean get() = heldBreakpointUpdate != null
	val isDisconnectHeld: Boolean get() = heldDisconnect != null
	private val sequence = AtomicInteger(1)
	private val accepted = CompletableFuture<Socket>()
	private val io = Executors.newSingleThreadExecutor { Thread(it, "VeroScript domain fixture").apply { isDaemon = true } }
	init { io.execute {
		try {
			val socket = listener.accept()
			accepted.complete(socket)
			val input = socket.getInputStream().buffered()
			while (!socket.isClosed) {
				val header = StringBuilder()
				while (!header.endsWith("\r\n\r\n")) {
					val byte = input.read()
					if (byte < 0) return@execute
					header.append(byte.toChar())
				}
				val length = Regex("Content-Length: (\\d+)").find(header)!!.groupValues[1].toInt()
				val request = JsonParser.parseString(String(input.readNBytes(length), Charsets.UTF_8)).asJsonObject
				requests.add(request)
				reply(request)
			}
		}
		catch (_: java.io.IOException)
		{
		}
	} }
	private fun reply(request: JsonObject) {
		val command = request["command"].asString
		if (command == "setBreakpoints" && holdNextBreakpointUpdate) {
			holdNextBreakpointUpdate = false
			heldBreakpointUpdate = request
			return
		}
		if (command == "disconnect" && holdDisconnect) {
			holdDisconnect = false
			heldDisconnect = request
			return
		}
		if (command == "stackTrace" && holdNextStackTrace) {
			holdNextStackTrace = false
			heldStackTrace = request
			return
		}
		if (command in setOf("continue", "next", "stepIn", "stepOut", "pause") && holdNextControl) {
			holdNextControl = false
			heldControl = request
			return
		}
		val reject = command == "setBreakpoints" && rejectNextBreakpointUpdate
		if (reject) rejectNextBreakpointUpdate = false
		if (command == "attach" && stopDuringAttach) {
			stopDuringAttach = false
			event("stopped", """{"reason":"breakpoint","threadId":1}""")
		}
		val body = when (command) {
			"initialize" -> """{"supportsConfigurationDoneRequest":true,"supportsSetVariable":true${advertise?.let { ",\"veroscriptSupportsDomainAttach\":$it" }.orEmpty()}}"""
			"attach" -> """{"veroscriptHostId":"host","veroscriptDomainId":"$domainId","veroscriptTargetName":"游戏","veroscriptSourceRevision":"source-7","veroscriptBindingRevision":"bindings-3","veroscriptSources":[{"path":"/host/main.vs","version":"opaque-source"}],"veroscriptCanPause":$enableOperations,"veroscriptCanStep":$enableOperations,"veroscriptCanEvaluate":$enableOperations,"veroscriptCanSetVariable":$enableOperations,"veroscriptCanUseConditionalBreakpoints":$enableOperations}"""
			"setBreakpoints" -> {
				val points = request["arguments"].asJsonObject["breakpoints"].asJsonArray
				"{\"breakpoints\":[" + points.mapIndexed { index, point -> """{"id":${index + 1},"verified":true,"line":${point.asJsonObject["line"]}}""" }.joinToString(",") + "]}"
			}
			"stackTrace" -> """{"stackFrames":[{"id":1,"name":"host callback","line":4,"column":1}],"totalFrames":1}"""
			"continue" -> """{"allThreadsContinued":true}"""
			else -> "{}"
		}
		send(JsonObject().apply {
			addProperty("type", "response"); addProperty("request_seq", request["seq"].asInt)
			addProperty("command", command); addProperty("success", !reject); add("body", JsonParser.parseString(body))
			if (reject) addProperty("message", "Host rejected breakpoint update")
		})
		if (command == "initialize") event("initialized", "{}")
		if (command == "continue") event("continued", """{"threadId":1,"allThreadsContinued":true}""")
	}
	fun event(name: String, body: String) = send(JsonObject().apply {
		addProperty("type", "event"); addProperty("event", name); add("body", JsonParser.parseString(body))
	})
	fun dropConnection() { accepted.get(10, TimeUnit.SECONDS).close() }
	fun releaseBreakpointUpdate() {
		val request = requireNotNull(heldBreakpointUpdate)
		heldBreakpointUpdate = null
		reply(request)
	}
	fun releaseDisconnect() {
		val request = requireNotNull(heldDisconnect)
		heldDisconnect = null
		reply(request)
	}
	fun rejectHeldStackTrace() {
		val request = requireNotNull(heldStackTrace)
		send(JsonObject().apply {
			addProperty("type", "response")
			addProperty("request_seq", request["seq"].asInt)
			addProperty("command", "stackTrace")
			addProperty("success", false)
			addProperty("message", "The host call has ended")
		})
		heldStackTrace = null
	}
	fun rejectHeldControl() {
		val request = requireNotNull(heldControl)
		send(JsonObject().apply {
			addProperty("type", "response")
			addProperty("request_seq", request["seq"].asInt)
			addProperty("command", request["command"].asString)
			addProperty("success", false)
			addProperty("message", "The host call has ended")
		})
		heldControl = null
	}
	@Synchronized private fun send(message: JsonObject) {
		message.addProperty("seq", sequence.getAndIncrement())
		val body = message.toString().toByteArray(Charsets.UTF_8)
		accepted.get(10, TimeUnit.SECONDS).getOutputStream().apply {
			val header = "Content-Length: ${body.size}\r\n\r\n".toByteArray(Charsets.US_ASCII)
			if (fragmentResponses) {
				for (byte in header + body) { write(byte.toInt()); flush() }
			}
			else
			{
				write(header); write(body); flush()
			}
		}
	}
	override fun close() {
		listener.close(); accepted.getNow(null)?.close(); io.shutdownNow()
	}
}
