// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.lsp

import com.intellij.openapi.Disposable
import com.intellij.openapi.components.Service
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VirtualFileManager
import com.intellij.openapi.vfs.newvfs.BulkFileListener
import com.intellij.openapi.vfs.newvfs.events.VFileEvent
import com.intellij.openapi.vfs.newvfs.events.VFileMoveEvent
import com.intellij.openapi.vfs.newvfs.events.VFilePropertyChangeEvent
import com.intellij.platform.lsp.api.LspClientManager
import com.intellij.util.Alarm
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import org.eclipse.lsp4j.DidChangeWatchedFilesParams
import org.eclipse.lsp4j.FileChangeType
import org.eclipse.lsp4j.FileEvent
import java.nio.file.Files
import java.nio.file.Path

/** Complements SDK source watchers with catalog and external dependency changes. */
@Service(Service.Level.PROJECT)
class VeroScriptDependencyWatcher(private val project: Project) : Disposable {
	private val alarm = Alarm(Alarm.ThreadToUse.POOLED_THREAD, this)
	private val pending = linkedSetOf<String>()
	private var watched = emptySet<LocalFileSystem.WatchRequest>()
	@Volatile private var disposed = false
	@Volatile private var catalogs = emptyList<Path>()
	@Volatile private var externalRoots = emptyList<Path>()

	init {
		project.messageBus.connect(this).subscribe(VirtualFileManager.VFS_CHANGES, object : BulkFileListener {
			override fun after(events: List<VFileEvent>) {
				if (disposed || project.isDisposed) return
				val root = project.basePath?.let(Path::of) ?: return
				val relevant = events.flatMap { event ->
					if (event.fileSystem !is LocalFileSystem) return@flatMap emptyList()
					val paths = mutableListOf(Path.of(event.path))
					if (event is VFileMoveEvent) paths.add(Path.of(event.oldParent.path, event.file.name))
					if (event is VFilePropertyChangeEvent && event.propertyName == "name") {
						event.file.parent?.let { paths.add(Path.of(it.path, event.oldValue.toString())) }
					}
					paths.filter { path ->
						catalogs.any { path.startsWith(it) || it.startsWith(path) } ||
							(!path.startsWith(root) && externalRoots.any { dependency ->
								dependency.startsWith(path) || path.startsWith(dependency) &&
									(path.toString().endsWithAnySourceExtension() || event.file?.isDirectory == true)
							})
					}
				}
				if (relevant.isEmpty()) return
				synchronized(pending) { relevant.forEach { pending.add(it.toUri().toString()) } }
				alarm.cancelAllRequests()
				alarm.addRequest({ flush() }, 250)
			}
		})
	}

	@Synchronized
	fun refresh(runtime: RuntimeLayout) {
		if (disposed || project.isDisposed) return
		val root = project.basePath?.let(Path::of) ?: return
		val settings = SemanticOptions.resolve(VeroScriptProjectSettings.get(project).configuration(), root)
		if (!settings.has("stdlibPath") && !settings.has("stdlibSearchPaths")) settings.addProperty("stdlibPath", runtime.stdlib.toString())
		catalogs = settings.getAsJsonArray("bindingCatalogPaths")?.map { Path.of(it.asString) }.orEmpty()
		externalRoots = (SemanticOptions.paths.flatMap { name ->
			settings.getAsJsonArray(name)?.map { Path.of(it.asString) }.orEmpty()
		} + listOfNotNull(settings["stdlibPath"]?.asString?.let(Path::of))).filter { !it.startsWith(root) }
		val paths = (catalogs + externalRoots).map { if (Files.isDirectory(it)) it
		else it.parent }
			.filterNotNull().map { it.toString() }.distinct()
		watched = LocalFileSystem.getInstance().replaceWatchedRoots(watched, paths, emptyList())
	}

	private fun flush() {
		if (disposed || project.isDisposed) return
		val uris = synchronized(pending) { pending.toList().also { pending.clear() } }
		if (uris.isEmpty()) return
		val params = DidChangeWatchedFilesParams(uris.map { FileEvent(it, FileChangeType.Changed) })
		LspClientManager.getInstance(project).getClients(VeroScriptLspProvider::class.java).forEach { client ->
			client.sendNotification { it.workspaceService.didChangeWatchedFiles(params) }
		}
	}

	@Synchronized
	override fun dispose() {
		// A pending discovery task can outlive this service while its project is still disposing.
		disposed = true
		LocalFileSystem.getInstance().removeWatchedRoots(watched)
		watched = emptySet()
		catalogs = emptyList()
		externalRoots = emptyList()
		synchronized(pending) { pending.clear() }
	}
	private fun String.endsWithAnySourceExtension() = endsWith(".vs", true) || endsWith(".vslib", true)
}
