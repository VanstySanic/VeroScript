// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.run

import com.intellij.execution.RunManager
import com.intellij.execution.actions.ConfigurationContext
import com.intellij.execution.actions.LazyRunConfigurationProducer
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.runners.ExecutionEnvironmentBuilder
import com.intellij.openapi.actionSystem.*
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.util.Ref
import com.intellij.psi.PsiElement
import com.vanstysanic.veroscript.gui.jetbrains.GuiBundle.message
import com.vanstysanic.veroscript.gui.jetbrains.language.GuiMarkupFileType
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import java.nio.file.Path

class GuiRunProducer : LazyRunConfigurationProducer<GuiRunConfiguration>() {
	override fun getConfigurationFactory() = GuiConfigurationType.factory()
	override fun setupConfigurationFromContext(configuration: GuiRunConfiguration, context: ConfigurationContext,
		sourceElement: Ref<PsiElement>): Boolean {
		val file = context.psiLocation?.containingFile ?: return false
		val virtual = file.virtualFile ?: return false
		if (file.fileType != GuiMarkupFileType || virtual.fileSystem.protocol != "file") return false
		val root = configuration.project.basePath?.let(Path::of) ?: return false
		configuration.options.entry = GuiCommand.persist(Path.of(virtual.path), root)
		configuration.options.selectTask("preview")
		configuration.name = message("run.previewName", virtual.nameWithoutExtension)
		sourceElement.set(file)
		return true
	}
	override fun isConfigurationFromContext(configuration: GuiRunConfiguration, context: ConfigurationContext): Boolean {
		if (configuration.options.task != "preview") return false
		val file = context.psiLocation?.containingFile?.virtualFile ?: return false
		if (file.fileType != GuiMarkupFileType || file.fileSystem.protocol != "file") return false
		val root = configuration.project.basePath?.let(Path::of) ?: return false
		return runCatching {
			val cwd = SemanticOptions.absolute(configuration.options.workingDirectory.orEmpty().ifBlank { "." }, root)
			SemanticOptions.absolute(configuration.options.entry.orEmpty(), cwd, root) == Path.of(file.path).toAbsolutePath().normalize()
		}.getOrDefault(false)
	}
}

abstract class GuiTaskAction(private val task: String) : AnAction(), DumbAware {
	override fun getActionUpdateThread() = ActionUpdateThread.BGT
	override fun update(event: AnActionEvent) {
		val file = event.getData(CommonDataKeys.VIRTUAL_FILE)
		event.presentation.isEnabledAndVisible = event.project?.basePath != null && file != null && !file.isDirectory &&
			file.fileSystem.protocol == "file" && file.extension in GuiCommand.inputExtensions &&
			(task != "compile" || file.extension != "vsapp")
	}
	override fun actionPerformed(event: AnActionEvent) {
		val project = event.project ?: return
		val file = event.getData(CommonDataKeys.VIRTUAL_FILE) ?: return
		val root = project.basePath?.let(Path::of) ?: return
		FileDocumentManager.getInstance().saveAllDocuments()
		val manager = RunManager.getInstance(project)
		val settings = manager.createConfiguration(message("run.${task}Name", file.name), GuiConfigurationType.factory())
		val options = (settings.configuration as GuiRunConfiguration).options
		options.entry = GuiCommand.persist(Path.of(file.path), root)
		options.selectTask(task)
		// Baking always requires reviewing the destination and asset list before the first write.
		if (task == "compile") {
			if (options.output.isNullOrBlank()) {
				options.output = GuiCommand.persist(root.resolve(".tmp/gui/${file.nameWithoutExtension}.vsapp"), root)
			}
			if (com.intellij.execution.impl.RunDialog.editConfiguration(project, settings, message("action.compile"))) {
				manager.setTemporaryConfiguration(settings)
			}
			return
		}
		manager.setTemporaryConfiguration(settings)
		ExecutionEnvironmentBuilder.create(DefaultRunExecutor.getRunExecutorInstance(), settings).buildAndExecute()
	}
}
class GuiPreviewAction : GuiTaskAction("preview")
class GuiCheckAction : GuiTaskAction("check")
class GuiCompileAction : GuiTaskAction("compile")
