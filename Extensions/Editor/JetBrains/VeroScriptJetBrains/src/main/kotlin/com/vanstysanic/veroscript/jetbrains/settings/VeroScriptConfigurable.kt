// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.settings

import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.options.ConfigurationException
import com.intellij.openapi.options.SearchableConfigurable
import com.intellij.openapi.project.Project
import com.intellij.platform.lsp.api.LspClientManager
import com.intellij.ui.components.JBCheckBox
import com.intellij.ui.components.JBScrollPane
import com.intellij.ui.components.JBTextArea
import com.intellij.util.ui.FormBuilder
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptLspProvider
import java.awt.Dimension
import javax.swing.JButton
import javax.swing.JComponent
import javax.swing.JPanel
import javax.swing.JTextField

class VeroScriptConfigurable(private val project: Project) : SearchableConfigurable {
	private fun runtimeField() = com.intellij.openapi.ui.TextFieldWithBrowseButton().apply {
		addBrowseFolderListener(project, com.intellij.openapi.fileChooser.FileChooserDescriptorFactory.createSingleFolderDescriptor())
	}
	private val defaultRuntime = runtimeField()
	private val localRuntime = runtimeField()
	private val locale = JTextField()
	private val enabled = JBCheckBox(message("settings.enabled"))
	private val braces = JBCheckBox(message("settings.bracesOnNewLine"))
	private val semantics = JBTextArea(18, 64)
	private var component: JPanel? = null
	override fun getId() = "veroscript.settings"
	override fun getDisplayName() = "VeroScript"

	override fun createComponent(): JComponent {
		val check = JButton(message("settings.checkRuntime"))
		check.addActionListener {
			val path = localRuntime.text.ifBlank { defaultRuntime.text }
			val modality = com.intellij.openapi.application.ModalityState.stateForComponent(check)
			check.isEnabled = false
			ApplicationManager.getApplication().executeOnPooledThread {
				val result = runCatching {
					RuntimeLayout.resolve(project, path).validate()
				}
				ApplicationManager.getApplication().invokeLater({
					if (!project.isDisposed) {
						check.isEnabled = true
						NotificationGroupManager.getInstance().getNotificationGroup("VeroScript")
							.createNotification("VeroScript", result.getOrElse { it.message.orEmpty() },
								if (result.isSuccess) NotificationType.INFORMATION
								else NotificationType.ERROR).notify(project)
					}
				}, modality)
			}
		}
		val scroll = JBScrollPane(semantics).apply { preferredSize = Dimension(640, 320) }
		component = FormBuilder.createFormBuilder()
			.addLabeledComponent(message("settings.defaultRuntime"), defaultRuntime)
			.addLabeledComponent(message("settings.localRuntime"), localRuntime)
			.addComponent(check)
			.addLabeledComponent(message("settings.locale"), locale)
			.addComponent(enabled)
			.addComponent(braces)
			.addComponent(javax.swing.JLabel(message("settings.semanticHelp")))
			.addLabeledComponent(message("settings.semantics"), scroll, true)
			.addComponentFillVertically(JPanel(), 0).panel
		reset()
		return component!!
	}

	private fun application() = ApplicationManager.getApplication().getService(VeroScriptApplicationSettings::class.java).state
	private fun personal() = project.getService(VeroScriptLocalSettings::class.java).state
	private fun shared() = VeroScriptProjectSettings.get(project).state

	override fun isModified() = defaultRuntime.text != application().runtimeHome || localRuntime.text != personal().runtimeHome ||
		locale.text != shared().locale || enabled.isSelected != shared().enabled ||
		braces.isSelected != shared().bracesOnNewLine || semantics.text != shared().semantics

	override fun apply() {
		try {
			SemanticOptions.parse(semantics.text)
			if (locale.text.isNotBlank()) java.util.Locale.Builder().setLanguageTag(locale.text.trim())
		}
		catch (exception: Exception)
		{
			throw ConfigurationException(exception.message.orEmpty())
		}
		val changedDefault = application().runtimeHome != defaultRuntime.text.trim()
		val changedProject = personal().runtimeHome != localRuntime.text.trim() || shared().locale != locale.text.trim() ||
			shared().enabled != enabled.isSelected || shared().semantics != semantics.text
		application().runtimeHome = defaultRuntime.text.trim()
		personal().runtimeHome = localRuntime.text.trim()
		shared().locale = locale.text.trim()
		shared().enabled = enabled.isSelected
		shared().bracesOnNewLine = braces.isSelected
		shared().semantics = semantics.text
		shared().schemaVersion = 1
		if (!changedDefault && !changedProject) return
		val targets = if (changedDefault) com.intellij.openapi.project.ProjectManager.getInstance().openProjects.filter {
			it == project || it.getService(VeroScriptLocalSettings::class.java).state.runtimeHome.isBlank()
		}
		else listOf(project)
		targets.forEach { target ->
			target.getService(com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptLspStatus::class.java).reset()
			target.getService(com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptLspStartup::class.java).invalidate()
			if (VeroScriptProjectSettings.get(target).state.enabled) {
				LspClientManager.getInstance(target).stopAndRestartClientsIfNeeded(VeroScriptLspProvider::class.java)
			}
			else LspClientManager.getInstance(target).stopClients(VeroScriptLspProvider::class.java)
		}
	}

	override fun reset() {
		defaultRuntime.text = application().runtimeHome
		localRuntime.text = personal().runtimeHome
		locale.text = shared().locale
		enabled.isSelected = shared().enabled
		braces.isSelected = shared().bracesOnNewLine
		semantics.text = shared().semantics
	}
	override fun disposeUIResources() { component = null }
}
