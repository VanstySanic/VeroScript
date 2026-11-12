// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using System.Text.Json;
using VeroScript;

internal static unsafe class AnnotationContractTests
{
	internal static void Run()
	{
		CheckNamePolicy();
		Require((VeroScriptRuntime.CompileCapabilities & VeroScriptCompileCapabilities.AnnotationProcessing) != 0,
			"runtime advertises annotation processing independently of execution capabilities");
		string? path = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_ANNOTATION_SERVICE_LIBRARY");
		if (string.IsNullOrEmpty(path))
		{
			throw new InvalidOperationException("Annotation service test fixture is required.");
		}
		nint library = NativeLibrary.Load(path);
		try
		{
			var create = (delegate* unmanaged[Cdecl]<uint, nint*, uint>)
				NativeLibrary.GetExport(library, "vsc_test_annotation_create");
			var release = (delegate* unmanaged[Cdecl]<nint, void>)
				NativeLibrary.GetExport(library, "vsc_test_annotation_release");
			foreach (VeroScriptDiagnosticSeverity severity in Enum.GetValues<VeroScriptDiagnosticSeverity>())
			{
				CheckService(create, release, severity);
			}
		}
		finally
		{
			// All sessions and service handles are gone before processor code is unloaded.
			NativeLibrary.Free(library);
		}
	}

	private static void CheckNamePolicy()
	{
		using var session = new VeroScriptSession();
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		var exact = new VeroScriptAnnotationDefinition("@Strict");
		Require(exact.CaseSensitive, "annotation names default to exact matching");
		session.RegisterAnnotation(exact);
		session.RegisterAnnotation(new("@Flexible", Arguments: [new("Name")]) { CaseSensitive = false });
		string beforeConflict = session.ExportBindingsJson();
		foreach (VeroScriptAnnotationDefinition overlap in new VeroScriptAnnotationDefinition[]
		{
			new("@fLEXIBLE"),
			new("@sTRICT") { CaseSensitive = false }
		})
		{
			bool rejected = false;
			try
			{
				session.RegisterAnnotation(overlap);
			}
			catch (VeroScriptException exception) when (exception.Status == VeroScriptStatus.EngineError)
			{
				rejected = true;
			}
			Require(rejected && session.ExportBindingsJson() == beforeConflict,
				"overlapping exact and folded annotation names are rejected without changing the catalog");
		}
		session.RegisterAnnotation(new("@sTRICT"));
		using (JsonDocument catalog = JsonDocument.Parse(session.ExportBindingsJson()))
		{
			JsonElement[] definitions = catalog.RootElement.GetProperty("annotations").EnumerateArray().ToArray();
			Require(definitions.Single(item => item.GetProperty("name").GetString() == "@Strict")
					.GetProperty("caseSensitive").GetBoolean()
				&& !definitions.Single(item => item.GetProperty("name").GetString() == "@Flexible")
					.GetProperty("caseSensitive").GetBoolean(),
				"catalog export preserves each annotation's name policy");
		}
		const string source = """
            @Strict @fLeXiBlE(Name=3) @STRICT @Unknown class Tagged {};
            void main()
            {
                type<Tagged> t = type<Tagged>();
                assert(t.annotation_count() == 4);
                assert(t.has_annotation("Strict") && !t.has_annotation("strict"));
                assert(t.has_annotation("FLEXIBLE") && t.has_annotation("@flexible"));
                assert(t.has_annotation("Unknown") && !t.has_annotation("unknown"));
                assert(t.annotation(0).definition().case_sensitive());
                annotation_info a = t.annotation(1);
                assert(a.name() == "@fLeXiBlE" && a.matches_name("@FLEXIBLE"));
                assert(!a.matches_name("FLEXIBLE"));
                assert(a.definition().name() == "@Flexible" && !a.definition().case_sensitive());
                assert(len(a.find_arguments("Name")) == 1 && len(a.find_arguments("name")) == 0);
                assert(!t.annotation(2).definition().is_valid());
                assert(t.metadata("Name") == "3" && !t.has_metadata("name"));
            }
            """;
		Require(session.LoadSource(source, "annotation-name-policy.vs").Success,
			"annotation policies compile: " + session.GetLastError());
		Require(session.Run().Success, "annotation policies reach script reflection: " + session.GetLastError());
	}

	private static void CheckService(delegate* unmanaged[Cdecl]<uint, nint*, uint> create,
		delegate* unmanaged[Cdecl]<nint, void> release, VeroScriptDiagnosticSeverity severity)
	{
		var output = new List<string>();
		using var session = new VeroScriptSession(new() { Print = output.Add });
		nint service = 0;
		Require(create((uint)severity, &service) == 0 && service != 0, "native extension creates a service");
		try
		{
			session.SetAnnotationProcessingService(service);
		}
		finally
		{
			release(service);
		}
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		const string source = """
            class Probe { public: @CsGenerate int value = 41; };
            int read() { Probe p = Probe(); return p.generated(); }
            void main() { print(read()); }
            """;
		VeroScriptRunResult loaded = session.LoadSource(source, "annotation-managed.vs");
		Require(loaded.Success == (severity != VeroScriptDiagnosticSeverity.Error),
			"only error diagnostics prevent compilation");
		Require(session.GetDiagnostics().Any(item => item.Severity == severity
			&& item.Message == "C# annotation diagnostic" && item.MessageId == "CSHARP_ANNOTATION_TEST"),
			"managed diagnostics preserve processor severity and identity");
		if (loaded.Success)
		{
			Require(session.Run().Success && output.SequenceEqual(["42"]),
				"session retains the generated method after source service release and configuration changes");
			bool busyRejected = false;
			VeroScriptStatus? callbackStatus = null;
			session.RegisterFunction("host_update", VeroScriptNativeType.Void, [], (_, _) =>
			{
				try
				{
					session.SetAnnotationProcessingService(0);
				}
				catch (VeroScriptException exception)
				{
					callbackStatus = exception.Status;
					// RequireIdleSession reports an engine configuration error
					// when a script callback attempts to replace this service.
					busyRejected = exception.Status == VeroScriptStatus.EngineError;
					if (!busyRejected)
					{
						throw;
					}
				}
			});
			VeroScriptRunResult busyLoad = session.LoadSource(source.Replace("print(read());", "host_update();"), "annotation-busy.vs");
			VeroScriptRunResult busyRun = busyLoad.Success ? session.Run() : default;
			Require(busyLoad.Success && busyRun.Success && busyRejected,
				$"active execution rejects service reconfiguration (load={busyLoad.Success}, run={busyRun.Success}, "
				+ $"callback={callbackStatus}, error={session.GetLastError()})");
		}
		Exception? foreignThread = Task.Run(() =>
		{
			try
			{
				session.SetAnnotationProcessingService(0);
				return null;
			}
			catch (Exception exception)
			{
				return exception;
			}
		}).GetAwaiter().GetResult();
		Require(foreignThread is VeroScriptException { Status: VeroScriptStatus.WrongThread },
			"service configuration stays on the session creator thread");
		session.SetAnnotationProcessingService(0);
		Require(!session.LoadSource(source, "annotation-cleared.vs").Success,
			"clearing the service removes generated declarations from subsequent compilation");
		session.Dispose();
		try
		{
			session.SetAnnotationProcessingService(0);
			throw new InvalidOperationException("Disposed session accepted an annotation service.");
		}
		catch (ObjectDisposedException)
		{
		}
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Annotation contract: " + message);
		}
	}
}
