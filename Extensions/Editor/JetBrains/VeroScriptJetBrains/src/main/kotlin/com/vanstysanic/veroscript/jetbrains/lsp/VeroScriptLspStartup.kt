// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.lsp

import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.Disposable
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.Service
import com.intellij.openapi.project.Project
import com.intellij.platform.lsp.api.LspIntegrationProvider
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import java.util.concurrent.atomic.AtomicLong

/** Discovery and version probing never run on the editor thread. */
@Service(Service.Level.PROJECT)
class VeroScriptLspStartup(private val project: Project) : Disposable {
	private val epoch = AtomicLong()
	private val running = AtomicLong()
	@Volatile private var readyRuntime: RuntimeLayout? = null

	fun request(starter: LspIntegrationProvider.LspClientStarter) {
		val existing = readyRuntime
		if (existing != null) {
			starter.ensureClientStarted(VeroScriptLspDescriptor(project, existing))
			return
		}
		val ticket = epoch.get() + 1
		if (!running.compareAndSet(0, ticket)) return
		ApplicationManager.getApplication().executeOnPooledThread {
			val result = runCatching {
				val runtime = RuntimeLayout.resolve(project)
				val version = runtime.validate()
				if (!project.isDisposed && epoch.get() + 1 == ticket) {
					project.getService(VeroScriptDependencyWatcher::class.java).refresh(runtime)
				}
				runtime to version
			}
			ApplicationManager.getApplication().invokeLater {
				running.compareAndSet(ticket, 0)
				if (project.isDisposed || epoch.get() + 1 != ticket || !VeroScriptProjectSettings.get(project).state.enabled) return@invokeLater
				result.fold({ (runtime, version) ->
					readyRuntime = runtime
					if (!version.startsWith("VeroScript 0.1.1 ")) {
						NotificationGroupManager.getInstance().getNotificationGroup("VeroScript")
							.createNotification("VeroScript", com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message("runtime.baseline", version.trim()),
								NotificationType.WARNING).notify(project)
					}
					// The callback starter only collects descriptors during fileOpened.
					com.intellij.platform.lsp.api.LspClientManager.getInstance(project)
						.ensureClientStarted(VeroScriptLspProvider::class.java, VeroScriptLspDescriptor(project, runtime))
				}, { project.getService(VeroScriptLspStatus::class.java).configurationError(it as? Exception ?: RuntimeException(it)) })
			}
		}
	}

	fun invalidate() {
		epoch.incrementAndGet()
		readyRuntime = null
		running.set(0)
	}
	override fun dispose() { invalidate() }
}
