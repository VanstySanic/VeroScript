// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.vanstysanic.veroscript.jetbrains.debug.DapConnection
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptDapCapabilities
import org.eclipse.lsp4j.debug.ConfigurationDoneArguments
import org.eclipse.lsp4j.debug.InitializeRequestArguments
import org.eclipse.lsp4j.debug.services.IDebugProtocolClient
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CompletableFuture
import java.util.concurrent.TimeUnit

/** Runs on Windows and Linux JVMs without the Rider test application or a Runtime. */
class DomainTransportTest {
	@Test fun extendedResponsesSurviveFragmentedTcpAndUtf8AndDisconnectKeepsHostOwnership() {
		DomainDapTestHost(true, "game").use { host ->
			host.fragmentResponses = true
			val initialized = CompletableFuture<Void>()
			val client = object : IDebugProtocolClient {
				override fun initialized() { initialized.complete(null) }
			}
			DapConnection.attach("127.0.0.1", host.port, client, {}, {}, domain = true).use { connection ->
				val capabilities = connection.server.initialize(InitializeRequestArguments()).get(10, TimeUnit.SECONDS)
				assertTrue((capabilities as VeroScriptDapCapabilities).veroscriptSupportsDomainAttach)
				initialized.get(10, TimeUnit.SECONDS)
				val info = connection.attachDomain(mapOf("attachMode" to "domain", "domainId" to "game")).get(10, TimeUnit.SECONDS)
				assertEquals("游戏", info.veroscriptTargetName)
				assertEquals("game", info.veroscriptDomainId)
				assertEquals("source-7", info.veroscriptSourceRevision)
				assertEquals("bindings-3", info.veroscriptBindingRevision)
				assertEquals("/host/main.vs", info.veroscriptSources.single().path)
				assertFalse(info.veroscriptCanSetVariable)
				connection.server.configurationDone(ConfigurationDoneArguments()).get(10, TimeUnit.SECONDS)
				connection.disconnect(false).get(10, TimeUnit.SECONDS)
				assertFalse(host.requests.single { it["command"].asString == "disconnect" }["arguments"].asJsonObject["terminateDebuggee"].asBoolean)
			}
		}
	}

	@Test fun absentAndFalseDomainCapabilitiesAreNotSupported() {
		for (advertise in listOf<Boolean?>(null, false)) DomainDapTestHost(advertise, "game").use { host ->
			DapConnection.attach("127.0.0.1", host.port, object : IDebugProtocolClient {}, {}, {}, domain = true).use { connection ->
				val caps = connection.server.initialize(InitializeRequestArguments()).get(10, TimeUnit.SECONDS)
				assertFalse((caps as VeroScriptDapCapabilities).veroscriptSupportsDomainAttach)
				connection.disconnect(false).get(10, TimeUnit.SECONDS)
			}
		}
	}
}
