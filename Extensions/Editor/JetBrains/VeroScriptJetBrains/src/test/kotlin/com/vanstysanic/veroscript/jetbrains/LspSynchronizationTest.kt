// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.execution.process.ProcessEvent
import com.intellij.execution.process.ProcessListener
import com.intellij.execution.process.ProcessOutputType
import com.intellij.openapi.project.Project
import com.intellij.openapi.project.ProjectManager
import com.intellij.openapi.util.Key
import com.intellij.platform.lsp.api.LspClientDescriptor
import com.intellij.testFramework.TestApplicationManager
import com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptLspDescriptor
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import org.eclipse.lsp4j.*
import org.eclipse.lsp4j.launch.LSPLauncher
import org.eclipse.lsp4j.services.LanguageClient
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.BeforeClass
import org.junit.Test
import java.io.OutputStreamWriter
import java.io.PipedInputStream
import java.io.PipedOutputStream
import java.lang.reflect.Proxy
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CompletableFuture
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/** Exercises Rider's process-handler bridge rather than launching a raw ProcessBuilder pipe. */
class LspSynchronizationTest {
	@Test fun sdkTransportClearsIncompleteFunctionDiagnosticsAfterCodeActions() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME for the SDK process transport", !home.isNullOrBlank())
		val directory = Files.createTempDirectory("vero Rider LSP 空格 ")
		val file = directory.resolve("test.vs")
		val project = project(directory)
		val descriptor: LspClientDescriptor = VeroScriptLspDescriptor(project, RuntimeLayout(Path.of(home)))
		val handler = descriptor.startServerProcess()
		val output = PipedOutputStream()
		val input = PipedInputStream(output)
		val writer = OutputStreamWriter(output, Charsets.UTF_8)
		val reports = LinkedBlockingQueue<PublishDiagnosticsParams>()
		handler.addProcessListener(object : ProcessListener {
			override fun onTextAvailable(event: ProcessEvent, outputType: Key<*>) {
				if (ProcessOutputType.isStdout(outputType)) {
					writer.write(event.text)
					writer.flush()
				}
			}
		})
		val client = object : LanguageClient {
			override fun telemetryEvent(value: Any?) {}
			override fun publishDiagnostics(params: PublishDiagnosticsParams) { reports.offer(params) }
			override fun showMessage(params: MessageParams) {}
			override fun logMessage(params: MessageParams) {}
			override fun showMessageRequest(params: ShowMessageRequestParams) = CompletableFuture.completedFuture<MessageActionItem>(null)
			override fun registerCapability(params: RegistrationParams) = CompletableFuture.completedFuture<Void>(null)
			override fun configuration(params: ConfigurationParams): CompletableFuture<List<Any>> =
				CompletableFuture.completedFuture(params.items.map { emptyMap<String, Any>() })
		}
		val launcher = LSPLauncher.createClientLauncher(client, input, requireNotNull(handler.processInput))
		val listener = launcher.startListening()
		handler.startNotify()
		val server = launcher.remoteProxy
		try {
			server.initialize(descriptor.createInitializeParams()).get(10, TimeUnit.SECONDS)
			server.initialized(InitializedParams())
			val uri = file.toUri().toString()
			val identifier = TextDocumentIdentifier(uri)
			val prefix = "// 😀 Unicode and a large formatted result exercise UTF-8 chunk boundaries.\n" +
				"// " + "formatted data ".repeat(1200) + "\npublic static class A final { const int a=100; }\n"
			val suffix = "\nint main(){print(A::a);}\n"
			var version = 1
			var source = prefix + suffix
			Files.writeString(file, source)
			server.textDocumentService.didOpen(DidOpenTextDocumentParams(TextDocumentItem(uri, "veroscript", version, source)))
			for (body in listOf("void t", "void t(", "void t()", "void t(){", "void t(){}", "void t()\n{\n}")) {
				source = prefix + body + suffix
				server.textDocumentService.didChange(DidChangeTextDocumentParams(VersionedTextDocumentIdentifier(uri, ++version),
					listOf(TextDocumentContentChangeEvent(source))))
				val actions = server.textDocumentService.codeAction(CodeActionParams(identifier,
					Range(Position(3, 0), Position(3, 0)), CodeActionContext(emptyList())))
				val tokens = server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier))
				assertNotNull(actions.get(10, TimeUnit.SECONDS))
				assertNotNull(tokens.get(10, TimeUnit.SECONDS))
				if (body.endsWith('}')) {
					val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
					while (true) {
						val report = reports.poll(maxOf(0, deadline - System.nanoTime()), TimeUnit.NANOSECONDS)
						assertNotNull("Missing diagnostics for completed function version $version", report)
						if (report.version == version) {
							assertTrue(report.toString(), report.diagnostics.none { it.severity == DiagnosticSeverity.Error })
							break
						}
					}
				}
			}
			// Rider cancels obsolete requests while typing; completed frames must still be consumed.
			repeat(24) { step ->
				source = prefix + (if (step % 2 == 0) "void t"
				else "void t(){}") + suffix
				server.textDocumentService.didChange(DidChangeTextDocumentParams(VersionedTextDocumentIdentifier(uri, ++version),
					listOf(TextDocumentContentChangeEvent(source))))
				server.textDocumentService.codeAction(CodeActionParams(identifier,
					Range(Position(3, 0), Position(3, 0)), CodeActionContext(emptyList()))).cancel(true)
				server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).cancel(true)
			}
			assertNotNull(server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS))
			while (true) {
				val report = reports.poll(10, TimeUnit.SECONDS)
				assertNotNull("Missing diagnostics after cancelled requests", report)
				if (report.version == version) {
					assertTrue(report.toString(), report.diagnostics.none { it.severity == DiagnosticSeverity.Error })
					break
				}
			}
			server.shutdown().get(5, TimeUnit.SECONDS)
			server.exit()
			assertTrue(handler.waitFor(5000))
			assertEquals(0, handler.exitCode)
		}
		finally
		{
			if (!handler.isProcessTerminated) handler.destroyProcess()
			handler.waitFor(5000)
			listener.cancel(true)
			writer.close()
			input.close()
			Files.deleteIfExists(file)
			Files.deleteIfExists(directory)
		}
	}

	private fun project(directory: Path): Project {
		val delegate = ProjectManager.getInstance().defaultProject
		return Proxy.newProxyInstance(Project::class.java.classLoader, arrayOf(Project::class.java)) { _, method, arguments ->
			when (method.name) {
				"getBasePath" -> directory.toString()
				else -> method.invoke(delegate, *(arguments ?: emptyArray()))
			}
		} as Project
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
	}
}
