// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.run

import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.TextFieldWithBrowseButton
import com.intellij.ui.components.JBCheckBox
import com.intellij.ui.components.JBScrollPane
import com.intellij.ui.components.JBTextArea
import com.intellij.util.ui.FormBuilder
import com.vanstysanic.veroscript.gui.jetbrains.GuiBundle.message
import javax.swing.JComboBox
import javax.swing.JLabel
import javax.swing.JTextField
import javax.swing.DefaultListCellRenderer
import javax.swing.JList

class GuiRunEditor(project: Project) : SettingsEditor<GuiRunConfiguration>() {
	private val task = JComboBox(arrayOf("preview", "check", "compile"))
	private val entry = TextFieldWithBrowseButton().apply {
		addBrowseFolderListener(project, FileChooserDescriptorFactory.singleFile())
	}
	private val cwd = TextFieldWithBrowseButton().apply {
		addBrowseFolderListener(project, FileChooserDescriptorFactory.createSingleFolderDescriptor())
	}
	private val packageRoot = TextFieldWithBrowseButton().apply {
		addBrowseFolderListener(project, FileChooserDescriptorFactory.createSingleFolderDescriptor())
	}
	private val arguments = JTextField()
	private val output = JTextField()
	private val applicationName = JTextField()
	private val assets = JBTextArea(3, 60)
	private val frames = JTextField()
	private val environment = JTextField("{}")
	private val inheritEnvironment = JBCheckBox(message("run.inheritEnvironment"))
	private val semantics = JBTextArea(5, 60)
	private val platform = JTextField()
	private val graphics = JTextField()
	private val gui = JTextField()
	private val image = JTextField()
	private val font = JTextField()
	private val textLayout = JTextField()

	init {
		task.renderer = object : DefaultListCellRenderer() {
			override fun getListCellRendererComponent(list: JList<*>?, value: Any?, index: Int, selected: Boolean, focus: Boolean): java.awt.Component {
				val label = when (value) {
					"preview" -> message("task.preview")
					"check" -> message("task.check")
					"compile" -> message("task.compile")
					else -> value
				}
				return super.getListCellRendererComponent(list, label, index, selected, focus)
			}
		}
		task.addActionListener { updateFields() }
	}
	private fun updateFields() {
		val preview = task.selectedItem == "preview"
		val compile = task.selectedItem == "compile"
		arguments.isEnabled = preview
		frames.isEnabled = preview
		output.isEnabled = compile
		applicationName.isEnabled = compile
		assets.isEnabled = compile
	}
	override fun createEditor() = JBScrollPane(FormBuilder.createFormBuilder()
		.addComponent(JLabel(message("run.runtimeHelp")))
		.addLabeledComponent(message("run.task"), task)
		.addLabeledComponent(message("run.entry"), entry)
		.addLabeledComponent(message("run.cwd"), cwd)
		.addLabeledComponent(message("run.packageRoot"), packageRoot)
		.addLabeledComponent(message("run.arguments"), arguments)
		.addLabeledComponent(message("run.frames"), frames)
		.addLabeledComponent(message("run.output"), output)
		.addLabeledComponent(message("run.name"), applicationName)
		.addLabeledComponent(message("run.assets"), JBScrollPane(assets), true)
		.addComponent(JLabel(message("run.taskHelp")))
		.addLabeledComponent(message("run.platform"), platform)
		.addLabeledComponent(message("run.graphics"), graphics)
		.addLabeledComponent(message("run.gui"), gui)
		.addLabeledComponent(message("run.image"), image)
		.addLabeledComponent(message("run.font"), font)
		.addLabeledComponent(message("run.textLayout"), textLayout)
		.addComponent(JLabel(message("run.backendHelp")))
		.addLabeledComponent(message("run.environment"), environment)
		.addComponent(inheritEnvironment)
		.addLabeledComponent(message("run.semantics"), JBScrollPane(semantics), true).panel)

	override fun resetEditorFrom(configuration: GuiRunConfiguration) {
		val options = configuration.options
		task.selectedItem = options.task
		entry.text = options.entry.orEmpty()
		cwd.text = options.workingDirectory.orEmpty()
		packageRoot.text = options.packageRoot.orEmpty()
		arguments.text = options.arguments
		output.text = options.output
		applicationName.text = options.applicationName
		assets.text = options.assets
		frames.text = options.frames
		platform.text = options.platformBackend
		graphics.text = options.graphicsBackend
		gui.text = options.guiBackend
		image.text = options.imageBackend
		font.text = options.fontBackend
		textLayout.text = options.textLayoutBackend
		environment.text = options.environment
		inheritEnvironment.isSelected = options.inheritParentEnvironment
		semantics.text = options.semantics
		updateFields()
	}
	override fun applyEditorTo(configuration: GuiRunConfiguration) {
		configuration.options.apply {
			task = this@GuiRunEditor.task.selectedItem as String
			entry = this@GuiRunEditor.entry.text
			workingDirectory = cwd.text
			packageRoot = this@GuiRunEditor.packageRoot.text
			arguments = this@GuiRunEditor.arguments.text.takeIf { task == "preview" }.orEmpty()
			frames = this@GuiRunEditor.frames.text.takeIf { task == "preview" }.orEmpty()
			output = this@GuiRunEditor.output.text.takeIf { task == "compile" }.orEmpty()
			applicationName = this@GuiRunEditor.applicationName.text.takeIf { task == "compile" }.orEmpty()
			assets = this@GuiRunEditor.assets.text.takeIf { task == "compile" } ?: "[]"
			platformBackend = platform.text
			graphicsBackend = graphics.text
			guiBackend = gui.text
			imageBackend = image.text
			fontBackend = font.text
			textLayoutBackend = textLayout.text
			environment = this@GuiRunEditor.environment.text
			inheritParentEnvironment = inheritEnvironment.isSelected
			semantics = this@GuiRunEditor.semantics.text
		}
	}
}
