// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.lsp

import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.notification.NotificationAction
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.options.ShowSettingsUtil
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspClient
import com.intellij.platform.lsp.api.LspClientManager
import com.intellij.platform.lsp.api.LspIntegrationProvider
import com.intellij.platform.lsp.api.ProjectWideLspClientDescriptor
import com.intellij.platform.lsp.api.customization.LspCustomization
import com.intellij.platform.lsp.api.customization.LspOnTypeFormattingSupport
import com.intellij.platform.lsp.api.customization.LspSemanticTokensSupport
import com.intellij.platform.lsp.api.lsWidget.LspClientWidgetItem
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors as Colors
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import com.vanstysanic.veroscript.jetbrains.VeroScriptIcons
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptDebugSourceFile
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptTextAttributes
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptConfigurable
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import org.eclipse.lsp4j.ConfigurationItem
import java.nio.file.Path

class VeroScriptLspProvider : LspIntegrationProvider {
	override fun fileOpened(project: Project, file: VirtualFile, clientStarter: LspIntegrationProvider.LspClientStarter) {
		if (file.fileType != VeroScriptFileType || file is VeroScriptDebugSourceFile || !VeroScriptProjectSettings.get(project).state.enabled) return
		project.getService(VeroScriptLspStartup::class.java).request(clientStarter)
	}

	override fun createWidgetItem(lspClient: LspClient, currentFile: VirtualFile?) =
		LspClientWidgetItem(lspClient, currentFile, VeroScriptIcons.file, VeroScriptConfigurable::class.java)
}

class VeroScriptLspDescriptor(project: Project, private val runtime: RuntimeLayout) :
	ProjectWideLspClientDescriptor(project, "VeroScript") {
	override fun isSupportedFile(file: VirtualFile) = file.fileType == VeroScriptFileType && file !is VeroScriptDebugSourceFile
	override fun getLanguageId(file: VirtualFile) = "veroscript"
	override fun createCommandLine() = GeneralCommandLine(runtime.tool("-lsp").toString())
		.withWorkDirectory(requireNotNull(project.basePath))
		.withCharset(Charsets.UTF_8)
	override fun createInitializeParams() = super.createInitializeParams().apply {
		locale = VeroScriptProjectSettings.get(project).locale()
	}
	private fun options() = SemanticOptions.resolve(VeroScriptProjectSettings.get(project).configuration(),
		Path.of(requireNotNull(project.basePath))).apply {
		if (!has("stdlibPath") && !has("stdlibSearchPaths")) addProperty("stdlibPath", runtime.stdlib.toString())
	}
	override fun createInitializationOptions(): Any = mapOf("veroscript" to options())
	override fun getWorkspaceConfiguration(item: ConfigurationItem): Any =
		if (item.section == "veroscript") options()
		else mapOf("veroscript" to options())
	override val lspCustomization = object : LspCustomization() {
		// The provider keeps native hover semantics and adapts only Rider's Markdown presentation.
		override val hoverCustomizer = com.intellij.platform.lsp.api.customization.LspHoverDisabled
		override val completionCustomizer = VeroScriptCompletionSupport()
		override val onTypeFormattingCustomizer = LspOnTypeFormattingSupport()
		override val semanticTokensCustomizer = object : LspSemanticTokensSupport() {
			override fun shouldAskServerForSemanticTokens(psiFile: com.intellij.psi.PsiFile) = psiFile.fileType == VeroScriptFileType
			override val tokenTypes = super.tokenTypes + listOf("decoratorArgument", "constant", "label", "preprocessor")
			override fun getTextAttributesKey(tokenType: String, modifiers: List<String>) = when (tokenType) {
				"namespace" -> Colors.CLASS_NAME
				"type", "class", "enum", "interface", "struct" -> Colors.CLASS_NAME
				"property" -> if ("readonly" in modifiers) Colors.CONSTANT
				else Colors.INSTANCE_FIELD
				"enumMember", "constant" -> Colors.CONSTANT
				"function" -> Colors.FUNCTION_DECLARATION
				"method" -> Colors.INSTANCE_METHOD
				"macro", "preprocessor", "decorator", "decoratorArgument" -> VeroScriptTextAttributes.metadata
				"keyword" -> VeroScriptTextAttributes.keyword
				"string" -> VeroScriptTextAttributes.string
				"number" -> VeroScriptTextAttributes.number
				"comment" -> VeroScriptTextAttributes.comment
				"label" -> Colors.LABEL
				else -> Colors.LOCAL_VARIABLE
			}
		}
	}
}

@com.intellij.openapi.components.Service(com.intellij.openapi.components.Service.Level.PROJECT)
class VeroScriptLspStatus(private val project: Project) {
	private var reported = false
	fun configurationError(exception: Exception) {
		if (reported) return
		reported = true
		NotificationGroupManager.getInstance().getNotificationGroup("VeroScript")
			.createNotification(message("lsp.configurationError"), exception.message.orEmpty(), NotificationType.WARNING)
			.addAction(NotificationAction.createSimple(message("settings.open")) {
				ShowSettingsUtil.getInstance().showSettingsDialog(project, VeroScriptConfigurable::class.java)
			}).notify(project)
	}
	fun reset() { reported = false }
}

class RestartVeroScriptLspAction : AnAction(), DumbAware {
	override fun getActionUpdateThread() = com.intellij.openapi.actionSystem.ActionUpdateThread.BGT
	override fun actionPerformed(event: AnActionEvent) {
		val project = event.project ?: return
		project.getService(VeroScriptLspStatus::class.java).reset()
		project.getService(VeroScriptLspStartup::class.java).invalidate()
		LspClientManager.getInstance(project).stopAndRestartClientsIfNeeded(VeroScriptLspProvider::class.java)
	}
	override fun update(event: AnActionEvent) { event.presentation.isEnabled = event.project != null }
}
