// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.run

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.intellij.execution.Executor
import com.intellij.execution.configurations.*
import com.intellij.execution.executors.DefaultRunExecutor
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessHandler
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.util.execution.ParametersListUtil
import com.vanstysanic.veroscript.gui.jetbrains.GuiBundle.message
import com.vanstysanic.veroscript.gui.jetbrains.GuiIcons
import com.vanstysanic.veroscript.jetbrains.run.ResolvedRun
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import java.nio.file.Files
import java.nio.file.Path

class GuiRunOptions : RunConfigurationOptions() {
	var task by string("preview")
	var entry by string("")
	var workingDirectory by string("")
	var arguments by string("")
	var output by string("")
	var applicationName by string("")
	var assets by string("[]")
	var frames by string("")
	var packageRoot by string("")
	var platformBackend by string("")
	var graphicsBackend by string("")
	var guiBackend by string("")
	var imageBackend by string("")
	var fontBackend by string("")
	var textLayoutBackend by string("")
	var semantics by string("{}")
	var environment by string("{}")
	var inheritParentEnvironment by property(true)

	/** A context action can inherit a template for a different task or entry type. */
	fun selectTask(value: String) {
		task = value
		if (value != "preview") {
			arguments = ""
		}
		if (value != "preview" || !entry.orEmpty().endsWith(".vswml")) {
			frames = ""
		}
		if (value != "compile") {
			output = ""
			applicationName = ""
			assets = "[]"
		}
	}
}

class GuiConfigurationType : ConfigurationType {
	private val factory = object : ConfigurationFactory(this) {
		override fun getId() = "VeroScriptGui"
		override fun createTemplateConfiguration(project: Project) = GuiRunConfiguration(project, this, "VeroScript GUI")
		override fun getOptionsClass() = GuiRunOptions::class.java
	}
	override fun getId() = "VeroScriptGuiRunConfiguration"
	override fun getDisplayName() = "VeroScript GUI"
	override fun getConfigurationTypeDescription() = message("run.description")
	override fun getIcon() = GuiIcons.file
	override fun getConfigurationFactories() = arrayOf(factory)
	companion object {
		fun factory() = ConfigurationTypeUtil.findConfigurationType(GuiConfigurationType::class.java).configurationFactories[0]
	}
}

/** Uses the native command service; no compiler, backend or GUI state lives in the IDE. */
class GuiRunConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
	RunConfigurationBase<GuiRunOptions>(project, factory, name) {
	public override fun getOptions() = super.getOptions() as GuiRunOptions
	override fun getConfigurationEditor(): SettingsEditor<out RunConfiguration> = GuiRunEditor(project)
	override fun checkConfiguration() {
		try { resolve() }
		catch (failure: com.intellij.openapi.progress.ProcessCanceledException)
		{
			throw failure
		}
		catch (failure: Exception)
		{
			throw RuntimeConfigurationError(failure.message.orEmpty())
		}
	}

	fun resolve(): GuiCommand {
		val root = Path.of(requireNotNull(project.basePath) { message("error.project") })
		val cwd = SemanticOptions.absolute(options.workingDirectory.orEmpty().ifBlank { "." }, root)
		require(Files.isDirectory(cwd)) { message("error.directory", cwd) }
		val settings = VeroScriptProjectSettings.get(project)
		val semantics = SemanticOptions.resolve(settings.configuration(), root)
		val overrides = SemanticOptions.resolve(SemanticOptions.parse(options.semantics.orEmpty()), cwd, root)
		if (overrides.has("stdlibPath")) semantics.remove("stdlibSearchPaths")
		if (overrides.has("stdlibSearchPaths")) semantics.remove("stdlibPath")
		overrides.entrySet().forEach { (key, value) -> semantics.add(key, value) }
		val runtime = RuntimeLayout.resolve(project)
		if (!semantics.has("stdlibPath") && !semantics.has("stdlibSearchPaths")) semantics.addProperty("stdlibPath", runtime.stdlib.toString())
		return GuiCommand.resolve(options, root, cwd, runtime, semantics, settings.locale())
	}

	override fun getState(executor: Executor, environment: ExecutionEnvironment): RunProfileState {
		require(executor.id == DefaultRunExecutor.EXECUTOR_ID) { message("error.debug") }
		return object : CommandLineState(environment) {
			override fun startProcess(): ProcessHandler {
				val run = resolve()
				consoleBuilder.addFilter(GuiDiagnosticFilter(project, run.cwd))
				return KillableColoredProcessHandler(run.commandLine())
			}
		}
	}
}

data class GuiCommand(val runtime: RuntimeLayout, val entry: Path, val cwd: Path, val task: String,
	val semantics: JsonObject, val locale: String, val scriptArguments: List<String>, val output: Path?,
	val applicationName: String, val assets: List<Path>, val frames: Long?, val packageRoot: Path?,
	val backends: Map<String, String>, val environment: Map<String, String>, val inheritEnvironment: Boolean) {

	fun arguments(): List<String> = buildList {
		// Checking and baking must not load a package environment's execution JIT provider.
		if (task != "preview") add("--backend=vm")
		addAll(listOf("--lang", locale))
		fun paths(key: String, flag: String) {
			semantics.getAsJsonArray(key)?.forEach { addAll(listOf(flag, it.asString)) }
		}
		paths("importPaths", "-I")
		paths("stdlibSearchPaths", "--stdlib-path")
		semantics["stdlibPath"]?.let { addAll(listOf("--stdlib-path", it.asString)) }
		paths("implicitImportPaths", "--implicit-import")
		paths("bindingCatalogPaths", "--binding-catalog")
		paths("defines", "-D")
		semantics.getAsJsonArray("predefinedMacros")?.forEach { addAll(listOf("--macro", ResolvedRun.macroArgument(it.asJsonObject))) }
		if (semantics["allowCircularImports"]?.asBoolean == false) add("--no-circular-imports")
		if (semantics["allowProjectWideAccess"]?.asBoolean == true) add("--allow-project-wide-access")
		packageRoot?.let { addAll(listOf("--path", it.toString())) }
		// Environment/compiler options precede module options; GUI options precede task dispatch.
		backends.forEach { (key, value) -> if (value.isNotBlank()) addAll(listOf("--$key", value)) }
		addAll(listOf("gui-$task", entry.toString()))
		if (task == "compile") {
			addAll(listOf("--output", requireNotNull(output).toString()))
			if (applicationName.isNotBlank()) addAll(listOf("--name", applicationName))
			assets.forEach { addAll(listOf("--asset", it.toString())) }
		}
		if (task == "preview") {
			frames?.let { addAll(listOf("--frames", it.toString())) }
			if (scriptArguments.isNotEmpty()) {
				add("--")
				addAll(scriptArguments)
			}
		}
	}

	fun commandLine() = GeneralCommandLine(runtime.tool("").toString()).withParameters(arguments())
		.withWorkDirectory(cwd.toFile()).withEnvironment(environment).withCharset(Charsets.UTF_8)
		.withParentEnvironmentType(if (inheritEnvironment) GeneralCommandLine.ParentEnvironmentType.CONSOLE
		else GeneralCommandLine.ParentEnvironmentType.NONE)

	companion object {
		val inputExtensions = setOf("vs", "vswml", "vsapp")
		fun persist(path: Path, root: Path): String {
			val absolute = path.toAbsolutePath().normalize()
			val base = root.toAbsolutePath().normalize()
			return if (absolute.startsWith(base)) "\$PROJECT_DIR\$/" + base.relativize(absolute).toString().replace(java.io.File.separatorChar, '/')
			else absolute.toString()
		}
		fun resolve(options: GuiRunOptions, root: Path, cwd: Path, runtime: RuntimeLayout, semantics: JsonObject, locale: String): GuiCommand {
			val task = options.task.orEmpty()
			require(task in setOf("preview", "check", "compile")) { message("error.task") }
			require(!options.entry.isNullOrBlank()) { message("error.entry") }
			val entry = SemanticOptions.absolute(options.entry.orEmpty(), cwd, root)
			val extension = entry.fileName.toString().substringAfterLast('.', "")
			require(Files.isRegularFile(entry) && extension in inputExtensions) { message("error.entry") }
			require(task != "compile" || extension != "vsapp") { message("error.compileSource") }
			val output = options.output.orEmpty().takeIf { it.isNotBlank() }?.let { SemanticOptions.absolute(it, cwd, root) }
			val assetsJson = json(options.assets.orEmpty().ifBlank { "[]" })
			require(assetsJson.isJsonArray && assetsJson.asJsonArray.all {
				it.isJsonPrimitive && it.asJsonPrimitive.isString && it.asString.isNotBlank() && '\u0000' !in it.asString
			}) { message("error.assets") }
			val assets = assetsJson.asJsonArray.map { SemanticOptions.absolute(it.asString, cwd, root) }
			val name = options.applicationName.orEmpty()
			require('\u0000' !in name) { message("error.name") }
			require(task == "compile" || output == null && name.isBlank() && assets.isEmpty()) { message("error.compileOptions") }
			if (task == "compile") {
				require(output != null && output.fileName.toString().endsWith(".vsapp") && !Files.isDirectory(output)) { message("error.output") }
				require(output != entry && (!Files.exists(output) || !Files.isSameFile(output, entry))) { message("error.overwrite") }
				require(assets.all { Files.isRegularFile(it) }) { message("error.assetMissing") }
				require(assets.none { it == output || Files.exists(output) && Files.isSameFile(it, output) }) { message("error.overwrite") }
			}
			val frameText = options.frames.orEmpty().trim()
			val frames = if (frameText.isEmpty() || frameText == "0") null
			else frameText.toLongOrNull().also { require(it != null && it > 0) { message("error.frames") } }
			require(frames == null || task == "preview" && extension == "vswml") { message("error.frameEntry") }
			val arguments = options.arguments.orEmpty()
			require('\u0000' !in arguments) { message("error.arguments") }
			require(task == "preview" || arguments.isBlank()) { message("error.previewOptions") }
			val packageRoot = options.packageRoot.orEmpty().takeIf { it.isNotBlank() }?.let { SemanticOptions.absolute(it, cwd, root) }
			require(packageRoot == null || Files.isDirectory(packageRoot)) { message("error.directory", packageRoot ?: "") }
			val backends = linkedMapOf("platform-backend" to options.platformBackend.orEmpty(),
				"graphics-backend" to options.graphicsBackend.orEmpty(), "gui-backend" to options.guiBackend.orEmpty(),
				"image-backend" to options.imageBackend.orEmpty(), "font-backend" to options.fontBackend.orEmpty(),
				"text-layout-backend" to options.textLayoutBackend.orEmpty())
			require(backends.values.all { '\u0000' !in it && (it.isEmpty() || it.isNotBlank() && it == it.trim()) }) { message("error.backend") }
			val environment = json(options.environment.orEmpty().ifBlank { "{}" })
			require(environment.isJsonObject && environment.asJsonObject.entrySet().all {
				it.key.isNotBlank() && '=' !in it.key && '\u0000' !in it.key && it.value.isJsonPrimitive &&
					it.value.asJsonPrimitive.isString && '\u0000' !in it.value.asString
			}) { message("error.environment") }
			return GuiCommand(runtime, entry, cwd, task, semantics, locale, ParametersListUtil.parse(arguments), output, name,
				assets, frames, packageRoot, backends, environment.asJsonObject.entrySet().associate { it.key to it.value.asString },
				options.inheritParentEnvironment)
		}

		private fun json(text: String) = try { JsonParser.parseString(text) }
		catch (failure: com.google.gson.JsonParseException)
		{
			throw IllegalArgumentException(message("error.json"), failure)
		}
	}
}

