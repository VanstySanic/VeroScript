// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.lsp

import com.intellij.markdown.utils.doc.DocMarkdownToHtmlConverter
import com.intellij.model.Pointer
import com.intellij.openapi.application.ReadAction
import com.intellij.openapi.editor.Document
import com.intellij.openapi.progress.ProcessCanceledException
import com.intellij.openapi.util.text.StringUtil
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.backend.documentation.DocumentationResult
import com.intellij.platform.backend.documentation.DocumentationTarget
import com.intellij.platform.backend.documentation.DocumentationTargetProvider
import com.intellij.platform.backend.presentation.TargetPresentation
import com.intellij.platform.lsp.api.LspClient
import com.intellij.platform.lsp.api.LspClientManager
import com.intellij.platform.lsp.api.LspServerState
import com.intellij.psi.PsiFile
import com.vanstysanic.veroscript.jetbrains.VeroScriptIcons
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptDebugSourceFile
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptLanguage
import org.eclipse.lsp4j.Hover
import org.eclipse.lsp4j.HoverParams
import org.eclipse.lsp4j.Position
import java.util.concurrent.CancellationException

/** Uses the public LSP transport and documentation API to customize Rider presentation. */
class VeroScriptDocumentationProvider : DocumentationTargetProvider {
	override fun documentationTargets(file: PsiFile, offset: Int): List<DocumentationTarget> {
		if (file.fileType != VeroScriptFileType || file.project.isDisposed) return emptyList()
		val virtualFile = file.virtualFile ?: return emptyList()
		if (virtualFile is VeroScriptDebugSourceFile) return emptyList()
		val document = file.viewProvider.document ?: return emptyList()
		if (offset !in 0 until document.textLength) return emptyList()
		val client = LspClientManager.getInstance(file.project).getClients(VeroScriptLspProvider::class.java)
			.firstOrNull { it.state == LspServerState.Running && it.descriptor.isSupportedFile(virtualFile) }
			?: return emptyList()
		val line = document.getLineNumber(offset)
		val position = Position(line, offset - document.getLineStartOffset(line))
		val name = file.findElementAt(offset)?.text?.take(128).orEmpty()
		return listOf(VeroScriptDocumentationTarget(client, virtualFile, document, document.modificationStamp, position, name))
	}
}

internal class VeroScriptDocumentationTarget(
	private val client: LspClient, private val file: VirtualFile, private val document: Document,
	private val stamp: Long, private val position: Position, private val name: String
) : DocumentationTarget {
	override fun createPointer(): Pointer<VeroScriptDocumentationTarget> = Pointer { if (isCurrent()) this
	else null }
	override fun computePresentation() = TargetPresentation.builder(name).icon(VeroScriptIcons.file).presentation()
	private fun isCurrent() = !client.project.isDisposed && file.isValid &&
		client.state == LspServerState.Running && document.modificationStamp == stamp

	override fun computeDocumentation(): DocumentationResult = DocumentationResult.asyncDocumentation {
		if (!isCurrent()) return@asyncDocumentation null
		try {
			// Documentation computation is asynchronous; never wait for a server on the UI thread.
			val hover = client.sendRequestSync(LspClient.DEFAULT_REQUEST_TIMEOUT_MS) {
				it.textDocumentService.hover(HoverParams(client.getDocumentIdentifier(file), position))
			} ?: return@asyncDocumentation null
			if (!isCurrent()) return@asyncDocumentation null
			ReadAction.computeBlocking<DocumentationResult.Documentation?, RuntimeException> {
				if (!isCurrent()) null
				else DocumentationResult.documentation(render(client.project, hover))
			}
		}
		catch (failure: ProcessCanceledException)
		{
			throw failure
		}
		catch (failure: CancellationException)
		{
			throw failure
		}
		catch (failure: Exception)
		{
			com.intellij.openapi.diagnostic.Logger.getInstance(VeroScriptDocumentationTarget::class.java)
				.debug("Unable to load VeroScript hover", failure)
			null
		}
	}

	companion object {
		internal fun render(project: com.intellij.openapi.project.Project, hover: Hover): String {
			val contents = hover.contents ?: return ""
			if (contents.isRight) {
				val content = contents.right
				if (content.kind != "markdown") return "<pre>" + StringUtil.escapeXmlEntities(content.value) + "</pre>"
				return DocMarkdownToHtmlConverter.convert(project, VeroScriptDocumentation.hoverMarkdown(content.value), VeroScriptLanguage)
			}
			return contents.left.joinToString("\n") { item ->
				val markdown = if (item.isLeft) VeroScriptDocumentation.hoverMarkdown(item.left)
					else VeroScriptDocumentation.code(item.right.value, 3)
				DocMarkdownToHtmlConverter.convert(project, markdown, VeroScriptLanguage)
			}
		}
	}
}
