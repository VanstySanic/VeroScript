// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using VeroScript;

internal static unsafe class NativeSdkContractTests
{
	internal static void Run()
	{
		CheckConfigurationDisposal();
		string path = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_ANNOTATION_SERVICE_LIBRARY") ?? throw new InvalidOperationException("Native SDK test fixture is required.");
		nint library = NativeLibrary.Load(path);
		try
		{
			CheckProcessing(library);
			CheckEnvironment(library);
			CheckRejectedConfiguration(library);
		}
		finally
		{
			NativeLibrary.Free(library);
		}
	}

	internal static void CheckConfigurationDisposal()
	{
		foreach (bool selectEnvironment in new[]
		{
			false,
			true
		}

		)
		{
			for (int input = 0; input < 4; ++input)
			{
				using var session = new VeroScriptSession();
				VeroScriptConfiguration configuration = input switch
				{
					0 => new()
					{
						ImportPaths = new DisposingList<string>(session)
					},
					1 => new()
					{
						SourceOverlays = new DisposingList<VeroScriptSourceOverlay>(session)
					},
					2 => new()
					{
						PredefinedMacros = new DisposingList<VeroScriptPredefinedMacro>(session)
					},
					_ => new()
					{
						PredefinedMacros = [new("DISPOSAL", "", new DisposingList<string>(session))]
					}
				};
				ExpectDisposed(() =>
				{
					if (selectEnvironment)
					{
						session.ApplyConfiguration(configuration, 0);
					}
					else
					{
						session.ApplyConfiguration(configuration);
					}
				});
			}
		}
	}

	private sealed class DisposingList<T>(VeroScriptSession session) : IReadOnlyList<T>
	{
		public int Count
		{
			get
			{
				session.Dispose();
				return 0;
			}
		}

		public T this[int index] => throw new ArgumentOutOfRangeException(nameof(index));
		public IEnumerator<T> GetEnumerator()
		{
			session.Dispose();
			return ((IEnumerable<T>)Array.Empty<T>()).GetEnumerator();
		}

		System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private static void CheckRejectedConfiguration(nint library)
	{
		var create = (delegate* unmanaged[Cdecl]<uint, nint*, uint>)NativeLibrary.GetExport(library, "vsc_test_module_environment_create");
		var release = (delegate* unmanaged[Cdecl]<nint, void>)NativeLibrary.GetExport(library, "vsc_test_module_environment_release");
		foreach (bool enabled in new[] { false, true })
		{
			using var session = new VeroScriptSession();
			session.ApplyConfiguration(new()
			{
				ExecutionBackend = VeroScriptExecutionBackend.Vm,
				EnableExecutionControl = enabled
			});
			var replacement = new VeroScriptConfiguration
			{
				ExecutionBackend = VeroScriptExecutionBackend.Vm,
				EnableExecutionControl = !enabled
			};
			nint environment = 0;
			Require(create(2, &environment) == 0 && environment != 0, "create rejecting VM resolver");
			bool rejected = false;
			try
			{
				try
				{
					session.ApplyConfiguration(replacement, environment);
				}
				catch (VeroScriptException exception) when (exception.Status == VeroScriptStatus.EngineError)
				{
					rejected = exception.Message.Contains("C# test VM resolver rejected configuration", StringComparison.Ordinal);
				}
			}
			finally
			{
				release(environment);
			}
			Require(rejected, "configuration rejection reaches the managed caller with its reason");
			RequireCancellationPolicy(session, enabled);
			session.ApplyConfiguration(replacement, 0);
			Require(session.LoadSource("int main(){return 73;}").Success
				&& session.Run() is { Success: true, ExitCode: 73 }, "accepted retry remains executable");
			RequireCancellationPolicy(session, !enabled);
		}
	}

	private static void RequireCancellationPolicy(VeroScriptSession session, bool enabled)
	{
		if (enabled)
		{
			Task.Run(session.RequestCancellation).GetAwaiter().GetResult();
			return;
		}
		string previousError = session.GetLastError();
		foreach (bool workerThread in new[] { false, true })
		{
			bool rejected = false;
			try
			{
				if (workerThread)
				{
					Task.Run(session.RequestCancellation).GetAwaiter().GetResult();
				}
				else
				{
					session.RequestCancellation();
				}
			}
			catch (VeroScriptException exception) when (exception.Status == VeroScriptStatus.InvalidArgument)
			{
				Require(exception.Message.Contains("request cancellation", StringComparison.Ordinal)
					&& !exception.Message.Contains("C# test VM resolver rejected configuration", StringComparison.Ordinal),
					"cancellation reports its own failure instead of a stale native error: " + exception.Message);
				rejected = true;
			}
			Require(rejected, "cancellation follows the accepted configuration");
			Require(session.GetLastError() == previousError, "cancellation preserves the creator-thread native error");
		}
	}

	private static void CheckProcessing(nint library)
	{
		Require((VeroScriptRuntime.CompileCapabilities & VeroScriptCompileCapabilities.GeneralProcessing) != 0, "runtime advertises general processing");
		var create = (delegate* unmanaged[Cdecl]<nint*, uint> )NativeLibrary.GetExport(library, "vsc_test_compile_processing_create");
		var release = (delegate* unmanaged[Cdecl]<nint, void> )NativeLibrary.GetExport(library, "vsc_test_compile_processing_release");
		using var session = new VeroScriptSession();
		nint service = 0;
		Require(create(&service) == 0 && service != 0, "create general processing service");
		try
		{
			nint borrowedService = service;
			ExpectWrongThread(() => session.SetCompileProcessingService(borrowedService));
			session.SetCompileProcessingService(service);
		}
		finally
		{
			release(service);
		}

		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		Require(session.LoadSource("int main(){return managed_generated();}").Success, "session retains the service across handle release and configuration: " + session.GetLastError());
		Require(session.Run()is { Success: true, ExitCode: 42 }, "general processor output executes");
		bool busyRejected = false;
		session.RegisterFunction("host_clear_processing", VeroScriptNativeType.Void, [], (_, _) =>
		{
			try
			{
				session.SetCompileProcessingService(0);
			}
			catch (VeroScriptException exception)when (exception.Status == VeroScriptStatus.EngineError)
			{
				busyRejected = true;
			}
		});
		Require(session.LoadSource("void main(){host_clear_processing();}").Success && session.Run().Success && busyRejected, "active callbacks cannot replace the service");
		session.Clear();
		session.SetCompileProcessingService(0);
		Require(!session.LoadSource("int main(){return managed_generated();}").Success, "clearing removes generated declarations");
		session.Dispose();
		ExpectDisposed(() => session.SetCompileProcessingService(0));
	}

	private static void CheckEnvironment(nint library)
	{
		Require((VeroScriptRuntime.Capabilities & VeroScriptCapabilities.ModuleEnvironment) != 0, "runtime advertises module environments");
		var create = (delegate* unmanaged[Cdecl]<uint, nint*, uint> )NativeLibrary.GetExport(library, "vsc_test_module_environment_create");
		var release = (delegate* unmanaged[Cdecl]<nint, void> )NativeLibrary.GetExport(library, "vsc_test_module_environment_release");
		var releases = (delegate* unmanaged[Cdecl]<uint> )NativeLibrary.GetExport(library, "vsc_test_module_environment_releases");
		uint before = releases();
		using var session = new VeroScriptSession();
		var configuration = new VeroScriptConfiguration
		{
			ExecutionBackend = VeroScriptExecutionBackend.Vm
		};
		nint environment = 0;
		Require(create(0, &environment) == 0 && environment != 0, "create immutable environment");
		try
		{
			nint borrowedEnvironment = environment;
			ExpectWrongThread(() => session.ApplyConfiguration(configuration, borrowedEnvironment));
			session.ApplyConfiguration(configuration, environment);
		}
		finally
		{
			release(environment);
		}

		Require(releases() == before, "session retains the released environment handle's snapshot");
		session.ApplyConfiguration(configuration);
		Require(session.LoadFile("veroscript-module:/app/main.vs").Success, "ordinary configuration preserves virtual source resolution: " + session.GetLastError());
		Require(session.Run()is { Success: true, ExitCode: 42 }, "environment modules execute");
		bool loadedRejected = false;
		try
		{
			session.ApplyConfiguration(configuration, 0);
		}
		catch (VeroScriptException exception)when (exception.Status == VeroScriptStatus.EngineError)
		{
			loadedRejected = true;
		}

		Require(loadedRejected && releases() == before, "loaded programs prevent snapshot replacement");
		session.Clear();
		Require(releases() == before, "clearing the program preserves its environment");
		session.ApplyConfiguration(configuration, 0);
		Require(releases() == before + 1, "explicit removal releases the final snapshot owner");
		Require(session.LoadSource("int main(){return 7;}").Success && session.Run()is { Success: true, ExitCode: 7 }, "ordinary execution works after removal");
		session.Clear();
		session.RegisterFunction("local_answer", VeroScriptNativeType.Int32, [], (_, result) => result.Set(7));
		session.ApplyConfiguration(new() { EnableExecutionControl = true });
		Require(create(1, &environment) == 0 && environment != 0, "create environment with a published catalog");
		bool conflictRejected = false;
		try
		{
			try
			{
				session.ApplyConfiguration(configuration, environment);
			}
			catch (VeroScriptException exception)when (exception.Status == VeroScriptStatus.EngineError)
			{
				conflictRejected = true;
			}
		}
		finally
		{
			release(environment);
		}

		Require(conflictRejected, "published catalog cannot replace existing local bindings");
		session.RequestCancellation(); // Rejected configuration must preserve the installed control.
		session.ApplyConfiguration(configuration);
		Require(session.LoadSource("int main(){return local_answer();}").Success && session.Run()is { Success: true, ExitCode: 7 }, "rejected snapshot keeps the local binding usable");
		session.Dispose();
		ExpectDisposed(() => session.ApplyConfiguration(configuration, 0));
		uint beforeCatalog = releases();
		using var catalogSession = new VeroScriptSession();
		Require(create(1, &environment) == 0 && environment != 0, "create environment-owned catalog");
		try
		{
			catalogSession.ApplyConfiguration(configuration, environment);
		}
		finally
		{
			release(environment);
		}

		bool registrationRejected = false;
		try
		{
			catalogSession.RegisterFunction("forbidden", VeroScriptNativeType.Void, [], (_, _) =>
			{
			});
		}
		catch (VeroScriptException exception)when (exception.Status == VeroScriptStatus.EngineError)
		{
			registrationRejected = true;
		}

		Require(registrationRejected && catalogSession.LoadFile("veroscript-module:/app/main.vs").Success && catalogSession.Run()is { Success: true, ExitCode: 42 }, "published bindings reject local mutation while keeping the snapshot usable");
		catalogSession.Dispose();
		Require(releases() == beforeCatalog + 1, "session disposal releases the final snapshot owner");
	}

	private static void ExpectWrongThread(Action action) => Task.Run(() =>
	{
		try
		{
			action();
		}
		catch (VeroScriptException exception)when (exception.Status == VeroScriptStatus.WrongThread)
		{
			return;
		}

		throw new InvalidOperationException("Native SDK operation accepted a foreign thread.");
	}).GetAwaiter().GetResult();
	private static void ExpectDisposed(Action action)
	{
		try
		{
			action();
		}
		catch (ObjectDisposedException)
		{
			return;
		}

		throw new InvalidOperationException("Native SDK operation accepted a disposed session.");
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Native SDK contract: " + message);
		}
	}
}
