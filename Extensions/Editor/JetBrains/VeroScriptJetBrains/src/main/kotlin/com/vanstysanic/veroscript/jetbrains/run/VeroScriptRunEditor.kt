// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.run

import com.intellij.openapi.options.SettingsEditor
import com.intellij.ui.components.JBCheckBox
import com.intellij.ui.components.JBScrollPane
import com.intellij.ui.components.JBTextArea
import com.intellij.util.ui.FormBuilder
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import javax.swing.JComboBox
import javax.swing.JTextField
import com.intellij.openapi.project.Project
import com.intellij.openapi.ui.TextFieldWithBrowseButton
import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory

class VeroScriptRunEditor(project: Project) : SettingsEditor<VeroScriptRunConfiguration>() {
	private val program = TextFieldWithBrowseButton().apply {
		addBrowseFolderListener(project, FileChooserDescriptorFactory.createSingleFileDescriptor("vs"))
	}
	private val arguments = JTextField()
	private val overrideArguments = JBCheckBox(message("run.overrideArguments"))
	private val cwd = TextFieldWithBrowseButton().apply {
		addBrowseFolderListener(project, FileChooserDescriptorFactory.createSingleFolderDescriptor())
	}
	private val environment = JTextField("{}")
	private val inheritEnvironment = JBCheckBox(message("run.inheritEnvironment"))
	private val semantics = JBTextArea(8, 60)
	private val mode = JComboBox(arrayOf("run", "check", "attach"))
	private val attachMode = JComboBox(arrayOf("program", "domain"))
	private val domainId = JTextField()
	private val backend = JComboBox(arrayOf("mixed", "vm", "jit"))
	private val jitBackend = TextFieldWithBrowseButton().apply {
		addBrowseFolderListener(project, com.intellij.openapi.fileChooser.FileChooserDescriptor(true, true, false, false, false, false))
	}
	private val input = TextFieldWithBrowseButton().apply {
		addBrowseFolderListener(project, FileChooserDescriptorFactory.singleFile())
	}
	private val stop = JComboBox(arrayOf("inherit", "true", "false"))
	private val host = JTextField()
	private val port = JTextField()
	private val depth = JTextField()
	private val launchTimeout = JTextField()
	override fun createEditor() = FormBuilder.createFormBuilder()
		.addLabeledComponent(message("run.mode"), mode)
		.addLabeledComponent(message("run.attachMode"), attachMode)
		.addLabeledComponent(message("run.domainId"), domainId)
		.addComponent(javax.swing.JLabel(message("run.domainHelp")))
		.addLabeledComponent(message("run.program"), program)
		.addLabeledComponent(message("run.arguments"), arguments)
		.addComponent(overrideArguments)
		.addLabeledComponent(message("run.cwd"), cwd)
		.addLabeledComponent(message("run.environment"), environment)
		.addComponent(inheritEnvironment)
		.addLabeledComponent(message("run.backend"), backend)
		.addLabeledComponent(message("run.jitBackend"), jitBackend)
		.addComponent(javax.swing.JLabel(message("run.jitHelp")))
		.addLabeledComponent(message("run.input"), input)
		.addLabeledComponent(message("run.stopOnEntry"), stop)
		.addLabeledComponent(message("run.host"), host)
		.addLabeledComponent(message("run.port"), port)
		.addLabeledComponent(message("run.depth"), depth)
		.addLabeledComponent(message("run.launchTimeout"), launchTimeout)
		.addComponent(javax.swing.JLabel(message("run.overrideHelp")))
		.addLabeledComponent(message("run.overrides"), JBScrollPane(semantics), true).panel

	override fun resetEditorFrom(configuration: VeroScriptRunConfiguration) {
		val options = configuration.options
		program.text = options.program.orEmpty()
		arguments.text = options.arguments
		overrideArguments.isSelected = options.overrideArguments
		cwd.text = options.workingDirectory.orEmpty()
		environment.text = options.environment
		inheritEnvironment.isSelected = options.inheritParentEnvironment
		semantics.text = options.semantics
		mode.selectedItem = options.mode
		attachMode.selectedItem = options.attachMode
		domainId.text = options.domainId.orEmpty()
		backend.selectedItem = options.backend
		jitBackend.text = options.jitBackendPath.orEmpty()
		input.text = options.inputFile.orEmpty()
		stop.selectedItem = options.stopOnEntry
		host.text = options.host
		port.text = options.port.toString()
		depth.text = options.maxCallDepth.toString()
		launchTimeout.text = options.launchTimeoutSeconds.toString()
	}

	override fun applyEditorTo(configuration: VeroScriptRunConfiguration) {
		val options = configuration.options
		options.program = program.text
		options.arguments = arguments.text
		options.overrideArguments = overrideArguments.isSelected
		options.workingDirectory = cwd.text
		options.environment = environment.text
		options.inheritParentEnvironment = inheritEnvironment.isSelected
		options.semantics = semantics.text
		options.mode = mode.selectedItem as String
		options.attachMode = attachMode.selectedItem as String
		options.domainId = domainId.text
		options.backend = backend.selectedItem as String
		options.jitBackendPath = jitBackend.text
		options.inputFile = input.text
		options.stopOnEntry = stop.selectedItem as String
		options.host = host.text
		options.port = port.text.toIntOrNull() ?: 0
		options.maxCallDepth = depth.text.toIntOrNull() ?: -1
		options.launchTimeoutSeconds = launchTimeout.text.toIntOrNull() ?: 0
	}
}
