// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.run

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.intellij.execution.Executor
import com.intellij.execution.configurations.*
import com.intellij.execution.process.KillableColoredProcessHandler
import com.intellij.execution.process.ProcessHandler
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import com.intellij.util.execution.ParametersListUtil
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import com.vanstysanic.veroscript.jetbrains.VeroScriptIcons
import com.vanstysanic.veroscript.jetbrains.runtime.RuntimeLayout
import com.vanstysanic.veroscript.jetbrains.settings.SemanticOptions
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings
import java.nio.file.Files
import java.nio.file.Path

class VeroScriptRunOptions : RunConfigurationOptions() {
	var program by string("")
	var arguments by string("")
	var overrideArguments by property(false)
	var workingDirectory by string("")
	var environment by string("{}")
	var inheritParentEnvironment by property(true)
	var semantics by string("{}")
	var mode by string("run")
	var attachMode by string("program")
	var domainId by string("")
	var backend by string("mixed")
	var jitBackendPath by string("")
	var inputFile by string("")
	var stopOnEntry by string("inherit")
	var host by string("localhost")
	var port by property(4711)
	var maxCallDepth by property(0)
	var launchTimeoutSeconds by property(30)

	/** Context-created launch/check configurations can inherit an attach template. */
	fun selectContextMode(value: String) {
		require(value == "run" || value == "check") { message("run.invalidMode") }
		mode = value
		attachMode = "program"
		domainId = ""
		overrideArguments = false
		if (value == "check") {
			arguments = ""
			inputFile = ""
			jitBackendPath = ""
		}
	}
}

class VeroScriptConfigurationType : ConfigurationType {
	private val factory = object : ConfigurationFactory(this) {
		override fun getId() = "VeroScript"
		override fun createTemplateConfiguration(project: Project) = VeroScriptRunConfiguration(project, this, "VeroScript")
		override fun getOptionsClass() = VeroScriptRunOptions::class.java
	}
	override fun getDisplayName() = "VeroScript"
	override fun getConfigurationTypeDescription() = message("run.description")
	override fun getIcon() = VeroScriptIcons.file
	override fun getId() = "VeroScriptRunConfiguration"
	override fun getConfigurationFactories() = arrayOf(factory)
	companion object {
		fun factory() = ConfigurationTypeUtil.findConfigurationType(VeroScriptConfigurationType::class.java).configurationFactories[0]
	}
}

class VeroScriptRunConfiguration(project: Project, factory: ConfigurationFactory, name: String) :
	RunConfigurationBase<VeroScriptRunOptions>(project, factory, name) {
	public override fun getOptions() = super.getOptions() as VeroScriptRunOptions
	override fun getConfigurationEditor(): SettingsEditor<out RunConfiguration> = VeroScriptRunEditor(project)
	override fun checkConfiguration() {
		// The shared IDE validation also runs before Debug, which never uses a JIT module.
		try { resolve(includeJitBackend = false) }
		catch (exception: com.intellij.openapi.progress.ProcessCanceledException)
		{
			throw exception
		}
		catch (exception: Exception)
		{
			throw RuntimeConfigurationError(exception.message.orEmpty())
		}
	}

	fun resolve(includeJitBackend: Boolean = true): ResolvedRun {
		val values = options
		val root = Path.of(requireNotNull(project.basePath) { message("runtime.projectDirectory") })
		require(values.attachMode in setOf("program", "domain")) { message("run.invalidAttachMode") }
		require(values.mode == "attach" || (values.attachMode == "program" && values.domainId.isNullOrBlank())) {
			message("run.domainRequiresAttach")
		}
		require(values.mode != "attach" || values.jitBackendPath.isNullOrBlank()) { message("run.jitAttach") }
		if (values.mode == "attach" && values.attachMode == "domain") {
			require(values.port in 1..65535) { message("run.invalidPort") }
			val host = values.host.orEmpty().trim()
			require(host.isNotEmpty() && host.none { it.isWhitespace() || it.isISOControl() }) { message("run.invalidHost") }
			val domainId = values.domainId.orEmpty().trim()
			require(domainId.none { it.isISOControl() }) { message("run.invalidDomainId") }
			require(values.launchTimeoutSeconds in 1..3600) { message("run.startupBudget") }
			val semanticOptions = SemanticOptions.parse(values.semantics.orEmpty())
			val environment = JsonParser.parseString(values.environment.orEmpty().ifBlank { "{}" })
			require(values.program.isNullOrBlank() && values.arguments.isNullOrBlank() && !values.overrideArguments &&
				values.workingDirectory.isNullOrBlank() && values.inputFile.isNullOrBlank() &&
				values.inheritParentEnvironment && values.stopOnEntry == "inherit" && values.maxCallDepth == 0 &&
				values.backend == "mixed" && semanticOptions.size() == 0 && environment.isJsonObject && environment.asJsonObject.size() == 0) {
				message("run.domainOverrides")
			}
			return ResolvedRun(null, null, root, emptyList(), emptyMap(), semanticOptions, "attach", "mixed",
				null, null, host, values.port, 0, VeroScriptProjectSettings.get(project).locale(),
				launchTimeoutSeconds = values.launchTimeoutSeconds, attachMode = "domain", domainId = domainId)
		}
		require(values.domainId.isNullOrBlank()) { message("run.domainRequiresAttach") }
		val cwd = SemanticOptions.absolute(values.workingDirectory.orEmpty().ifBlank { "." }, root)
		require(Files.isDirectory(cwd)) { message("run.cwdMissing", cwd) }
		require(values.mode in setOf("run", "check", "attach")) { message("run.invalidMode") }
		require(values.backend in setOf("vm", "mixed", "jit")) { message("run.invalidBackend") }
		require(values.port in 1..65535) { message("run.invalidPort") }
		require(values.maxCallDepth >= 0) { message("run.invalidDepth") }
		require(values.launchTimeoutSeconds in 1..3600) { message("run.startupBudget") }
		val stopOnEntry = when (values.stopOnEntry) {
			"true" -> true
			"false" -> false
			"inherit" -> null
			else -> error(message("run.invalidEntry"))
		}
		val attach = values.mode == "attach"
		val host = values.host.orEmpty().trim()
		val program = RunProgramPaths.resolve(values, root, cwd)
		if (!attach) require(program != null && Files.isRegularFile(program) && program.toString().endsWith(".vs", true)) {
			message("run.programMissing")
		}
		val inherited = SemanticOptions.resolve(VeroScriptProjectSettings.get(project).configuration(), root)
		val overrides = SemanticOptions.resolve(SemanticOptions.parse(values.semantics.orEmpty()), cwd, root)
		if (overrides.has("stdlibPath")) inherited.remove("stdlibSearchPaths")
		if (overrides.has("stdlibSearchPaths")) inherited.remove("stdlibPath")
		overrides.entrySet().forEach { (key, value) -> inherited.add(key, value) }
		val environment = JsonParser.parseString(values.environment.orEmpty().ifBlank { "{}" })
		require(environment.isJsonObject && environment.asJsonObject.entrySet().all {
			it.key.isNotBlank() && !it.key.contains('=') && !it.key.contains('\u0000') &&
				it.value.isJsonPrimitive && it.value.asJsonPrimitive.isString && !it.value.asString.contains('\u0000')
		}) { message("run.invalidEnvironment") }
		val input = values.inputFile.orEmpty().takeIf { it.isNotBlank() }?.let {
			val file = SemanticOptions.absolute(it, cwd, root)
			require(Files.isRegularFile(file)) { message("run.inputMissing", file) }
			file
		}
		if (attach) require(values.workingDirectory.isNullOrBlank() && environment.asJsonObject.size() == 0 && input == null && values.inheritParentEnvironment) {
			message("run.attachOverrides")
		}
		if (attach) require(host.isNotEmpty() && host.none { it.isWhitespace() || it.isISOControl() }) {
			message("run.invalidHost")
		}
		if (values.mode == "check") require(values.arguments.isNullOrBlank()) { message("run.checkArguments") }
		val runtime = if (attach) null
		else RuntimeLayout.resolve(project)
		if (!attach && !inherited.has("stdlibPath") && !inherited.has("stdlibSearchPaths")) {
			inherited.addProperty("stdlibPath", runtime!!.stdlib.toString())
		}
		return ResolvedRun(runtime, program, cwd, ParametersListUtil.parse(values.arguments.orEmpty()),
			environment.asJsonObject.entrySet().associate { it.key to it.value.asString },
			if (attach) overrides
			else inherited, values.mode.orEmpty(), values.backend.orEmpty(), input,
			stopOnEntry, host, values.port, values.maxCallDepth,
			VeroScriptProjectSettings.get(project).locale(), values.overrideArguments,
			values.inheritParentEnvironment, values.launchTimeoutSeconds,
			jitBackendPath = values.jitBackendPath.orEmpty().takeIf { includeJitBackend && values.mode == "run" && it.isNotBlank() }
				?.let { SemanticOptions.absolute(it, cwd, root) })
	}

	override fun getState(executor: Executor, environment: ExecutionEnvironment): RunProfileState = object : CommandLineState(environment) {
		override fun startProcess(): ProcessHandler {
			val run = resolve()
			require(run.mode != "attach") { message("run.attachUseDebug") }
			val command = run.commandLine()
			return KillableColoredProcessHandler(command).apply {
				if (run.mode == "run" && run.input != null) {
					com.intellij.openapi.application.ApplicationManager.getApplication().executeOnPooledThread {
						try { Files.newInputStream(run.input).use { it.copyTo(process.outputStream) } }
						catch (exception: Exception)
						{
							notifyTextAvailable(exception.message.orEmpty() + "\n", com.intellij.execution.process.ProcessOutputTypes.STDERR)
							destroyProcess()
						}
						finally
						{
							runCatching { process.outputStream.close() }
						}
					}
				}
			}
		}
	}
}

/** All paths are absolute here. Resolution preserves the origin of inherited options. */
data class ResolvedRun(
	val runtime: RuntimeLayout?, val program: Path?, val cwd: Path, val arguments: List<String>,
	val environment: Map<String, String>, val semantics: JsonObject, val mode: String,
	val backend: String, val input: Path?, val stopOnEntry: Boolean?, val host: String, val port: Int,
	val maxCallDepth: Int, val locale: String, val overrideArguments: Boolean = false,
	val inheritParentEnvironment: Boolean = true, val launchTimeoutSeconds: Int = 30,
	val attachMode: String = "program", val domainId: String = "", val jitBackendPath: Path? = null
) {
	val isDomainAttach: Boolean get() = mode == "attach" && attachMode == "domain"
	fun commandLine(): GeneralCommandLine {
		val args = mutableListOf<String>()
		if (mode == "check") args += "--check"
		// Package environments select/load a JIT provider before checking the source.
		// Check must opt out without replacing the user's ordinary Run preference.
		val commandBackend = if (mode == "check") "vm"
		else backend
		args += "--backend=$commandBackend"
		// JIT modules belong to ordinary execution, never LSP, Check or the DAP host.
		if (mode == "run") jitBackendPath?.let {
			require(Files.isRegularFile(it) || Files.isDirectory(it)) { message("run.jitMissing", it.toString()) }
			args += "--jit-backend=$it"
		}
		args += listOf("--lang", locale)
		fun paths(name: String, flag: String) {
			semantics.getAsJsonArray(name)?.forEach { args += listOf(flag, it.asString) }
		}
		paths("importPaths", "-I")
		paths("stdlibSearchPaths", "--stdlib-path")
		semantics["stdlibPath"]?.let { args += listOf("--stdlib-path", it.asString) }
		paths("implicitImportPaths", "--implicit-import")
		paths("bindingCatalogPaths", "--binding-catalog")
		paths("defines", "-D")
		semantics.getAsJsonArray("predefinedMacros")?.forEach { args += listOf("--macro", macroArgument(it.asJsonObject)) }
		if (semantics["allowCircularImports"]?.asBoolean == false) args += "--no-circular-imports"
		if (semantics["allowProjectWideAccess"]?.asBoolean == true) args += "--allow-project-wide-access"
		args += requireNotNull(program).toString()
		args += arguments
		return GeneralCommandLine(requireNotNull(runtime).tool("").toString()).withParameters(args)
			.withParentEnvironmentType(if (inheritParentEnvironment) GeneralCommandLine.ParentEnvironmentType.CONSOLE
			else GeneralCommandLine.ParentEnvironmentType.NONE)
			.withWorkDirectory(cwd.toFile()).withEnvironment(environment).withCharset(Charsets.UTF_8)
	}

	fun debugArguments(): Map<String, Any> {
		if (isDomainAttach) return linkedMapOf<String, Any>("attachMode" to "domain").apply {
			if (domainId.isNotBlank()) put("domainId", domainId)
		}
		val result = linkedMapOf<String, Any>()
		if (mode != "attach") {
			result["program"] = requireNotNull(program).toString()
			result["cwd"] = cwd.toString()
			result["args"] = arguments
			result["env"] = environment
			stopOnEntry?.let { result["stopOnEntry"] = it }
			result["backend"] = backend
			if (maxCallDepth > 0) result["maxCallDepth"] = maxCallDepth
			input?.let { result["input"] = Files.readString(it) }
		}
		else
		{
			program?.let { result["program"] = it.toString() }
			if (overrideArguments || arguments.isNotEmpty()) result["args"] = arguments
			stopOnEntry?.let { result["stopOnEntry"] = it }
		}
		val allowed = SemanticOptions.paths + setOf("defines", "predefinedMacros", "allowCircularImports", "allowProjectWideAccess")
		semantics.entrySet().forEach { (key, value) ->
			when (key) {
				"stdlibPath" -> result["stdlibSearchPaths"] = listOf(value.asString)
				in allowed -> result[key] = SemanticOptions.gson.fromJson(value, Any::class.java)
			}
		}
		return result
	}

	companion object {
		fun macroArgument(macro: JsonObject): String {
			val parameters = macro.getAsJsonArray("parameters")?.map { it.asString }.orEmpty().toMutableList()
			if (macro["variadic"]?.asBoolean == true) parameters += "..."
			return macro["name"].asString + "(" + parameters.joinToString(",") + ")=" + macro["body"].asString
		}
	}
}
