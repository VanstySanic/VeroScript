// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import com.intellij.execution.process.ProcessHandler
import com.intellij.execution.ui.ConsoleViewContentType
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.Disposable
import com.intellij.openapi.util.Disposer
import com.intellij.openapi.vfs.VfsUtilCore
import com.intellij.openapi.progress.ProcessCanceledException
import com.intellij.openapi.progress.ProgressIndicator
import com.intellij.openapi.progress.ProgressManager
import com.intellij.openapi.progress.Task
import com.intellij.xdebugger.*
import com.intellij.xdebugger.breakpoints.XBreakpointHandler
import com.intellij.xdebugger.breakpoints.XLineBreakpoint
import com.intellij.xdebugger.frame.XSuspendContext
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import com.vanstysanic.veroscript.jetbrains.run.ResolvedRun
import org.eclipse.lsp4j.debug.*
import org.eclipse.lsp4j.debug.services.IDebugProtocolClient
import org.eclipse.lsp4j.debug.services.IDebugProtocolServer
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

class VeroScriptDebugProcess(session: XDebugSession, private val run: ResolvedRun) : XDebugProcess(session), IDebugProtocolClient {
	private val stopped = AtomicBoolean()
	private val initialized = CompletableFuture<Void>()
	private val generation = AtomicLong()
	private val lock = Any()
	private data class BreakpointSpec(val point: XLineBreakpoint<VeroScriptBreakpointProperties>, val line: Int, val condition: String?)
	private val breakpoints = linkedMapOf<String, MutableMap<XLineBreakpoint<VeroScriptBreakpointProperties>, BreakpointSpec>>()
	private val registeredPaths = java.util.IdentityHashMap<XLineBreakpoint<VeroScriptBreakpointProperties>, String>()
	private val dirtyFiles = linkedSetOf<String>()
	private data class BreakpointBinding(val path: String, val spec: BreakpointSpec)
	private val breakpointIds = mutableMapOf<Int, BreakpointBinding>()
	private var breakpointUpdates = CompletableFuture.completedFuture<Void>(null)
	private val lifetime = Disposer.newDisposable("VeroScript debug session")
	internal val hoverHints = DebugHoverHints(session.project, isCurrent = ::isCurrent)
	internal val sources = VeroScriptDebugSources()
	@Volatile private var connection: DapConnection? = null
	@Volatile private var paused = false
	@Volatile private var configured = false
	@Volatile private var exitCode = 0
	@Volatile var capabilities = Capabilities()
		private set
	@Volatile var domainInfo: DomainDebugInfo? = null
		private set
	val canEvaluate: Boolean get() = !run.isDomainAttach || domainInfo?.veroscriptCanEvaluate == true
	val canSetVariable: Boolean get() = if (run.isDomainAttach) domainInfo?.veroscriptCanSetVariable == true
		else capabilities.supportsSetVariable == true
	val server: IDebugProtocolServer get() = requireNotNull(connection) { "Debug adapter is not connected" }.server
	internal val processExit: CompletableFuture<Int>? get() = connection?.processExit
	private val processHandler = object : ProcessHandler() {
		override fun destroyProcessImpl() { stop() }
		override fun detachProcessImpl() { stop() }
		override fun detachIsDefault() = false
		override fun getProcessInput(): java.io.OutputStream? = null
		fun finished() { if (!isProcessTerminated) notifyProcessTerminated(this@VeroScriptDebugProcess.exitCode) }
	}
	private val handler = object : XBreakpointHandler<XLineBreakpoint<VeroScriptBreakpointProperties>>(VeroScriptLineBreakpointType::class.java) {
		override fun registerBreakpoint(breakpoint: XLineBreakpoint<VeroScriptBreakpointProperties>) {
			val path = VfsUtilCore.urlToPath(breakpoint.fileUrl)
			val spec = BreakpointSpec(breakpoint, breakpoint.line + 1, breakpoint.conditionExpression?.expression)
			synchronized(lock) {
				registeredPaths.put(breakpoint, path)?.takeIf { it != path }?.let { previous ->
					breakpoints[previous]?.remove(breakpoint)
					dirtyFiles.add(previous)
				}
				breakpoints.getOrPut(path) { linkedMapOf() }[breakpoint] = spec
				dirtyFiles.add(path)
			}
			if (configured && !paused && !run.isDomainAttach) ui { session.setBreakpointInvalid(breakpoint, message("debug.breakpointQueued")) }
			if (configured && (paused || run.isDomainAttach)) updateBreakpoints().exceptionally { failure ->
				fail(failure)
				null
			}
		}
		override fun unregisterBreakpoint(breakpoint: XLineBreakpoint<VeroScriptBreakpointProperties>, temporary: Boolean) {
			synchronized(lock) {
				val path = registeredPaths.remove(breakpoint) ?: VfsUtilCore.urlToPath(breakpoint.fileUrl)
				breakpoints[path]?.remove(breakpoint)
				dirtyFiles.add(path)
			}
			if (configured && (paused || run.isDomainAttach)) updateBreakpoints().exceptionally { failure ->
				fail(failure)
				null
			}
		}
	}

	init {
		Disposer.register(session.project, lifetime)
		Disposer.register(lifetime, Disposable { stop() })
		Disposer.register(lifetime, hoverHints)
		Disposer.register(lifetime, Disposable { sources.close() })
		session.addSessionListener(object : XDebugSessionListener {
			override fun beforeSessionResume() = hoverHints.clear()
			override fun sessionResumed() = hoverHints.clear()
			override fun sessionStopped() = hoverHints.clear()
			override fun stackFrameChanged() = hoverHints.clear()
		}, lifetime)
	}

	override fun getEditorsProvider() = VeroScriptDebuggerEditorsProvider()
	override fun getBreakpointHandlers() = arrayOf(handler)
	override fun doGetProcessHandler(): ProcessHandler = processHandler

	override fun sessionInitialized() {
		session.setPauseActionSupported(!run.isDomainAttach)
		ProgressManager.getInstance().run(object : Task.Backgroundable(session.project, message("debug.starting"), true) {
			override fun run(indicator: ProgressIndicator) = connect(indicator)
		})
	}

	internal fun connect(indicator: ProgressIndicator) {
		try {
			indicator.checkCanceled()
			val transport = if (run.mode == "attach") DapConnection.attach(run.host, run.port, this@VeroScriptDebugProcess, ::transportClosed, ::stderr, run.isDomainAttach)
				else DapConnection.launch(listOf(requireNotNull(run.runtime).tool("-dap").toString()), run.cwd, this@VeroScriptDebugProcess,
					::transportClosed, ::stderr, run.inheritParentEnvironment)
			connection = transport
			if (stopped.get()) {
				transport.close()
				return
			}
			val arguments = InitializeRequestArguments().apply {
				clientID = "VeroScriptJetBrains"
				adapterID = "veroscript"
				pathFormat = "path"
				linesStartAt1 = true
				columnsStartAt1 = true
				locale = run.locale
				supportsVariableType = true
				supportsVariablePaging = true
				supportsRunInTerminalRequest = false
			}
			val startup = server.initialize(arguments).orTimeout(10, TimeUnit.SECONDS).thenCompose { result ->
				check(!stopped.get()) { message("debug.disconnected") }
				capabilities = result
				val prepare = if (run.isDomainAttach) {
					require((result as? VeroScriptDapCapabilities)?.veroscriptSupportsDomainAttach == true) {
						message("debug.domainUnsupported")
					}
					transport.attachDomain(run.debugArguments()).thenAccept { info ->
						if (stopped.get()) return@thenAccept
						require(!info.veroscriptHostId.isNullOrBlank() && !info.veroscriptDomainId.isNullOrBlank() &&
							(run.domainId.isBlank() || run.domainId == info.veroscriptDomainId)) { message("debug.domainIdentityMismatch") }
						domainInfo = info
						ui {
							session.setPauseActionSupported(info.veroscriptCanPause)
							session.consoleView.print(message("debug.domainConnected", info.veroscriptHostId.orEmpty(),
								info.veroscriptDomainId.orEmpty(), info.veroscriptTargetName.orEmpty()) + "\n" +
								message("debug.domainRevisions", info.veroscriptSourceRevision.orEmpty(), info.veroscriptBindingRevision.orEmpty()) + "\n",
								ConsoleViewContentType.SYSTEM_OUTPUT)
						}
					}
				}
				else if (run.mode == "attach") server.attach(run.debugArguments())
				else server.launch(run.debugArguments())
				prepare.orTimeout(run.launchTimeoutSeconds.toLong(), TimeUnit.SECONDS)
			}.thenCompose { initialized.orTimeout(10, TimeUnit.SECONDS) }
				.thenCompose {
					check(!stopped.get()) { message("debug.disconnected") }
					synchronized(lock) { dirtyFiles.addAll(breakpoints.keys) }
					configureBreakpoints()
				}
			while (true) {
				indicator.checkCanceled()
				try {
					startup.get(100, TimeUnit.MILLISECONDS)
					break
				}
				catch (_: java.util.concurrent.TimeoutException)
				{
					if (stopped.get()) return
				}
			}
		}
		catch (_: ProcessCanceledException)
		{
			stop()
		}
		catch (exception: Throwable)
		{
			fail(exception)
		}
	}

	private fun configureBreakpoints(): CompletableFuture<Void> = updateBreakpoints().thenCompose {
		if (stopped.get()) return@thenCompose CompletableFuture.completedFuture(null)
		val ready = synchronized(lock) {
			dirtyFiles.isEmpty().also { if (it) configured = true }
		}
		if (ready) server.configurationDone(ConfigurationDoneArguments()).orTimeout(10, TimeUnit.SECONDS)
		else configureBreakpoints()
	}

	override fun initialized() { initialized.complete(null) }
	override fun output(arguments: OutputEventArguments) {
		ui { session.consoleView.print(arguments.output,
			if (arguments.category == "stderr") ConsoleViewContentType.ERROR_OUTPUT
			else ConsoleViewContentType.NORMAL_OUTPUT) }
	}
	private fun stderr(text: String) { ui { session.consoleView.print(text, ConsoleViewContentType.ERROR_OUTPUT) } }
	override fun stopped(arguments: StoppedEventArguments) {
		if (stopped.get()) return
		paused = true
		val current = generation.incrementAndGet()
		val thread = arguments.threadId ?: 1
		updateBreakpoints().thenCompose {
			if (!isCurrent(current)) return@thenCompose CompletableFuture.completedFuture(StackTraceResponse())
			server.stackTrace(StackTraceArguments().apply {
				threadId = thread
				startFrame = 0
				levels = 100
			})
				.orTimeout(10, TimeUnit.SECONDS)
		}.thenCompose { frames -> prepareSources(frames, current) }.thenAccept { frames -> ui {
			if (isCurrent(current)) session.positionReached(VeroScriptSuspendContext(this, current, thread, frames))
		} }.exceptionally { failure ->
			// A host call can resume/end/reload before its stack request returns.
			// Fail only the stop that still owns this request.
			if (isCurrent(current)) fail(failure)
			null
		}
	}
	override fun continued(arguments: ContinuedEventArguments) {
		paused = false
		generation.incrementAndGet()
		ui { if (!paused && !stopped.get()) session.sessionResumed() }
	}
	override fun breakpoint(arguments: BreakpointEventArguments) {
		val data = arguments.breakpoint
		val breakpoint = synchronized(lock) { breakpointIds[data.id] } ?: return
		presentBreakpoint(breakpoint.path, breakpoint.spec, data)
	}
	override fun exited(arguments: ExitedEventArguments) {
		exitCode = arguments.exitCode
		ui { session.consoleView.print(message("debug.exited", arguments.exitCode) + "\n",
			if (arguments.exitCode == 0) ConsoleViewContentType.SYSTEM_OUTPUT
			else ConsoleViewContentType.ERROR_OUTPUT) }
	}
	override fun terminated(arguments: TerminatedEventArguments?) { finish() }

	@Synchronized
	private fun updateBreakpoints(): CompletableFuture<Void> {
		val snapshot = synchronized(lock) {
			dirtyFiles.map { it to breakpoints[it].orEmpty().values.toList() }.also { dirtyFiles.clear() }
		}
		breakpointUpdates = breakpointUpdates.thenCompose {
			if (stopped.get()) return@thenCompose CompletableFuture.completedFuture(null)
			val requests = snapshot.map { (path, points) ->
				val supported = points.filter { spec ->
					val allowed = !run.isDomainAttach || domainInfo?.veroscriptCanUseConditionalBreakpoints == true || spec.condition.isNullOrBlank()
					if (!allowed) ui {
						if (!stopped.get() && synchronized(lock) { breakpoints[path]?.get(spec.point) == spec })
							session.setBreakpointInvalid(spec.point, message("debug.domainConditionalUnsupported"))
					}
					allowed
				}
				val args = SetBreakpointsArguments().apply {
					source = Source().apply { this.path = path }
					breakpoints = supported.map { point -> SourceBreakpoint().apply {
						line = point.line
						condition = point.condition
					} }.toTypedArray()
					sourceModified = false
				}
				server.setBreakpoints(args).orTimeout(10, TimeUnit.SECONDS).thenAccept { response ->
					synchronized(lock) {
						breakpointIds.entries.removeIf { (_, binding) -> binding.path == path }
						response.breakpoints.forEachIndexed { index, data ->
							supported.getOrNull(index)?.let { spec ->
								if (breakpoints[path]?.get(spec.point) == spec) {
									data.id?.let { breakpointIds[it] = BreakpointBinding(path, spec) }
									presentBreakpoint(path, spec, data)
								}
							}
						}
					}
				}.exceptionally { failure ->
					if (stopped.get()) return@exceptionally null
					if (!run.isDomainAttach) throw java.util.concurrent.CompletionException(failure)
					val cause = generateSequence(failure) { it.cause }.last()
					points.forEach { spec -> ui {
						if (synchronized(lock) { breakpoints[path]?.get(spec.point) == spec })
							session.setBreakpointInvalid(spec.point, cause.message ?: message("debug.breakpointPending"))
					} }
					report(failure)
					null
				}
			}
			CompletableFuture.allOf(*requests.toTypedArray())
		}
		return breakpointUpdates
	}

	private fun presentBreakpoint(path: String, spec: BreakpointSpec, data: Breakpoint) = ui {
		if (stopped.get() || synchronized(lock) { breakpoints[path]?.get(spec.point) != spec }) return@ui
		val point = spec.point
		if (data.isVerified) {
			session.setBreakpointVerified(point)
			data.line?.takeIf { it != point.line + 1 }?.let {
				session.updateBreakpointPresentation(point, null, message("debug.breakpointMoved", it))
			}
		}
		else session.setBreakpointInvalid(point, data.message ?: message("debug.breakpointPending"))
	}

	fun isCurrent(value: Long) = !stopped.get() && paused && generation.get() == value
	internal fun prepareSources(frames: StackTraceResponse, current: Long): CompletableFuture<StackTraceResponse> {
		if (!isCurrent(current)) return CompletableFuture.completedFuture(frames)
		return sources.prepare(server, frames) { name, failure ->
			val cause = generateSequence(failure) { it.cause }.last()
			ui { if (isCurrent(current)) session.reportError(message("debug.sourceFailed", name, cause.message.orEmpty())) }
		}
	}
	@Synchronized
	private fun command(request: () -> CompletableFuture<*>) {
		if (!configured || !paused || stopped.get()) return
		paused = false
		val current = generation.incrementAndGet()
		updateBreakpoints().thenCompose {
			if (stopped.get() || generation.get() != current) return@thenCompose CompletableFuture.completedFuture(null)
			request().orTimeout(10, TimeUnit.SECONDS)
		}.thenRun { ui { if (!paused && !stopped.get()) session.sessionResumed() } }.exceptionally { failure ->
			// A newer host stop owns the session after this command's generation expires.
			if (generation.get() == current) fail(failure)
			null
		}
	}
	override fun resume(context: XSuspendContext?) = command { server.continue_(ContinueArguments().apply { threadId = 1 }) }
	private fun step(request: () -> CompletableFuture<*>) {
		if (run.isDomainAttach && domainInfo?.veroscriptCanStep != true) {
			ui { session.reportError(message("debug.domainStepUnsupported")) }
			return
		}
		command(request)
	}
	override fun startStepOver(context: XSuspendContext?) = step { server.next(NextArguments().apply { threadId = 1 }) }
	override fun startStepInto(context: XSuspendContext?) = step { server.stepIn(StepInArguments().apply { threadId = 1 }) }
	override fun startStepOut(context: XSuspendContext?) = step { server.stepOut(StepOutArguments().apply { threadId = 1 }) }
	override fun startPausing() {
		if (run.isDomainAttach && domainInfo?.veroscriptCanPause != true) {
			ui { session.reportError(message("debug.domainPauseUnsupported")) }
			return
		}
		val current = generation.get()
		if (configured && !paused && !stopped.get()) server.pause(PauseArguments().apply { threadId = 1 })
			.orTimeout(10, TimeUnit.SECONDS).exceptionally { failure ->
				// The target can stop before its notification arrives. A rejected pause must
				// preserve the connection; transportClosed handles an actual disconnect.
				if (generation.get() == current) report(failure)
				null
			}
	}

	override fun stop() {
		if (!stopped.compareAndSet(false, true)) return
		paused = false
		generation.incrementAndGet()
		val transport = connection
		if (transport == null) finishCleanup()
		else
		{
			val disconnect = runCatching { transport.disconnect(run.mode != "attach") }
				.getOrElse { CompletableFuture.failedFuture(it) }
			disconnect.whenComplete { _, _ ->
				transport.close()
				finishCleanup()
			}
		}
	}

	private fun finish() {
		if (!stopped.compareAndSet(false, true)) return
		generation.incrementAndGet()
		connection?.close()
		finishCleanup()
	}
	private fun transportClosed(failure: Throwable?) {
		if (stopped.get()) return
		fail(failure ?: java.io.IOException(message("debug.disconnected")))
	}
	private fun finishCleanup() {
		processHandler.finished()
		Disposer.dispose(lifetime)
		ui { session.stop() }
	}
	private fun fail(failure: Throwable) {
		if (stopped.get()) return
		if (exitCode == 0) exitCode = 1
		report(failure)
		stop()
	}
	private fun report(failure: Throwable) {
		val cause = generateSequence(failure) { it.cause }.last()
		ui { session.reportError(cause.message ?: cause.javaClass.simpleName) }
	}
	fun ui(action: () -> Unit) {
		ApplicationManager.getApplication().invokeLater({ if (!session.project.isDisposed) action() },
			com.intellij.openapi.application.ModalityState.any())
	}
}
