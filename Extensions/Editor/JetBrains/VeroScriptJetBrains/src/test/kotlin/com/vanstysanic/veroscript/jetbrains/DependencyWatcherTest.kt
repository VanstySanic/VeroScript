// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.project.Project
import com.intellij.openapi.util.Disposer
import com.intellij.testFramework.TestApplicationManager
import com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptDependencyWatcher
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test
import java.lang.reflect.Proxy
import java.nio.file.Files
import java.util.concurrent.atomic.AtomicInteger

class DependencyWatcherTest {
	@Test fun lateRefreshCannotReacquireWatchRootsAfterServiceDisposal() {
		val directory = Files.createTempDirectory("vero watcher ")
		val projectRoot = Files.createDirectory(directory.resolve("project"))
		val runtimeRoot = Files.createDirectory(directory.resolve("runtime"))
		val stdlib = Files.createDirectory(runtimeRoot.resolve("stdlib"))
		val reads = AtomicInteger()
		val settings = VeroScriptProjectSettings()
		val project = Proxy.newProxyInstance(Project::class.java.classLoader, arrayOf(Project::class.java)) { instance, method, arguments ->
			when (method.name) {
				"hashCode" -> System.identityHashCode(instance)
				"equals" -> instance === arguments?.get(0)
				"toString", "getName" -> "VeroScript watcher test"
				// Services are disposed before the project necessarily reports isDisposed=true.
				"isDisposed" -> false
				"getMessageBus" -> ApplicationManager.getApplication().messageBus
				"getBasePath" -> { reads.incrementAndGet(); projectRoot.toString() }
				"getService" -> if (arguments?.get(0) == VeroScriptProjectSettings::class.java) settings
				else null
				else -> null
			}
		} as Project
		val watcher = VeroScriptDependencyWatcher(project)
		try {
			watcher.refresh(RuntimeLayout(runtimeRoot))
			assertTrue(reads.get() > 0)
			Disposer.dispose(watcher)
			val before = reads.get()
			// Simulate discovery completing after the watcher has already released its resources.
			watcher.refresh(RuntimeLayout(runtimeRoot))
			assertEquals("A disposed watcher must ignore late background refreshes", before, reads.get())
		}
		finally
		{
			Disposer.dispose(watcher)
			Files.deleteIfExists(stdlib)
			Files.deleteIfExists(runtimeRoot)
			Files.deleteIfExists(projectRoot)
			Files.deleteIfExists(directory)
		}
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
	}
}
