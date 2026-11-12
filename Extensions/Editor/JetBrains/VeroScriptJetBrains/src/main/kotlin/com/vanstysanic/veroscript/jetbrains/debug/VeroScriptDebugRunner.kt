// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import com.intellij.execution.configurations.RunProfile
import com.intellij.execution.configurations.RunProfileState
import com.intellij.execution.configurations.RunnerSettings
import com.intellij.execution.executors.DefaultDebugExecutor
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.execution.runners.GenericProgramRunner
import com.intellij.openapi.fileEditor.FileDocumentManager
import com.intellij.xdebugger.XDebugProcessStarter
import com.intellij.xdebugger.XDebugSession
import com.intellij.xdebugger.XDebuggerManager
import com.vanstysanic.veroscript.jetbrains.run.VeroScriptRunConfiguration

class VeroScriptDebugRunner : GenericProgramRunner<RunnerSettings>() {
	override fun getRunnerId() = "VeroScriptDebugRunner"
	override fun canRun(executorId: String, profile: RunProfile) = executorId == DefaultDebugExecutor.EXECUTOR_ID &&
		profile is VeroScriptRunConfiguration && profile.options.mode != "check"
	override fun doExecute(state: RunProfileState, environment: ExecutionEnvironment): com.intellij.execution.ui.RunContentDescriptor? {
		val configuration = environment.runProfile as VeroScriptRunConfiguration
		if (configuration.options.mode != "attach" || configuration.options.attachMode != "domain") {
			FileDocumentManager.getInstance().saveAllDocuments()
		}
		val run = configuration.resolve(includeJitBackend = false)
		val result = XDebuggerManager.getInstance(environment.project).newSessionBuilder(object : XDebugProcessStarter() {
			override fun start(session: XDebugSession) = VeroScriptDebugProcess(session, run)
		}).environment(environment).sessionName(environment.runProfile.name).showTab(true).startSession()
		return result.runContentDescriptor
	}
}
