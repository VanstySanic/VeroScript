// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.testFramework.LightVirtualFile
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import org.eclipse.lsp4j.debug.Source
import org.eclipse.lsp4j.debug.SourceArguments
import org.eclipse.lsp4j.debug.StackTraceResponse
import org.eclipse.lsp4j.debug.services.IDebugProtocolServer
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

/** Source references identify immutable adapter snapshots within one debug connection. */
internal class VeroScriptDebugSources : AutoCloseable {
	private val requests = ConcurrentHashMap<Int, CompletableFuture<VirtualFile?>>()
	@Volatile private var closed = false

	fun prepare(server: IDebugProtocolServer, frames: StackTraceResponse,
		onFailure: (String, Throwable) -> Unit): CompletableFuture<StackTraceResponse> {
		if (closed) return CompletableFuture.completedFuture(frames)
		val sources = frames.stackFrames.mapNotNull { it.source }.filter { (it.sourceReference ?: 0) > 0 }
			.distinctBy { it.sourceReference }
		val pending = sources.map { source ->
			requests.computeIfAbsent(source.sourceReference) { reference ->
				val request = try {
					server.source(SourceArguments().apply { this.source = source; sourceReference = reference })
				}
				catch (failure: Exception)
				{
					CompletableFuture.failedFuture(failure)
				}
				request.orTimeout(10, TimeUnit.SECONDS).handle { response, failure ->
					if (closed) null
					else if (failure != null)
					{
						onFailure(source.name ?: source.path ?: reference.toString(), failure)
						null
					}
					else
					{
						val name = source.name?.takeIf { it.isNotBlank() }
							?: source.path?.substringAfterLast('/')?.substringAfterLast('\\')?.takeIf { it.isNotBlank() }
							?: "source-$reference.vs"
						VeroScriptDebugSourceFile(name, response.content)
					}
				}
			}.also { cached ->
				// A transient error must not poison this reference for the rest of the connection.
				cached.thenAccept { file -> if (file == null) requests.remove(source.sourceReference, cached) }
			}
		}
		return CompletableFuture.allOf(*pending.toTypedArray()).thenApply { frames }
	}

	fun find(source: Source?): VirtualFile? {
		if (closed || source == null) return null
		val reference = source.sourceReference ?: 0
		// A positive reference is authoritative, even if the adapter also supplies a local path.
		if (reference > 0) return requests[reference]?.getNow(null)
		val path = source.path ?: return null
		return LocalFileSystem.getInstance().findFileByPath(com.intellij.openapi.util.io.FileUtil.toSystemIndependentName(path))
	}

	override fun close() {
		closed = true
		requests.clear()
	}
}

/** Inspection snapshots are not editable project documents or breakpoint targets. */
internal class VeroScriptDebugSourceFile(name: String, text: String) : LightVirtualFile(name, VeroScriptFileType, text) {
	init { isWritable = false }
}
