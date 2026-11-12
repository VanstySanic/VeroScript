// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.execution.ui.ConsoleView
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.progress.util.ProgressIndicatorBase
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.testFramework.TestApplicationManager
import com.intellij.xdebugger.XDebugSession
import com.intellij.xdebugger.breakpoints.XLineBreakpoint
import com.intellij.xdebugger.impl.breakpoints.XExpressionImpl
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptBreakpointProperties
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptDebugProcess
import com.vanstysanic.veroscript.jetbrains.run.ResolvedRun
import com.vanstysanic.veroscript.jetbrains.run.VeroScriptConfigurationType
import com.vanstysanic.veroscript.jetbrains.run.VeroScriptRunConfiguration
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test
import com.intellij.xdebugger.frame.XSuspendContext
import java.lang.reflect.Proxy
import java.nio.file.Path
import java.util.concurrent.CompletableFuture
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/** Real TCP framing and plugin controller; this host is a protocol fixture, not a VM. */
class DomainDebugTest {
	@Test fun aStopDuringAttachmentPreservesTheDomainPauseAndItsCapabilities() {
		Fixture(enableOperations = true).use { fixture ->
			fixture.host.stopDuringAttach = true
			fixture.start()
			await { fixture.positions.get() == 1 }
			assertEquals("game", fixture.process.domainInfo!!.veroscriptDomainId)
			assertTrue(fixture.process.canEvaluate)
			assertNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator)
			assertFalse(fixture.process.processHandler.isProcessTerminated)
			fixture.process.resume(null)
			await { fixture.resumed.get() > 0 && fixture.host.requests.any { it["command"].asString == "continue" } }
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
		}
	}

	@Test fun persistentDomainRetainsMetadataUpdatesIdleBreakpointsAndSurvivesCalls() {
		Fixture().use { fixture ->
			fixture.start()
			assertEquals("host", fixture.process.domainInfo!!.veroscriptHostId)
			assertEquals("游戏", fixture.process.domainInfo!!.veroscriptTargetName)
			assertEquals("opaque-source", fixture.process.domainInfo!!.veroscriptSources.single().version)
			assertTrue(fixture.output.any { it.contains("source-7") && it.contains("bindings-3") })
			assertFalse(fixture.process.canSetVariable) // initialize advertised true; domain overrides it
			assertFalse(fixture.process.canEvaluate)
			val attach = fixture.host.requests.single { it["command"].asString == "attach" }["arguments"].asJsonObject
			assertEquals(setOf("attachMode", "domainId"), attach.keySet())
			assertEquals("domain", attach["attachMode"].asString)

			fixture.process.breakpointHandlers.single().registerBreakpoint(point(3))
			await { fixture.host.requests.any { it["command"].asString == "setBreakpoints" } && fixture.verified.get() == 1 }
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 1 }
			assertTrue(fixture.process.isCurrent(1))
			assertNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator)
			fixture.process.resume(null)
			await { !fixture.process.isCurrent(1) && fixture.resumed.get() > 0 && fixture.host.requests.any { it["command"].asString == "continue" } }
			assertFalse(fixture.process.processHandler.isProcessTerminated)
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 2 }
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
			fixture.process.stop()
			await { fixture.process.processHandler.isProcessTerminated }
			val disconnect = fixture.host.requests.single { it["command"].asString == "disconnect" }
			assertFalse(disconnect["arguments"].asJsonObject["terminateDebuggee"].asBoolean)
			assertFalse(fixture.host.requests.any { it["command"].asString in setOf("launch", "terminate") })
		}
	}

	@Test fun unsupportedHostsFailBeforeAttachWithoutFallingBackToProgram() {
		for (capability in listOf<Boolean?>(null, false)) Fixture(capability).use { fixture ->
			fixture.start()
			await { fixture.process.processHandler.isProcessTerminated && fixture.errors.isNotEmpty() }
			assertFalse(fixture.host.requests.any { it["command"].asString in setOf("attach", "launch", "configurationDone") })
		}
	}

	@Test fun identityMismatchFailsAndDisconnectsWithoutTermination() {
		Fixture(returnedId = "other").use { fixture ->
			fixture.start()
			await { fixture.process.processHandler.isProcessTerminated && fixture.errors.isNotEmpty() }
			assertFalse(fixture.host.requests.any { it["command"].asString == "configurationDone" })
			assertFalse(fixture.host.requests.single { it["command"].asString == "disconnect" }["arguments"].asJsonObject["terminateDebuggee"].asBoolean)
		}
	}

	@Test fun unsupportedOperationsDoNotCloseTheDomainOrSendRequests() {
		Fixture().use { fixture ->
			fixture.start()
			fixture.process.startPausing()
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 1 }
			fixture.process.startStepInto(null)
			fixture.process.breakpointHandlers.single().registerBreakpoint(point(2, conditional = true))
			await { fixture.invalid.get() > 0 && fixture.errors.size == 2 }
			assertTrue(fixture.process.isCurrent(1))
			assertFalse(fixture.process.processHandler.isProcessTerminated)
			assertFalse(fixture.host.requests.any { it["command"].asString in setOf("pause", "next", "stepIn", "stepOut") })
			await { fixture.host.requests.any { it["command"].asString == "setBreakpoints" } }
			assertEquals(0, fixture.host.requests.last { it["command"].asString == "setBreakpoints" }["arguments"].asJsonObject["breakpoints"].asJsonArray.size())
		}
	}

	@Test fun aRejectedBreakpointUpdateDoesNotPoisonLaterUpdates() {
		Fixture().use { fixture ->
			fixture.start()
			fixture.host.rejectNextBreakpointUpdate = true
			fixture.process.breakpointHandlers.single().registerBreakpoint(point(2))
			await { fixture.errors.isNotEmpty() && fixture.invalid.get() > 0 }
			assertFalse(fixture.process.processHandler.isProcessTerminated)
			fixture.process.breakpointHandlers.single().registerBreakpoint(point(3))
			await { fixture.verified.get() == 2 }
			assertFalse(fixture.process.processHandler.isProcessTerminated)
		}
	}

	@Test fun domainCapabilitiesEnableSupportedOperationsAndConditions() {
		Fixture(enableOperations = true).use { fixture ->
			fixture.start()
			assertTrue(fixture.process.canSetVariable)
			assertTrue(fixture.process.canEvaluate)
			fixture.process.breakpointHandlers.single().registerBreakpoint(point(3, conditional = true))
			await { fixture.verified.get() == 1 }
			val update = fixture.host.requests.single { it["command"].asString == "setBreakpoints" }
			assertEquals("true", update["arguments"].asJsonObject["breakpoints"].asJsonArray.single().asJsonObject["condition"].asString)
			fixture.process.startPausing()
			await { fixture.host.requests.any { it["command"].asString == "pause" } }
			fixture.host.event("stopped", """{"reason":"pause","threadId":1}""")
			await { fixture.positions.get() == 1 }
			assertNotNull(fixture.context!!.activeExecutionStack!!.topFrame!!.evaluator)
			fixture.process.startStepOver(null)
			await { fixture.host.requests.any { it["command"].asString == "next" } && fixture.resumed.get() > 0 }
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
			assertFalse(fixture.process.processHandler.isProcessTerminated)
		}
	}

	@Test fun domainConnectionLossEndsTheIdeSession() {
		Fixture().use { fixture ->
			fixture.start()
			fixture.host.dropConnection()
			await { fixture.process.processHandler.isProcessTerminated && fixture.errors.isNotEmpty() }
			assertEquals(1, fixture.process.processHandler.exitCode)
		}
	}

	@Test fun anExpiredStackFailureDoesNotEndThePersistentDomain() {
		Fixture().use { fixture ->
			fixture.start()
			fixture.host.holdNextStackTrace = true
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.host.requests.any { it["command"].asString == "stackTrace" } }
			fixture.host.event("continued", """{"threadId":1,"allThreadsContinued":true}""")
			await { fixture.resumed.get() > 0 }
			fixture.host.rejectHeldStackTrace()
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 1 }
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
			assertFalse(fixture.process.processHandler.isProcessTerminated)
		}
	}

	@Test fun expiredControlFailuresCannotEndANewerDomainStop() {
		for (command in listOf("continue", "next", "stepIn", "stepOut")) Fixture(enableOperations = true).use { fixture ->
			fixture.start()
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 1 }
			fixture.host.holdNextControl = true
			when (command) {
				"continue" -> fixture.process.resume(null)
				"next" -> fixture.process.startStepOver(null)
				"stepIn" -> fixture.process.startStepInto(null)
				"stepOut" -> fixture.process.startStepOut(null)
			}
			await { fixture.host.isControlHeld }
			fixture.host.event("continued", """{"threadId":1,"allThreadsContinued":true}""")
			await { fixture.resumed.get() > 0 }
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 2 }
			fixture.host.rejectHeldControl()
			// The response barrier also drains the old command's synchronous continuations.
			fixture.process.server.threads().get(5, TimeUnit.SECONDS)
			assertTrue("$command: ${fixture.errors}", fixture.errors.isEmpty())
			assertFalse(command, fixture.process.processHandler.isProcessTerminated)
			fixture.process.resume(null)
			await { fixture.host.requests.count { it["command"].asString in setOf("continue", "next", "stepIn", "stepOut") } == 2 }
		}
	}

	@Test fun anExpiredPauseFailureDoesNotReportAgainstANewerDomainStop() {
		Fixture(enableOperations = true).use { fixture ->
			fixture.start()
			fixture.host.holdNextControl = true
			fixture.process.startPausing()
			await { fixture.host.isControlHeld }
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 1 }
			fixture.host.rejectHeldControl()
			fixture.process.server.threads().get(5, TimeUnit.SECONDS)
			ApplicationManager.getApplication().invokeAndWait {}
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
			assertFalse(fixture.process.processHandler.isProcessTerminated)
			assertTrue(fixture.process.isCurrent(1))
			fixture.process.resume(null)
			await { fixture.host.requests.any { it["command"].asString == "continue" } }
		}
	}

	@Test fun aCurrentControlFailureStillReportsAndEndsTheSession() {
		Fixture().use { fixture ->
			fixture.start()
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 1 }
			fixture.host.holdNextControl = true
			fixture.process.resume(null)
			await { fixture.host.isControlHeld }
			fixture.host.rejectHeldControl()
			await { fixture.errors.isNotEmpty() && fixture.process.processHandler.isProcessTerminated }
			assertEquals(1, fixture.process.processHandler.exitCode)
		}
	}

	@Test fun stoppingDuringInitialBreakpointsDoesNotActivateTheDomain() {
		Fixture().use { fixture ->
			fixture.host.holdNextBreakpointUpdate = true
			fixture.host.holdDisconnect = true
			fixture.process.breakpointHandlers.single().registerBreakpoint(point(3))
			val startup = fixture.startAsync()
			await { fixture.host.isBreakpointUpdateHeld }
			fixture.process.stop()
			await { fixture.host.isDisconnectHeld }
			fixture.host.releaseBreakpointUpdate()
			// The threads response is ordered after the delayed breakpoint response,
			// so all its synchronous controller continuations have run by this point.
			fixture.process.server.threads().get(5, TimeUnit.SECONDS)
			assertFalse(fixture.host.requests.any { it["command"].asString == "configurationDone" })
			fixture.host.releaseDisconnect()
			startup.get(5, TimeUnit.SECONDS)
			await { fixture.process.processHandler.isProcessTerminated }
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
		}
	}

	@Test fun stoppingWithQueuedBreakpointUpdatesDoesNotResumeOrSendLaterUpdates() {
		Fixture().use { fixture ->
			fixture.start()
			fixture.host.event("stopped", """{"reason":"breakpoint","threadId":1}""")
			await { fixture.positions.get() == 1 }
			fixture.host.holdNextBreakpointUpdate = true
			fixture.host.holdDisconnect = true
			val handler = fixture.process.breakpointHandlers.single()
			handler.registerBreakpoint(point(2))
			await { fixture.host.isBreakpointUpdateHeld }
			handler.registerBreakpoint(point(3))
			fixture.process.resume(null)
			fixture.process.stop()
			await { fixture.host.isDisconnectHeld }
			fixture.host.releaseBreakpointUpdate()
			fixture.process.server.threads().get(5, TimeUnit.SECONDS)
			assertEquals(1, fixture.host.requests.count { it["command"].asString == "setBreakpoints" })
			assertFalse(fixture.host.requests.any { it["command"].asString == "continue" })
			fixture.host.releaseDisconnect()
			await { fixture.process.processHandler.isProcessTerminated }
			assertTrue(fixture.errors.toString(), fixture.errors.isEmpty())
		}
	}

	@Test fun domainConfigurationNeedsNoRuntimeAndRejectsExecutionOverrides() {
		val settings = VeroScriptProjectSettings()
		val project = proxy<Project> { name, arguments -> when (name) {
			"getBasePath" -> Path.of(".").toAbsolutePath().toString()
			"getService" -> if (arguments?.firstOrNull() == VeroScriptProjectSettings::class.java) settings
			else error("Domain must not resolve Runtime")
			else -> null
		} }
		val configuration = VeroScriptRunConfiguration(project, VeroScriptConfigurationType().configurationFactories.single(), "Domain")
		configuration.options.apply { mode = "attach"; attachMode = "domain"; domainId = "game" }
		val run = configuration.resolve()
		assertTrue(run.isDomainAttach)
		assertNull(run.runtime)
		assertNull(run.program)
		assertEquals(mapOf("attachMode" to "domain", "domainId" to "game"), run.debugArguments())
		val xml = com.intellij.util.xmlb.XmlSerializer.serialize(configuration.options)
		val copy = com.intellij.util.xmlb.XmlSerializer.deserialize(xml, configuration.options.javaClass)
		assertEquals("domain", copy.attachMode)
		assertEquals("game", copy.domainId)
		val options = configuration.options
		val mutations = listOf<Pair<() -> Unit, () -> Unit>>(
			({ options.program = "missing.vs" }) to { options.program = "" },
			({ options.arguments = "x" }) to { options.arguments = "" },
			({ options.overrideArguments = true }) to { options.overrideArguments = false },
			({ options.workingDirectory = "missing" }) to { options.workingDirectory = "" },
			({ options.environment = """{"A":"B"}""" }) to { options.environment = "{}" },
			({ options.semantics = """{"defines":[]}""" }) to { options.semantics = "{}" },
			({ options.inputFile = "missing" }) to { options.inputFile = "" },
			({ options.stopOnEntry = "true" }) to { options.stopOnEntry = "inherit" },
			({ options.maxCallDepth = 2 }) to { options.maxCallDepth = 0 },
			({ options.inheritParentEnvironment = false }) to { options.inheritParentEnvironment = true },
			({ options.backend = "jit" }) to { options.backend = "mixed" },
			({ options.jitBackendPath = "jit" }) to { options.jitBackendPath = "" })
		for ((change, reset) in mutations) {
			change()
			assertThrows(IllegalArgumentException::class.java) { configuration.resolve() }
			reset()
		}
		assertTrue(configuration.resolve().isDomainAttach)
		options.attachMode = "program"
		assertThrows(IllegalArgumentException::class.java) { configuration.resolve() }
		options.domainId = ""
		assertFalse(configuration.resolve().isDomainAttach)
		options.jitBackendPath = "jit"
		assertThrows(IllegalArgumentException::class.java) { configuration.resolve() }
	}

	private class Fixture(advertise: Boolean? = true, returnedId: String = "game", enableOperations: Boolean = false) : AutoCloseable {
		val host = DomainDapTestHost(advertise, returnedId, enableOperations)
		val errors = CopyOnWriteArrayList<String>()
		val output = CopyOnWriteArrayList<String>()
		val positions = AtomicInteger()
		@Volatile var context: XSuspendContext? = null
		val resumed = AtomicInteger()
		val verified = AtomicInteger()
		val invalid = AtomicInteger()
		private val project = proxy<Project> { name, _ -> when (name) { "isDisposed" -> false;
		else -> null } }
		private val console = proxy<ConsoleView> { name, args -> if (name == "print") { output.add(args!![0].toString()); null }
		else null }
		private val session = proxy<XDebugSession> { name, args -> when (name) {
			"getProject" -> project
			"getConsoleView" -> console
			"positionReached" -> { context = args!![0] as XSuspendContext; positions.incrementAndGet(); null }
			"sessionResumed" -> { resumed.incrementAndGet(); null }
			"setBreakpointVerified" -> { verified.incrementAndGet(); null }
			"setBreakpointInvalid" -> { invalid.incrementAndGet(); null }
			"reportError" -> { errors.add(args!![0].toString()); null }
			else -> null
		} }
		val process = VeroScriptDebugProcess(session, ResolvedRun(null, null, Path.of(".").toAbsolutePath(), emptyList(), emptyMap(),
			SemanticOptions.parse("{}"), "attach", "mixed", null, null, "127.0.0.1", host.port, 0, "en-US", attachMode = "domain", domainId = "game"))
		init { process.processHandler.startNotify() }
		fun startAsync() = CompletableFuture.runAsync { process.connect(ProgressIndicatorBase()) }
		fun start() {
			startAsync().get(15, TimeUnit.SECONDS)
			ApplicationManager.getApplication().invokeAndWait {}
		}
		override fun close() {
			try {
				process.stop()
				await { process.processHandler.isProcessTerminated }
			}
			finally
			{
				host.close()
				Disposer.dispose(project)
			}
		}
	}


	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
		private fun point(line: Int, conditional: Boolean = false): XLineBreakpoint<VeroScriptBreakpointProperties> = proxy { name, _ -> when (name) {
			"getFileUrl" -> "file:///host/main.vs"
			"getLine" -> line
			"getConditionExpression" -> if (conditional) XExpressionImpl.fromText("true")
			else null
			else -> null
		} }
		private fun await(condition: () -> Boolean) {
			val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
			while (!condition() && System.nanoTime() < deadline) {
				ApplicationManager.getApplication().invokeAndWait {}
				Thread.sleep(10)
			}
			assertTrue("Timed out waiting for the domain controller", condition())
		}
		private inline fun <reified T> proxy(noinline call: (String, Array<out Any?>?) -> Any?): T =
			Proxy.newProxyInstance(T::class.java.classLoader, arrayOf(T::class.java)) { instance, method, args ->
				when (method.name) {
					"hashCode" -> System.identityHashCode(instance)
					"equals" -> instance === args?.firstOrNull()
					"toString" -> T::class.java.simpleName + " domain fixture"
					else -> call(method.name, args) ?: when (method.returnType) {
						Boolean::class.javaPrimitiveType -> false
						Int::class.javaPrimitiveType -> 0
						else -> null
					}
				}
			} as T
	}
}
