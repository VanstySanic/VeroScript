// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.run

import com.intellij.execution.RunManager
import com.intellij.execution.actions.ConfigurationContext
import com.intellij.execution.actions.LazyRunConfigurationProducer
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.runners.ExecutionEnvironmentBuilder
import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.openapi.project.DumbAware
import com.intellij.openapi.util.Ref
import com.intellij.psi.PsiElement
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptDebugSourceFile
import java.nio.file.Path

class VeroScriptRunConfigurationProducer : LazyRunConfigurationProducer<VeroScriptRunConfiguration>() {
	override fun getConfigurationFactory() = VeroScriptConfigurationType.factory()
	override fun setupConfigurationFromContext(configuration: VeroScriptRunConfiguration, context: ConfigurationContext,
		sourceElement: Ref<PsiElement>): Boolean {
		val file = context.psiLocation?.containingFile ?: return false
		if (file.fileType != VeroScriptFileType) return false
		val virtualFile = file.virtualFile ?: return false
		if (virtualFile is VeroScriptDebugSourceFile || virtualFile.fileSystem.protocol != "file") return false
		val root = configuration.project.basePath?.let(Path::of) ?: return false
		configuration.options.program = RunProgramPaths.persist(Path.of(virtualFile.path), root)
		configuration.options.selectContextMode("run")
		configuration.name = virtualFile.nameWithoutExtension
		sourceElement.set(file)
		return true
	}
	override fun isConfigurationFromContext(configuration: VeroScriptRunConfiguration, context: ConfigurationContext): Boolean {
		if (configuration.options.mode != "run") return false
		val file = context.psiLocation?.containingFile?.virtualFile ?: return false
		if (file is VeroScriptDebugSourceFile || file.fileType != VeroScriptFileType || file.fileSystem.protocol != "file") return false
		val path = file.path
		val root = configuration.project.basePath?.let(Path::of) ?: return false
		return runCatching { RunProgramPaths.resolve(configuration.options, root) == Path.of(path).toAbsolutePath().normalize() }.getOrDefault(false)
	}
}

class CheckVeroScriptAction : AnAction(), DumbAware {
	override fun getActionUpdateThread() = com.intellij.openapi.actionSystem.ActionUpdateThread.BGT
	override fun update(event: AnActionEvent) {
		val file = event.getData(CommonDataKeys.VIRTUAL_FILE)
		event.presentation.isEnabledAndVisible = event.project?.basePath != null &&
			file != null && file.fileType == VeroScriptFileType && file !is VeroScriptDebugSourceFile && file.fileSystem.protocol == "file"
	}
	override fun actionPerformed(event: AnActionEvent) {
		val project = event.project ?: return
		val file = event.getData(CommonDataKeys.VIRTUAL_FILE) ?: return
		if (file is VeroScriptDebugSourceFile || file.fileType != VeroScriptFileType || file.fileSystem.protocol != "file") return
		val root = project.basePath?.let(Path::of) ?: return
		FileDocumentManager.getInstance().saveAllDocuments()
		val manager = RunManager.getInstance(project)
		val settings = manager.createConfiguration(message("run.checkName", file.name),
			VeroScriptConfigurationType.factory())
		(settings.configuration as VeroScriptRunConfiguration).options.apply {
			program = RunProgramPaths.persist(Path.of(file.path), root)
			selectContextMode("check")
		}
		manager.setTemporaryConfiguration(settings)
		ExecutionEnvironmentBuilder.create(DefaultRunExecutor.getRunExecutorInstance(), settings).buildAndExecute()
	}
}
