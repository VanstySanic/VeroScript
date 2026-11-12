// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.execution.configurations.RunProfileState
import com.intellij.execution.executors.DefaultDebugExecutor
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.execution.runners.ExecutionEnvironmentBuilder
import com.intellij.execution.ui.ExecutionConsole
import com.intellij.execution.ui.RunContentDescriptor
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.testFramework.TestApplicationManager
import com.intellij.xdebugger.*
import com.intellij.xdebugger.breakpoints.XBreakpointManager
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptDebugRunner
import com.vanstysanic.veroscript.jetbrains.run.VeroScriptConfigurationType
import com.vanstysanic.veroscript.jetbrains.run.VeroScriptRunConfiguration
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptLocalSettings
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test
import java.lang.reflect.Proxy
import java.nio.file.Files
import javax.swing.JPanel

/** Covers the runner-to-XDebugger boundary, before the DAP controller is created. */
class DebugRunnerTest {
	@Test fun debugRunnerNamesItsVisibleSessionFromTheRunConfiguration() {
		val root = Files.createTempDirectory("vero runner ")
		val program = Files.writeString(root.resolve("main.vs"), "void main() {}")
		val local = VeroScriptLocalSettings().apply { state.runtimeHome = root.toString() }
		val settings = VeroScriptProjectSettings()
		val calls = linkedMapOf<String, Any?>()
		val descriptor = RunContentDescriptor(null, null, JPanel(), "Vero runner test")
		val result = proxy<XSessionStartedResult> { _, _ -> descriptor }
		val builder = Proxy.newProxyInstance(XDebugSessionBuilder::class.java.classLoader,
			arrayOf(XDebugSessionBuilder::class.java)) { instance, method, arguments ->
			if (method.name == "startSession") {
				assertEquals("Debug test 中文", calls["sessionName"])
				assertEquals(true, calls["showTab"])
				result
			}
			else
			{
				calls[method.name] = arguments?.firstOrNull()
				instance
			}
		} as XDebugSessionBuilder
		val manager = Manager(builder)
		val project = proxy<Project> { name, arguments -> when (name) {
			"getBasePath" -> root.toString()
			"getName" -> "Vero runner test"
			"isDisposed", "isDefault" -> false
			"getService" -> when (arguments?.firstOrNull()) {
				VeroScriptLocalSettings::class.java -> local
				VeroScriptProjectSettings::class.java -> settings
				XDebuggerManager::class.java -> manager
				else -> null
			}
			else -> null
		} }
		try {
			val configuration = VeroScriptRunConfiguration(project,
				VeroScriptConfigurationType().configurationFactories.single(), "Debug test 中文")
			configuration.options.program = program.toString()
			// Even malformed unused JIT paths must not block VM-only debugging.
			configuration.options.jitBackendPath = "unused\u0000backend"
			configuration.checkConfiguration()
			val runner = VeroScriptDebugRunner()
			val environment = ExecutionEnvironmentBuilder(project, DefaultDebugExecutor.getDebugExecutorInstance())
				.runner(runner).runProfile(configuration).build()
			val state = RunProfileState { _, _ -> null }
			val execute = VeroScriptDebugRunner::class.java.getDeclaredMethod("doExecute", RunProfileState::class.java, ExecutionEnvironment::class.java)
				.apply { isAccessible = true }
			ApplicationManager.getApplication().invokeAndWait {
				assertSame(descriptor, execute.invoke(runner, state, environment))
			}
			assertSame(environment, calls["environment"])
		}
		finally
		{
			Disposer.dispose(descriptor)
			Disposer.dispose(project)
			Files.deleteIfExists(program)
			Files.deleteIfExists(root)
		}
	}

	// The SDK retains abstract legacy methods, although this fixture only uses the builder API.
	@Suppress("OVERRIDE_DEPRECATION")
	private class Manager(private val builder: XDebugSessionBuilder) : XDebuggerManager() {
		override fun newSessionBuilder(starter: XDebugProcessStarter) = builder
		override fun getBreakpointManager(): XBreakpointManager = error("Not used by the runner")
		override fun getDebugSessions() = emptyArray<XDebugSession>()
		override fun getDebugSession(console: ExecutionConsole): XDebugSession? = null
		override fun <T : XDebugProcess> getDebugProcesses(type: Class<T>): List<T> = emptyList()
		override fun getCurrentSession(): XDebugSession? = null
		override fun startSession(environment: ExecutionEnvironment, starter: XDebugProcessStarter): XDebugSession = error("Use the builder")
		override fun startSessionAndShowTab(name: String, content: RunContentDescriptor?, starter: XDebugProcessStarter): XDebugSession = error("Use the builder")
		override fun startSessionAndShowTab(name: String, starter: XDebugProcessStarter, environment: ExecutionEnvironment): XDebugSession = error("Use the builder")
		override fun startSessionAndShowTab(name: String, content: RunContentDescriptor?, suspendOnly: Boolean, starter: XDebugProcessStarter): XDebugSession = error("Use the builder")
		override fun startSessionAndShowTab(name: String, icon: javax.swing.Icon?, content: RunContentDescriptor?, suspendOnly: Boolean, starter: XDebugProcessStarter): XDebugSession = error("Use the builder")
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }

		private inline fun <reified T> proxy(noinline call: (String, Array<out Any?>?) -> Any?): T =
			Proxy.newProxyInstance(T::class.java.classLoader, arrayOf(T::class.java)) { instance, method, arguments ->
				when (method.name) {
					"hashCode" -> System.identityHashCode(instance)
					"equals" -> instance === arguments?.firstOrNull()
					"toString" -> T::class.java.simpleName + " test proxy"
					else -> call(method.name, arguments)
				}
			} as T
	}
}
