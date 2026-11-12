// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.execution.ui.ConsoleView
import com.intellij.execution.ui.ConsoleViewContentType
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.editor.EditorFactory
import com.intellij.openapi.progress.util.ProgressIndicatorBase
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.testFramework.TestApplicationManager
import com.intellij.xdebugger.XDebugSession
import com.intellij.xdebugger.breakpoints.XLineBreakpoint
import com.intellij.xdebugger.evaluation.XDebuggerEvaluator
import com.intellij.xdebugger.frame.XSuspendContext
import com.intellij.xdebugger.frame.XValue
import com.intellij.xdebugger.frame.XValueNode
import com.intellij.xdebugger.frame.XValuePlace
import com.intellij.xdebugger.frame.XCompositeNode
import com.intellij.xdebugger.frame.XValueChildrenList
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptBreakpointProperties
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptDebugProcess
import com.vanstysanic.veroscript.jetbrains.run.ResolvedRun
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import org.eclipse.lsp4j.debug.*
import org.eclipse.lsp4j.debug.launch.DSPLauncher
import org.eclipse.lsp4j.debug.services.IDebugProtocolClient
import org.eclipse.lsp4j.debug.services.IDebugProtocolServer
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.lang.reflect.Proxy
import java.net.ServerSocket
import java.net.Socket
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/** Exercises the actual plugin controller and DAP transport without a fake Rider .NET solution. */
class DebugProcessTest {
	@Test fun dapUtf16ColumnsLocateUnopenedPhysicalUnicodeSources() {
		val text = "void helper()\n{\n string text = \"中文😀\"; print(text);\n}\n"
		val sourcePath = Files.createTempFile("vero imported source 空格 ", ".vs")
		try {
			Files.writeString(sourcePath, text)
			val file = requireNotNull(com.intellij.openapi.vfs.LocalFileSystem.getInstance().refreshAndFindFileByNioFile(sourcePath))
			val manager = com.intellij.openapi.fileEditor.FileDocumentManager.getInstance()
			assertNull("The imported source must initially have no cached document", manager.getCachedDocument(file))
			Fixture().use { fixture ->
				fixture.server.frameSource = Source().apply {
					path = sourcePath.toString()
					name = file.name
					sourceReference = 0
				}
				fixture.server.frameLine = 3
				fixture.server.frameColumn = text.lineSequence().elementAt(2).indexOf("print") + 1
				fixture.start().get(10, TimeUnit.SECONDS)
				fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
				await { fixture.positions.get() == 1 }
				val position = com.intellij.openapi.application.ReadAction.computeBlocking<Pair<Int, Int>, RuntimeException> {
					assertNull("Preparing a physical source must not load its document", manager.getCachedDocument(file))
					val source = requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.sourcePosition)
					assertTrue(source.file.isInLocalFileSystem)
					assertTrue("The location must resolve to the same physical source", Files.isSameFile(sourcePath, source.file.toNioPath()))
					assertNull("Creating a physical location must not load its document", manager.getCachedDocument(source.file))
					source.line to source.offset
				}
				assertEquals(2, position.first)
				assertEquals(text.indexOf("print"), position.second)
				assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
			}
		}
		finally
		{
			Files.deleteIfExists(sourcePath)
		}
	}

	@Test fun dapUtf16ColumnsLocateCapturedUnicodeSources() {
		val text = "void main()\r\n{\r\n string text = \"中文😀\"; print(text);\r\n}\r\n"
		Fixture().use { fixture ->
			fixture.server.frameSource = Source().apply {
				path = "/compiled/unicode.vs"
				name = "unicode.vs"
				sourceReference = 81
			}
			fixture.server.frameLine = 3
			fixture.server.frameColumn = text.lineSequence().elementAt(2).indexOf("print") + 1
			fixture.server.source = {
				CompletableFuture.completedFuture(SourceResponse().apply { content = text })
			}
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
			await { fixture.positions.get() == 1 }
			val position = com.intellij.openapi.application.ReadAction.computeBlocking<Triple<Int, Int, Int>, RuntimeException> {
				val source = requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.sourcePosition)
				assertFalse(source.file.isWritable)
				val document = requireNotNull(com.intellij.openapi.fileEditor.FileDocumentManager.getInstance().getDocument(source.file))
				Triple(source.line, source.offset, document.text.indexOf("print"))
			}
			assertEquals(2, position.first)
			assertTrue(position.third > 0)
			assertEquals(position.third, position.second)
		}
	}

	@Test fun nativeCompilerImportsCanStepIntoGeneratedMethods() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME for native compiler-extension debugging", !home.isNullOrBlank())
		val directory = Files.createTempDirectory("vero compiler debug 空格 ")
		val program = directory.resolve("main.vs")
		val extension = directory.resolve("compiler_getter.vs")
		try {
			javaClass.getResourceAsStream("/compiler_getter.vs")!!.use { Files.copy(it, extension) }
			Files.writeString(program, "import compiler \"compiler_getter.vs\";\n@gEtTeR\nclass Box {public:int value=7;};\n" +
				"int main()\n{\n Box box=Box();\n return box.Read();\n}\n")
			val run = ResolvedRun(RuntimeLayout(Path.of(home)), program, directory, emptyList(), emptyMap(),
				SemanticOptions.parse("{}"), "run", "vm", null, false, "127.0.0.1", 4711, 0, "en-US")
			Fixture(run).use { fixture ->
				val breakpoint = point(6, program)
				fixture.handler.registerBreakpoint(breakpoint)
				fixture.start().get(20, TimeUnit.SECONDS)
				await { fixture.positions.get() == 1 }
				var source: com.intellij.openapi.vfs.VirtualFile? = null
				for (step in 0 until 8) {
					val count = fixture.positions.get()
					fixture.process.startStepInto(null)
					await { fixture.positions.get() > count || fixture.process.processHandler.isProcessTerminated }
					source = fixture.context!!.activeExecutionStack!!.topFrame!!.sourcePosition?.file
					if (source != null && !source.isInLocalFileSystem) break
					assertFalse("Generated method must be reached before exit", fixture.process.processHandler.isProcessTerminated)
				}
				assertNotNull("The generated method must have a readable source", source)
				assertFalse(source!!.isInLocalFileSystem)
				assertFalse(source.isWritable)
				val text = com.intellij.openapi.application.ReadAction.computeBlocking<String, RuntimeException> {
					com.intellij.openapi.fileEditor.FileDocumentManager.getInstance().getDocument(source)!!.text
				}
				assertTrue(text, text.contains("Read") && text.contains("return value"))
				fixture.handler.unregisterBreakpoint(breakpoint, false)
				fixture.process.resume(null)
				await { fixture.process.processHandler.isProcessTerminated }
				assertEquals(7, fixture.process.processHandler.exitCode)
				assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
			}
		}
		finally
		{
			Files.deleteIfExists(program)
			Files.deleteIfExists(extension)
			Files.deleteIfExists(directory)
		}
	}

	@Test fun generatedSourcesUseReadOnlySnapshotsAndShareRequestsAcrossStops() {
		Fixture().use { fixture ->
			val requests = AtomicInteger()
			val reply = CompletableFuture<SourceResponse>()
			fixture.server.frameSource = Source().apply {
				name = "generated.vs"; path = "veroscript-generated:/builder/body.vs"; sourceReference = 17
			}
			fixture.server.source = { arguments ->
				assertEquals(17, arguments.sourceReference)
				assertEquals(fixture.server.frameSource.path, arguments.source.path)
				requests.incrementAndGet()
				reply
			}
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "step" })
			await { requests.get() == 1 }
			assertEquals(0, fixture.positions.get())
			reply.complete(SourceResponse().apply { content = "// generated\nint Read(){return 42;}\n" })
			await { fixture.positions.get() == 1 }
			val frame = fixture.context!!.activeExecutionStack!!.topFrame!!
			val position = requireNotNull(frame.sourcePosition)
			assertFalse(position.file.isWritable)
			assertEquals("generated.vs", position.file.name)
			assertEquals("// generated\nint Read(){return 42;}\n",
				com.intellij.openapi.application.ReadAction.computeBlocking<String, RuntimeException> {
					com.intellij.openapi.fileEditor.FileDocumentManager.getInstance().getDocument(position.file)!!.text
				})
			assertFalse(com.vanstysanic.veroscript.jetbrains.debug.VeroScriptLineBreakpointType()
				.canPutAt(position.file, 1, fixture.project))
			val descriptor = com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptLspDescriptor(
				com.intellij.openapi.project.ProjectManager.getInstance().defaultProject, RuntimeLayout(Path.of(".")))
			assertFalse(descriptor.isSupportedFile(position.file))
			fixture.server.client.continued(ContinuedEventArguments().apply { threadId = 1 })
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "step" })
			await { fixture.positions.get() == 2 }
			assertSame(position.file, fixture.context!!.activeExecutionStack!!.topFrame!!.sourcePosition!!.file)
			assertEquals(1, requests.get())
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
		}
	}

	@Test fun failedSourceRequestsKeepTheStopAndCanRecoverOnTheNextPause() {
		Fixture().use { fixture ->
			val requests = AtomicInteger()
			fixture.server.frameSource = Source().apply { name = "missing.vs"; sourceReference = 18 }
			fixture.server.source = {
				if (requests.incrementAndGet() == 1) CompletableFuture.failedFuture(org.eclipse.lsp4j.jsonrpc.ResponseErrorException(
					org.eclipse.lsp4j.jsonrpc.messages.ResponseError(-32603, "source unavailable", null)))
				else CompletableFuture.completedFuture(SourceResponse().apply { content = "int recovered(){return 1;}" })
			}
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "step" })
			await { fixture.positions.get() == 1 && fixture.errors.isNotEmpty() }
			assertNull(fixture.context!!.activeExecutionStack!!.topFrame!!.sourcePosition)
			assertFalse(fixture.process.processHandler.isProcessTerminated)
			assertTrue(fixture.errors.single().contains("source unavailable"))
			fixture.server.client.continued(ContinuedEventArguments().apply { threadId = 1 })
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "step" })
			await { fixture.positions.get() == 2 }
			assertNotNull("A transient source failure must not hide that reference for the whole session",
				fixture.context!!.activeExecutionStack!!.topFrame!!.sourcePosition)
			assertEquals(2, requests.get())
		}
	}

	@Test fun delayedSourcesCannotReopenAResumedOrDisposedStop() {
		Fixture().use { fixture ->
			val requested = AtomicBoolean()
			val reply = CompletableFuture<SourceResponse>()
			fixture.server.frameSource = Source().apply { name = "late.vs"; sourceReference = 19 }
			fixture.server.source = { requested.set(true); reply }
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "step" })
			await { requested.get() }
			fixture.server.client.continued(ContinuedEventArguments().apply { threadId = 1 })
			fixture.process.stop()
			await { fixture.process.processHandler.isProcessTerminated }
			reply.complete(SourceResponse().apply { content = "int late(){return 1;}" })
			ApplicationManager.getApplication().invokeAndWait {}
			assertEquals(0, fixture.positions.get())
			assertNull(fixture.process.sources.find(fixture.server.frameSource))
		}
	}

	@Test fun sourceReferencesOverrideLocalFilesAndPagedFramesLoadTheirSnapshots() {
		val local = Files.createTempFile("vero source 空格 ", ".vs")
		try {
			Files.writeString(local, "int local(){return 0;}")
			Fixture().use { fixture ->
				fixture.server.frameSource = Source().apply { path = local.toString(); sourceReference = 21 }
				fixture.server.source = { arguments -> CompletableFuture.completedFuture(SourceResponse().apply {
					content = "int generated${arguments.sourceReference}(){return 42;}"
				}) }
				fixture.start().get(10, TimeUnit.SECONDS)
				fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "step" })
				await { fixture.positions.get() == 1 }
				val stack = fixture.context!!.activeExecutionStack!!
				val initial = requireNotNull(stack.topFrame!!.sourcePosition).file
				assertFalse(initial.isInLocalFileSystem)
				fixture.server.frameSource = Source().apply { name = "paged.vs"; sourceReference = 22 }
				val page = CompletableFuture<com.intellij.xdebugger.frame.XStackFrame>()
				stack.computeStackFrames(100, proxy<com.intellij.xdebugger.frame.XExecutionStack.XStackFrameContainer> { method, arguments ->
					if (method == "addStackFrames") page.complete((arguments!![0] as List<*>).first() as com.intellij.xdebugger.frame.XStackFrame)
					null
				})
				await { page.isDone }
				val paged = requireNotNull(page.get().sourcePosition).file
				assertEquals("paged.vs", paged.name)
				assertNotSame(initial, paged)
				assertFalse(paged.isWritable)
			}
		}
		finally
		{
			Files.deleteIfExists(local)
		}
	}

	@Test fun variableExpansionHandlesUnpagedAndPagedAdaptersWithoutDuplicateChildren() {
		for (paged in listOf(false, true)) for (count in listOf(0, 99, 100, 101, 250)) Fixture().use { fixture ->
			val requests = CopyOnWriteArrayList<VariablesArguments>()
			fixture.server.evaluation = EvaluateResponse().apply {
				result = "array<int>"; variablesReference = 12
				if (paged) indexedVariables = count
			}
			fixture.server.variables = { arguments ->
				requests.add(arguments)
				val start = if (paged) arguments.start ?: 0
				else 0
				val end = if (paged) (start + (arguments.count ?: count)).coerceAtMost(count)
				else count
				CompletableFuture.completedFuture(VariablesResponse().apply {
					variables = (start until end).map { index -> Variable().apply {
						name = "[$index]"; value = index.toString(); variablesReference = 0
					} }.toTypedArray()
				})
			}
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
			await { fixture.positions.get() == 1 }
			val value = evaluate(requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator), "items")
			val pages = expand(value)
			assertEquals("Every child must appear once: paged=$paged count=$count",
				(0 until count).map { "[$it]" }, pages.flatten())
			assertTrue("UI pages must be bounded", pages.all { it.size <= 100 })
			if (!paged) {
				assertEquals("Unpaged adapters must be queried once", 1, requests.size)
				assertNull(requests.single().start)
				assertNull(requests.single().count)
			}
			else assertEquals((count + 99) / 100, requests.size)
		}
	}

	@Test fun readOnlyPresentationHintsSuppressEditingWithoutDisablingWritableChildren() {
		for (supportsWrites in listOf(false, true)) Fixture().use { fixture ->
			fixture.server.supportsSetVariable = supportsWrites
			fixture.server.evaluation = EvaluateResponse().apply { result = "Item"; variablesReference = 12 }
			fixture.server.variables = {
				CompletableFuture.completedFuture(VariablesResponse().apply {
					variables = arrayOf(
						Variable().apply {
							name = "inspection"; value = "7"; variablesReference = 0
							presentationHint = VariablePresentationHint().apply { attributes = arrayOf("readOnly") }
						},
						Variable().apply { name = "writable"; value = "8"; variablesReference = 0 },
						Variable().apply {
							name = "staticValue"; value = "9"; variablesReference = 0
							presentationHint = VariablePresentationHint().apply { attributes = arrayOf("static") }
						}
					)
				})
			}
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
			await { fixture.positions.get() == 1 }
			val value = evaluate(requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator), "item")
			val result = CompletableFuture<XValueChildrenList>()
			value.computeChildren(proxy<XCompositeNode> { method, arguments ->
				when (method) {
					"addChildren" -> result.complete(arguments!![0] as XValueChildrenList)
					"setErrorMessage" -> result.completeExceptionally(AssertionError(arguments!![0].toString()))
				}
				null
			})
			await { result.isDone }
			val children = result.get()
			assertEquals(3, children.size())
			assertNull(children.getValue(0).modifier)
			assertEquals(supportsWrites, children.getValue(1).modifier != null)
			assertEquals(supportsWrites, children.getValue(2).modifier != null)
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
		}
	}

	@Test fun nativeHundredElementVariableExpansionEndsWithoutRequestingAnotherPage() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native controller integration", !home.isNullOrBlank())
		val directory = Files.createTempDirectory("vero variables 空格 ")
		val program = directory.resolve("测试.vs")
		try {
			Files.writeString(program, "int main()\n{\n array<int> values = {" + (0 until 100).joinToString(",") + "};\n return 0;\n}\n")
			val run = ResolvedRun(RuntimeLayout(Path.of(home)), program, directory, emptyList(), emptyMap(),
				SemanticOptions.parse("{}"), "run", "vm", null, false, "127.0.0.1", 4711, 0, "en-US")
			Fixture(run).use { fixture ->
				fixture.handler.registerBreakpoint(point(3, program))
				fixture.start().get(15, TimeUnit.SECONDS)
				await { fixture.positions.get() == 1 }
				val value = evaluate(requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator), "values")
				assertEquals(listOf((0 until 100).map { "[$it]" }), expand(value))
			}
		}
		finally
		{
			Files.deleteIfExists(program)
			Files.deleteIfExists(directory)
		}
	}

	@Test fun pagedVariableExpansionEndsOnAShortPageEvenIfTheCountIsStale() {
		Fixture().use { fixture ->
			fixture.server.evaluation = EvaluateResponse().apply {
				result = "array<int>"; variablesReference = 12; indexedVariables = 250
			}
			val requests = AtomicInteger()
			fixture.server.variables = {
				requests.incrementAndGet()
				CompletableFuture.completedFuture(VariablesResponse().apply {
					variables = (0 until 5).map { index -> Variable().apply {
						name = "[$index]"; value = index.toString(); variablesReference = 0
					} }.toTypedArray()
				})
			}
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
			await { fixture.positions.get() == 1 }
			val value = evaluate(requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator), "items")
			assertEquals(listOf((0 until 5).map { "[$it]" }), expand(value))
			assertEquals(1, requests.get())
		}
	}

	@Test fun cachedVariablePagesAreDiscardedWhenExecutionResumes() {
		Fixture().use { fixture ->
			fixture.server.evaluation = EvaluateResponse().apply { result = "array<int>"; variablesReference = 12 }
			fixture.server.variables = {
				CompletableFuture.completedFuture(VariablesResponse().apply {
					variables = (0 until 250).map { index -> Variable().apply {
						name = "[$index]"; value = index.toString(); variablesReference = 0
					} }.toTypedArray()
				})
			}
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
			await { fixture.positions.get() == 1 }
			val value = evaluate(requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator), "items")
			val rows = AtomicInteger()
			val next = java.util.concurrent.atomic.AtomicReference<Runnable?>()
			value.computeChildren(proxy<XCompositeNode> { method, arguments ->
				when (method) {
					"addChildren" -> rows.addAndGet((arguments!![0] as XValueChildrenList).size())
					"tooManyChildren" -> next.set(arguments?.getOrNull(1) as? Runnable)
				}
				null
			})
			await { next.get() != null }
			assertEquals(100, rows.get())
			fixture.server.client.continued(ContinuedEventArguments().apply { threadId = 1 })
			ApplicationManager.getApplication().invokeAndWait { next.get()!!.run() }
			ApplicationManager.getApplication().invokeAndWait {}
			assertEquals("An old page must not repopulate the resumed tree", 100, rows.get())
		}
	}

	@Test fun startupDrainsLateBreakpointsAndStepCommandsAreNotDuplicated() {
		Fixture().use { fixture ->
			val first = CompletableFuture<SetBreakpointsResponse>()
			val received = CompletableFuture<SetBreakpointsArguments>()
			val requests = CopyOnWriteArrayList<SetBreakpointsArguments>()
			fixture.server.breakpoints = { arguments ->
				requests.add(arguments)
				if (requests.size == 1) {
					received.complete(arguments)
					first
				}
				else CompletableFuture.completedFuture(response(arguments))
			}
			val removed = point(0)
			fixture.handler.registerBreakpoint(removed)
			val startup = fixture.start()
			val initial = received.get(10, TimeUnit.SECONDS)
			fixture.handler.unregisterBreakpoint(removed, false)
			val added = point(1)
			fixture.handler.registerBreakpoint(added)
			first.complete(response(initial))
			startup.get(10, TimeUnit.SECONDS)
			assertEquals(2, requests.size)
			assertEquals(listOf(2), requests.last().breakpoints.map { it.line })
			assertEquals(1, fixture.server.configured.get())
			await { fixture.verified.contains(added) }
			assertFalse(fixture.verified.contains(removed))

			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
			await { fixture.positions.get() == 1 }
			fixture.process.startStepOver(null)
			fixture.process.startStepOver(null)
			await { fixture.server.steps.get() >= 1 }
			fixture.server.stepReply.complete(null)
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "step" })
			await { fixture.positions.get() == 2 }
			assertEquals(1, fixture.server.steps.get())
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
		}
	}

	@Test fun aLatePauseFailureCannotEndANewerBreakpointStop() {
		Fixture().use { fixture ->
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.process.startPausing()
			await { fixture.server.pauses.get() == 1 }
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
			await { fixture.positions.get() == 1 }
			fixture.server.pauseReply.completeExceptionally(IllegalStateException("Target already stopped"))
			// This ordered response drains the earlier pause response and its controller callbacks.
			fixture.process.server.threads().get(5, TimeUnit.SECONDS)
			ApplicationManager.getApplication().invokeAndWait {}
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
			assertFalse(fixture.process.processHandler.isProcessTerminated)
			assertTrue(fixture.process.isCurrent(1))
			fixture.process.startStepOver(null)
			await { fixture.server.steps.get() == 1 }
			fixture.server.stepReply.complete(null)
		}
	}

	@Test fun aPauseRejectionBeforeTheStopEventReportsAndPreservesTheProgramSession() {
		Fixture().use { fixture ->
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.process.startPausing()
			await { fixture.server.pauses.get() == 1 }
			fixture.server.pauseReply.completeExceptionally(org.eclipse.lsp4j.jsonrpc.ResponseErrorException(
				org.eclipse.lsp4j.jsonrpc.messages.ResponseError(-32603, "Unable to pause target", null)))
			await { fixture.errors.isNotEmpty() }
			assertTrue(fixture.errors.toString(), fixture.errors.single().contains("Unable to pause target"))
			fixture.process.server.threads().get(5, TimeUnit.SECONDS)
			assertFalse(fixture.process.processHandler.isProcessTerminated)
			// The target may already be stopped while its notification is still in flight.
			fixture.server.client.stopped(StoppedEventArguments().apply { threadId = 1; reason = "breakpoint" })
			await { fixture.positions.get() == 1 }
			assertTrue(fixture.process.isCurrent(1))
			fixture.process.startStepOver(null)
			await { fixture.server.steps.get() == 1 }
			fixture.server.stepReply.complete(null)
		}
	}

	@Test fun unexpectedEofReportsFailureAndTerminatesTheSession() {
		Fixture().use { fixture ->
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.accepted.get(10, TimeUnit.SECONDS).close()
			await { fixture.process.processHandler.isProcessTerminated && fixture.errors.isNotEmpty() }
			assertEquals(1, fixture.process.processHandler.exitCode)
		}
	}

	@Test fun cancellationBeforeConnectionCleansUpWithoutReportingProtocolFailure() {
		Fixture().use { fixture ->
			val indicator = ProgressIndicatorBase().apply { cancel() }
			fixture.process.connect(indicator)
			await { fixture.process.processHandler.isProcessTerminated }
			assertEquals(0, fixture.process.processHandler.exitCode)
			assertTrue(fixture.errors.isEmpty())
			assertFalse(fixture.accepted.isDone)
		}
	}

	@Test fun normalTerminationPreservesTheProgramExitCode() {
		for (code in listOf(0, 7)) Fixture().use { fixture ->
			fixture.start().get(10, TimeUnit.SECONDS)
			fixture.server.client.exited(ExitedEventArguments().apply { exitCode = code })
			fixture.server.client.terminated(TerminatedEventArguments())
			await { fixture.process.processHandler.isProcessTerminated && fixture.output.isNotEmpty() }
			assertEquals(code, fixture.process.processHandler.exitCode)
			assertSame(if (code == 0) ConsoleViewContentType.SYSTEM_OUTPUT
				else ConsoleViewContentType.ERROR_OUTPUT, fixture.output.single().second)
			assertTrue(fixture.errors.isEmpty())
		}
	}

	@Test fun nativeLaunchReachesABreakpointStepsAndPreservesTheProgramExitCode() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native controller integration", !home.isNullOrBlank())
		val directory = Files.createTempDirectory("vero controller 空格 ")
		val program = directory.resolve("测试.vs")
		try {
			Files.writeString(program, "int main()\n{\n int value = 7;\n value += 1;\n return value;\n}\n")
			val run = ResolvedRun(RuntimeLayout(Path.of(home)), program, directory, emptyList(), emptyMap(),
				SemanticOptions.parse("{}"), "run", "jit", null, false, "127.0.0.1", 4711, 0, "en-US",
				jitBackendPath = directory.resolve("absent JIT module"))
			Fixture(run).use { fixture ->
				val breakpoint = point(3, program)
				fixture.handler.registerBreakpoint(breakpoint)
				fixture.start().get(15, TimeUnit.SECONDS)
				await { fixture.positions.get() == 1 }
				await { fixture.verified.contains(breakpoint) }
				val evaluator = requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator)
				val document = EditorFactory.getInstance().createDocument(Files.readString(program))
				val offset = document.text.indexOf("value +=")
				val hover = requireNotNull(evaluator.getExpressionInfoAtOffset(fixture.project, document, offset, false))
				fun evaluateValue(currentEvaluator: XDebuggerEvaluator, expression: String): String {
					val evaluated = CompletableFuture<XValue>()
					currentEvaluator.evaluate(expression, object : XDebuggerEvaluator.XEvaluationCallback {
						override fun evaluated(result: XValue) { evaluated.complete(result) }
						override fun errorOccurred(errorMessage: String) { evaluated.completeExceptionally(AssertionError(errorMessage)) }
					}, null)
					await { evaluated.isDone }
					val presentation = CompletableFuture<String>()
					evaluated.get().computePresentation(proxy<XValueNode> { name, arguments ->
						if (name == "setPresentation") presentation.complete(arguments!![2] as String)
						null
					}, XValuePlace.TOOLTIP)
					return presentation.get(5, TimeUnit.SECONDS)
				}
				assertEquals("7", evaluateValue(evaluator, hover.textRange.substring(document.text)))
				fixture.process.startStepOver(null)
				await { fixture.positions.get() == 2 }
				assertNull(evaluator.getExpressionInfoAtOffset(fixture.project, document, offset, false))
				val nextEvaluator = requireNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator)
				assertEquals("8", evaluateValue(nextEvaluator, "value"))
				fixture.process.resume(null)
				await { fixture.process.processHandler.isProcessTerminated }
				assertEquals(8, fixture.process.processHandler.exitCode)
				assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
			}
		}
		finally
		{
			Files.deleteIfExists(program)
			Files.deleteIfExists(directory)
		}
	}

	private class Server : IDebugProtocolServer {
		lateinit var client: IDebugProtocolClient
		var frameSource = Source()
		var frameLine = 1
		var frameColumn = 1
		var source: (SourceArguments) -> CompletableFuture<SourceResponse> = {
			CompletableFuture.completedFuture(SourceResponse().apply { content = "" })
		}
		var evaluation = EvaluateResponse().apply { result = "0"; variablesReference = 0 }
		var supportsSetVariable = false
		var variables: (VariablesArguments) -> CompletableFuture<VariablesResponse> = {
			CompletableFuture.completedFuture(VariablesResponse().apply { variables = emptyArray() })
		}
		var breakpoints: (SetBreakpointsArguments) -> CompletableFuture<SetBreakpointsResponse> = {
			CompletableFuture.completedFuture(response(it))
		}
		val configured = AtomicInteger()
		val steps = AtomicInteger()
		val stepReply = CompletableFuture<Void>()
		val pauses = AtomicInteger()
		val pauseReply = CompletableFuture<Void>()
		override fun initialize(arguments: InitializeRequestArguments): CompletableFuture<Capabilities> {
			client.initialized()
			return CompletableFuture.completedFuture(Capabilities().apply {
				supportsSetVariable = this@Server.supportsSetVariable
			})
		}
		override fun attach(arguments: Map<String, Any>) = CompletableFuture.completedFuture<Void>(null)
		override fun configurationDone(arguments: ConfigurationDoneArguments): CompletableFuture<Void> {
			configured.incrementAndGet()
			return CompletableFuture.completedFuture(null)
		}
		override fun setBreakpoints(arguments: SetBreakpointsArguments) = breakpoints(arguments)
		override fun stackTrace(arguments: StackTraceArguments) = CompletableFuture.completedFuture(StackTraceResponse().apply {
			stackFrames = arrayOf(StackFrame().apply { id = 1; name = "main"; line = frameLine; column = frameColumn; source = frameSource })
			totalFrames = 1
		})
		override fun next(arguments: NextArguments): CompletableFuture<Void> {
			steps.incrementAndGet()
			return stepReply
		}
		override fun pause(arguments: PauseArguments): CompletableFuture<Void> {
			pauses.incrementAndGet()
			return pauseReply
		}
		override fun threads() = CompletableFuture.completedFuture(ThreadsResponse().apply { threads = emptyArray() })
		override fun evaluate(arguments: EvaluateArguments) = CompletableFuture.completedFuture(evaluation)
		override fun variables(arguments: VariablesArguments) = variables.invoke(arguments)
		override fun source(arguments: SourceArguments) = source.invoke(arguments)
		override fun disconnect(arguments: DisconnectArguments) = CompletableFuture.completedFuture<Void>(null)
	}

	private class Fixture(nativeRun: ResolvedRun? = null) : AutoCloseable {
		val server = Server()
		private val listener = if (nativeRun == null) ServerSocket(0)
		else null
		private val io = Executors.newCachedThreadPool { task ->
			java.lang.Thread(task, "VeroScript controller test").apply { isDaemon = true }
		}
		val accepted = CompletableFuture<Socket>()
		val errors = CopyOnWriteArrayList<String>()
		val output = CopyOnWriteArrayList<Pair<String, ConsoleViewContentType>>()
		val positions = AtomicInteger()
		@Volatile var context: XSuspendContext? = null
		val verified = CopyOnWriteArrayList<Any>()
		private val disposed = AtomicBoolean()
		val project = proxy<Project> { name, _ -> when (name) {
			"isDisposed" -> disposed.get()
			"dispose" -> { disposed.set(true); null }
			"getName" -> "VeroScript controller test"
			else -> null
		} }
		private val console = proxy<ConsoleView> { name, arguments ->
			if (name == "print") output.add(arguments!![0] as String to arguments[1] as ConsoleViewContentType)
			null
		}
		private val session = proxy<XDebugSession> { name, arguments -> when (name) {
			"getProject" -> project
			"getConsoleView" -> console
			"positionReached" -> { context = arguments!![0] as XSuspendContext; positions.incrementAndGet(); null }
			"setBreakpointVerified" -> { verified.add(arguments!![0]!!); null }
			"reportError", "reportMessage" -> { errors.add(arguments!![0].toString()); null }
			else -> null
		} }
		private val run = nativeRun ?: ResolvedRun(null, null, Path.of(".").toAbsolutePath(), emptyList(), emptyMap(),
			SemanticOptions.parse("{}"), "attach", "vm", null, null, "127.0.0.1", requireNotNull(listener).localPort, 0, "en-US")
		val process = VeroScriptDebugProcess(session, run)
		val handler = process.breakpointHandlers.single()

		init {
			process.processHandler.startNotify()
			if (listener != null) io.execute {
				try {
					val socket = listener.accept()
					accepted.complete(socket)
					val launcher = DSPLauncher.createServerLauncher(server, socket.getInputStream(), socket.getOutputStream(), io) { it }
					server.client = launcher.remoteProxy
					launcher.startListening()
				}
				catch (exception: java.io.IOException)
				{
					if (!listener.isClosed) accepted.completeExceptionally(exception)
				}
			}
		}

		fun start() = CompletableFuture.runAsync({ process.connect(ProgressIndicatorBase()) }, io)

		override fun close() {
			try {
				val childExit = process.processExit
				process.stop()
				await { process.processHandler.isProcessTerminated }
				// A finished IDE session can precede the adapter's bounded asynchronous exit.
				childExit?.get(10, TimeUnit.SECONDS)
			}
			finally
			{
				listener?.close()
				accepted.getNow(null)?.close()
				io.shutdownNow()
				Disposer.dispose(project)
			}
		}
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }

		private fun evaluate(evaluator: XDebuggerEvaluator, expression: String): XValue {
			val result = CompletableFuture<XValue>()
			evaluator.evaluate(expression, object : XDebuggerEvaluator.XEvaluationCallback {
				override fun evaluated(value: XValue) { result.complete(value) }
				override fun errorOccurred(errorMessage: String) { result.completeExceptionally(AssertionError(errorMessage)) }
			}, null)
			await { result.isDone }
			return result.get()
		}

		private fun expand(value: XValue): List<List<String>> {
			val pages = CopyOnWriteArrayList<Pair<List<String>, Boolean>>()
			val next = java.util.concurrent.atomic.AtomicReference<Runnable?>()
			val error = java.util.concurrent.atomic.AtomicReference<String?>()
			val node = proxy<XCompositeNode> { method, arguments ->
				when (method) {
					"addChildren" -> {
						val children = arguments!![0] as XValueChildrenList
						pages.add((0 until children.size()).map(children::getName) to arguments[1] as Boolean)
					}
					"tooManyChildren" -> next.set(arguments?.getOrNull(1) as? Runnable)
					"setErrorMessage" -> error.set(arguments!![0] as String)
				}
				null
			}
			value.computeChildren(node)
			var delivered = 0
			while (true) {
				await { pages.size > delivered || error.get() != null }
				assertNull(error.get())
				assertTrue("Expansion must terminate without duplicate pages", pages.size <= 4)
				delivered = pages.size
				if (pages.last().second) return pages.map { it.first }
				await { next.get() != null }
				ApplicationManager.getApplication().invokeAndWait { next.getAndSet(null)!!.run() }
			}
		}

		private fun point(line: Int, path: Path = Path.of("controller.vs")): XLineBreakpoint<VeroScriptBreakpointProperties> = proxy { name, _ -> when (name) {
			"getFileUrl" -> VfsUtilCore.pathToUrl(path.toAbsolutePath().toString().replace('\\', '/'))
			"getLine" -> line
			else -> null
		} }

		private fun response(arguments: SetBreakpointsArguments) = SetBreakpointsResponse().apply {
			breakpoints = arguments.breakpoints.mapIndexed { index, source -> Breakpoint().apply {
				id = index + 1
				line = source.line
				isVerified = true
			} }.toTypedArray()
		}

		private fun await(condition: () -> Boolean) {
			val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
			while (!condition() && System.nanoTime() < deadline) {
				ApplicationManager.getApplication().invokeAndWait {}
				java.lang.Thread.sleep(10)
			}
			assertTrue("Timed out waiting for the debug controller", condition())
		}

		private inline fun <reified T> proxy(noinline call: (String, Array<out Any?>?) -> Any?): T =
			Proxy.newProxyInstance(T::class.java.classLoader, arrayOf(T::class.java)) { instance, method, arguments ->
				when (method.name) {
					"hashCode" -> System.identityHashCode(instance)
					"equals" -> instance === arguments?.get(0)
					"toString" -> T::class.java.simpleName + " test proxy"
					else -> call(method.name, arguments) ?: when (method.returnType) {
						Boolean::class.javaPrimitiveType -> false
						Int::class.javaPrimitiveType -> 0
						Long::class.javaPrimitiveType -> 0L
						else -> null
					}
				}
			} as T
	}
}
