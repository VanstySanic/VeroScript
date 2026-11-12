// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.vanstysanic.veroscript.jetbrains.debug.DapConnection
import org.eclipse.lsp4j.debug.*
import org.eclipse.lsp4j.debug.services.IDebugProtocolClient
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ExecutionException
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

class DapRuntimeTest {
	@Test fun tcpAttachInheritsHostProgramAndDefines() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native protocol integration", !home.isNullOrBlank())
		val executable = Path.of(home, if (System.getProperty("os.name").startsWith("Windows")) "veroscript.exe"
		else "veroscript")
		val directory = Files.createTempDirectory("vero attach ")
		val program = directory.resolve("host.vs")
		Files.writeString(program, "void main()\n{\n int value = HOST_VALUE;\n print(value);\n}\n")
		val port = java.net.ServerSocket(0).use { it.localPort }
		val host = ProcessBuilder(executable.toString(), "--debug", "--dap-port=$port", "-D", "HOST_VALUE=73", program.toString())
			.directory(directory.toFile()).redirectError(ProcessBuilder.Redirect.INHERIT).start()
		val initialized = CompletableFuture<Void>()
		val stopped = CompletableFuture<StoppedEventArguments>()
		val client = object : IDebugProtocolClient {
			override fun initialized() { initialized.complete(null) }
			override fun stopped(arguments: StoppedEventArguments) { stopped.complete(arguments) }
		}
		var connection: DapConnection? = null
		try {
			val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
			while (connection == null && System.nanoTime() < deadline && host.isAlive) {
				try { connection = DapConnection.attach("127.0.0.1", port, client, {}, {}) }
				catch (exception: java.net.ConnectException)
				{
					java.lang.Thread.sleep(50)
				}
			}
			val server = requireNotNull(connection) { "TCP host did not start" }.server
			server.initialize(InitializeRequestArguments().apply { adapterID = "veroscript"; linesStartAt1 = true; columnsStartAt1 = true })
				.get(10, TimeUnit.SECONDS)
			server.attach(emptyMap()).get(10, TimeUnit.SECONDS)
			initialized.get(10, TimeUnit.SECONDS)
			server.setBreakpoints(SetBreakpointsArguments().apply {
				source = Source().apply { path = program.toString() }
				breakpoints = arrayOf(SourceBreakpoint().apply { line = 4 })
			}).get(10, TimeUnit.SECONDS)
			server.configurationDone(ConfigurationDoneArguments()).get(10, TimeUnit.SECONDS)
			stopped.get(10, TimeUnit.SECONDS)
			val frame = server.stackTrace(StackTraceArguments().apply { threadId = 1 }).get(10, TimeUnit.SECONDS).stackFrames[0]
			val value = server.evaluate(EvaluateArguments().apply { expression = "value"; frameId = frame.id }).get(10, TimeUnit.SECONDS)
			assertEquals("73", value.result)
			connection.disconnect(false).get(5, TimeUnit.SECONDS)
			connection.close()
			assertTrue(host.waitFor(5, TimeUnit.SECONDS))
			assertEquals(0, host.exitValue())
		}
		finally
		{
			connection?.close()
			host.destroyForcibly()
			assertTrue("TCP host did not exit during cleanup", host.waitFor(5, TimeUnit.SECONDS))
			Files.deleteIfExists(program)
			Files.deleteIfExists(directory)
		}
	}

	@Test fun launchBreakpointsInt64VariablesEvaluationMutationAndStepping() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native protocol integration", !home.isNullOrBlank())
		val executable = Path.of(home, if (System.getProperty("os.name").startsWith("Windows")) "veroscript-dap.exe"
		else "veroscript-dap")
		assertTrue(Files.isRegularFile(executable))
		val directory = Files.createTempDirectory("vero dap 空格 ")
		val program = directory.resolve("测试.vs")
		Files.writeString(program, "struct Payload { int value = 7; }; void main()\n{\n int64 value = 9223372036854775806i64; array<int> values = {1,2}; variant box = Payload(); auto bytes = new std::memory::buffer(8); auto view = bytes.readonly_slice(0,4);\n value += 1;\n print(value); print(\"[PARENT:\" + std::environment::getenv(\"VEROSCRIPT_TEST_RUNTIME\") + \"]\"); print(\"[EXPLICIT:\" + std::environment::getenv(\"VERO_TEST_EXPLICIT\") + \"]\");\n}\n")
		val initialized = CompletableFuture<Void>()
		val stops = LinkedBlockingQueue<StoppedEventArguments>()
		val terminated = CompletableFuture<Void>()
		val output = StringBuffer()
		val errors = StringBuffer()
		val client = object : IDebugProtocolClient {
			override fun initialized() { initialized.complete(null) }
			override fun stopped(args: StoppedEventArguments) { stops.add(args) }
			override fun output(args: OutputEventArguments) { output.append(args.output) }
			override fun terminated(args: TerminatedEventArguments?) { terminated.complete(null) }
		}
		val connection = DapConnection.launch(listOf(executable.toString()), directory, client,
			{ error -> if (error != null) terminated.completeExceptionally(error) }, { errors.append(it) },
			inheritParentEnvironment = false)
		try {
			val server = connection.server
			val capabilities = server.initialize(InitializeRequestArguments().apply {
				adapterID = "veroscript"; pathFormat = "path"; linesStartAt1 = true; columnsStartAt1 = true
			}).get(10, TimeUnit.SECONDS)
			assertEquals(true, capabilities.supportsSetVariable)
			server.launch(mapOf("program" to program.toString(), "cwd" to directory.toString(), "backend" to "vm",
				"env" to mapOf("VERO_TEST_EXPLICIT" to "two words中文")))
				.get(10, TimeUnit.SECONDS)
			initialized.get(10, TimeUnit.SECONDS)
			val breakpoint = server.setBreakpoints(SetBreakpointsArguments().apply {
				source = Source().apply { path = program.toString() }
				breakpoints = arrayOf(SourceBreakpoint().apply { line = 4; condition = "value > 0" })
			}).get(10, TimeUnit.SECONDS)
			assertEquals(1, breakpoint.breakpoints.size)
			server.configurationDone(ConfigurationDoneArguments()).get(10, TimeUnit.SECONDS)
			assertNotNull("Breakpoint stop missing: $errors", stops.poll(10, TimeUnit.SECONDS))
			val frames = server.stackTrace(StackTraceArguments().apply { threadId = 1 }).get(10, TimeUnit.SECONDS).stackFrames
			assertTrue(frames.isNotEmpty())
			assertEquals(4, frames[0].line)
			val evaluation = server.evaluate(EvaluateArguments().apply { expression = "value"; frameId = frames[0].id; context = "watch" })
				.get(10, TimeUnit.SECONDS)
			assertEquals("9223372036854775806", evaluation.result)
			val scopes = server.scopes(ScopesArguments().apply { frameId = frames[0].id }).get(10, TimeUnit.SECONDS).scopes
			val locals = scopes.first { it.name.contains("local", true) }
			val variables = server.variables(VariablesArguments().apply { variablesReference = locals.variablesReference; start = 0; count = 100 })
				.get(10, TimeUnit.SECONDS).variables
			assertEquals("9223372036854775806", variables.first { it.name == "value" }.value)
			val firstArray = variables.first { it.name == "values" }.variablesReference
			assertTrue(firstArray > 0)
			fun children(reference: Int): Array<Variable> = server.variables(VariablesArguments().apply {
				variablesReference = reference; start = 0; count = 100
			}).get(10, TimeUnit.SECONDS).variables
			val firstMemory = variables.first { it.name == "bytes" }.variablesReference
			val firstView = variables.first { it.name == "view" }.variablesReference
			fun inspectVariant(reference: Int) {
				val entries = children(reference)
				assertEquals(setOf("\$present", "\$type", "\$value"), entries.map { it.name }.toSet())
				assertTrue(entries.all { "readOnly" in it.presentationHint?.attributes.orEmpty() })
				val payload = entries.first { it.name == "\$value" }
				assertTrue(payload.variablesReference > 0)
				val fields = children(payload.variablesReference)
				assertEquals(listOf("value"), fields.map { it.name })
				assertEquals("7", fields.single().value)
				assertTrue("readOnly" in fields.single().presentationHint?.attributes.orEmpty())
				val failure = assertThrows(ExecutionException::class.java) {
					server.setVariable(SetVariableArguments().apply {
						variablesReference = payload.variablesReference; name = "value"; value = "9"
					}).get(10, TimeUnit.SECONDS)
				}
				assertTrue(failure.cause is org.eclipse.lsp4j.jsonrpc.ResponseErrorException)
				assertEquals("7", children(payload.variablesReference).single().value)
			}
			inspectVariant(variables.first { it.name == "box" }.variablesReference)
			inspectVariant(server.evaluate(EvaluateArguments().apply {
				expression = "box"; frameId = frames[0].id; context = "watch"
			}).get(10, TimeUnit.SECONDS).variablesReference)
			assertFalse("Read-only children must not disable mutable locals",
				"readOnly" in variables.first { it.name == "value" }.presentationHint?.attributes.orEmpty())
			assertTrue(firstMemory > 0)
			assertTrue(firstView > 0)
			val memoryChildren = children(firstMemory)
			assertEquals("8", memoryChildren.first { it.name == "[byte_length]" }.value)
			val objectId = memoryChildren.first { it.name == "[object_id]" }.value
			assertTrue(objectId.toULong() > 0uL)
			assertTrue(memoryChildren.all { it.variablesReference == 0 && it.memoryReference == null })
			assertEquals("4", children(firstView).first { it.name == "[byte_length]" }.value)
			assertTrue(children(firstView).none { it.name == "[object_id]" })
			val watch = server.evaluate(EvaluateArguments().apply { expression = "bytes"; frameId = frames[0].id; context = "watch" })
				.get(10, TimeUnit.SECONDS)
			assertEquals(objectId, children(watch.variablesReference).first { it.name == "[object_id]" }.value)
			val readOnlyError = assertThrows(ExecutionException::class.java) {
				server.setVariable(SetVariableArguments().apply {
					variablesReference = firstMemory; name = "[byte_length]"; value = "0"
				}).get(10, TimeUnit.SECONDS)
			}
			assertTrue(readOnlyError.cause is org.eclipse.lsp4j.jsonrpc.ResponseErrorException)
			val mutation = server.setVariable(SetVariableArguments().apply { variablesReference = locals.variablesReference; name = "value"; value = "7" })
				.get(10, TimeUnit.SECONDS)
			assertEquals("7", mutation.value)
			server.next(NextArguments().apply { threadId = 1 }).get(10, TimeUnit.SECONDS)
			assertNotNull(stops.poll(10, TimeUnit.SECONDS))
			// Frame IDs and variable references are opaque identities, not stack indices.
			val nextFrame = server.stackTrace(StackTraceArguments().apply { threadId = 1 })
				.get(10, TimeUnit.SECONDS).stackFrames[0]
			assertNotEquals(frames[0].id, nextFrame.id)
			val nextLocals = server.scopes(ScopesArguments().apply { frameId = nextFrame.id })
				.get(10, TimeUnit.SECONDS).scopes.first { it.name.contains("local", true) }
			assertNotEquals(locals.variablesReference, nextLocals.variablesReference)
			fun expectExpired(request: CompletableFuture<*>) {
				val error = assertThrows(ExecutionException::class.java) { request.get(10, TimeUnit.SECONDS) }
				assertTrue("Expired handles must return a protocol error", error.cause is org.eclipse.lsp4j.jsonrpc.ResponseErrorException)
			}
			expectExpired(server.scopes(ScopesArguments().apply { frameId = frames[0].id }))
			expectExpired(server.evaluate(EvaluateArguments().apply { expression = "value"; frameId = frames[0].id }))
			expectExpired(server.variables(VariablesArguments().apply { variablesReference = locals.variablesReference }))
			expectExpired(server.variables(VariablesArguments().apply { variablesReference = firstArray }))
			expectExpired(server.variables(VariablesArguments().apply { variablesReference = firstMemory }))
			expectExpired(server.variables(VariablesArguments().apply { variablesReference = firstView }))
			expectExpired(server.variables(VariablesArguments().apply { variablesReference = watch.variablesReference }))
			val nextMemory = children(nextLocals.variablesReference).first { it.name == "bytes" }.variablesReference
			assertNotEquals(firstMemory, nextMemory)
			assertEquals(objectId, children(nextMemory).first { it.name == "[object_id]" }.value)
			assertEquals("8", server.evaluate(EvaluateArguments().apply { expression = "value"; frameId = nextFrame.id })
				.get(10, TimeUnit.SECONDS).result)
			assertEquals("8", server.evaluate(EvaluateArguments().apply { expression = "value" })
				.get(10, TimeUnit.SECONDS).result)
			fun arrayReference(): Int = server.variables(VariablesArguments().apply {
				variablesReference = nextLocals.variablesReference
			}).get(10, TimeUnit.SECONDS).variables.first { it.name == "values" }.variablesReference
			val nextArray = arrayReference()
			assertNotEquals(firstArray, nextArray)
			assertEquals("Repeated expansion must retain the current child reference", nextArray, arrayReference())
			server.setVariable(SetVariableArguments().apply { variablesReference = nextArray; name = "[0]"; value = "9" })
				.get(10, TimeUnit.SECONDS)
			expectExpired(server.variables(VariablesArguments().apply { variablesReference = nextArray }))
			val refreshedArray = arrayReference()
			assertNotEquals(nextArray, refreshedArray)
			assertEquals("9", server.variables(VariablesArguments().apply { variablesReference = refreshedArray })
				.get(10, TimeUnit.SECONDS).variables.first { it.name == "[0]" }.value)
			server.continue_(ContinueArguments().apply { threadId = 1 }).get(10, TimeUnit.SECONDS)
			terminated.get(10, TimeUnit.SECONDS)
			assertTrue("Output: $output; stderr: $errors", output.toString().contains("8"))
			assertTrue(output.toString(), output.toString().contains("[PARENT:]"))
			assertTrue(output.toString(), output.toString().contains("[EXPLICIT:two words中文]"))
		}
		finally
		{
			runCatching { connection.disconnect(true).get(4, TimeUnit.SECONDS) }
			connection.close()
			requireNotNull(connection.processExit).get(5, TimeUnit.SECONDS)
			Files.deleteIfExists(program)
			Files.deleteIfExists(directory)
		}
	}
}
