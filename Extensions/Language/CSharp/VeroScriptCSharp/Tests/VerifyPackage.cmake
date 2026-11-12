# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)

foreach(_VSC_required IN ITEMS
	VSC_DOTNET
	VSC_PROJECT
	VSC_CONFIGURATION
	VSC_VERSION
	VSC_PACKAGE_DIR
	VSC_WORK_DIR
	VSC_NATIVE_LIBRARY
	VSC_NATIVE_ANNOTATION_LIBRARY
	VSC_STDLIB_DIR)
	if(NOT DEFINED ${_VSC_required} OR "${${_VSC_required}}" STREQUAL "")
		message(FATAL_ERROR "${_VSC_required} is required")
	endif()
endforeach()

if(NOT EXISTS "${VSC_NATIVE_LIBRARY}")
	message(FATAL_ERROR "VeroScriptInterop library does not exist: ${VSC_NATIVE_LIBRARY}")
endif()
if(NOT EXISTS "${VSC_NATIVE_ANNOTATION_LIBRARY}")
	message(FATAL_ERROR "Annotation service test fixture does not exist: ${VSC_NATIVE_ANNOTATION_LIBRARY}")
endif()
if(NOT IS_DIRECTORY "${VSC_STDLIB_DIR}")
	message(FATAL_ERROR "VeroScript standard-library directory does not exist: ${VSC_STDLIB_DIR}")
endif()
set(_VSC_run_arch_args "")
set(_VSC_run_restore_args --no-restore)
if(DEFINED VSC_DOTNET_ARCH AND NOT "${VSC_DOTNET_ARCH}" STREQUAL "")
	list(APPEND _VSC_run_arch_args
		--arch "${VSC_DOTNET_ARCH}" --self-contained true)
	set(_VSC_run_restore_args "")
endif()
cmake_path(GET VSC_NATIVE_LIBRARY PARENT_PATH _VSC_native_directory)

cmake_path(GET VSC_WORK_DIR FILENAME _VSC_work_name)
if(NOT _VSC_work_name STREQUAL "VeroScriptCSharpPackageTest")
	message(FATAL_ERROR
		"VSC_WORK_DIR must end in VeroScriptCSharpPackageTest: ${VSC_WORK_DIR}")
endif()

file(REMOVE_RECURSE "${VSC_WORK_DIR}")
file(MAKE_DIRECTORY "${VSC_PACKAGE_DIR}" "${VSC_WORK_DIR}")

set(_VSC_package "${VSC_PACKAGE_DIR}/VeroScript.CSharp.${VSC_VERSION}.nupkg")
set(_VSC_symbols "${VSC_PACKAGE_DIR}/VeroScript.CSharp.${VSC_VERSION}.snupkg")
file(REMOVE "${_VSC_package}" "${_VSC_symbols}")

set(_VSC_pack_artifacts_args "")
if(DEFINED VSC_ARTIFACTS_DIR AND NOT "${VSC_ARTIFACTS_DIR}" STREQUAL "")
	list(APPEND _VSC_pack_artifacts_args --artifacts-path "${VSC_ARTIFACTS_DIR}")
endif()

execute_process(
	COMMAND "${VSC_DOTNET}" pack "${VSC_PROJECT}"
		--configuration "${VSC_CONFIGURATION}"
		--output "${VSC_PACKAGE_DIR}"
		${_VSC_pack_artifacts_args}
		--nologo
		-p:Version=${VSC_VERSION}
		-p:PackageVersion=${VSC_VERSION}
	RESULT_VARIABLE _VSC_pack_result
	COMMAND_ECHO STDOUT)
if(NOT _VSC_pack_result EQUAL 0)
	message(FATAL_ERROR "Packing VeroScript.CSharp failed with exit code ${_VSC_pack_result}")
endif()

foreach(_VSC_artifact IN ITEMS "${_VSC_package}" "${_VSC_symbols}")
	if(NOT EXISTS "${_VSC_artifact}")
		message(FATAL_ERROR "Expected package artifact was not generated: ${_VSC_artifact}")
	endif()
endforeach()

set(_VSC_symbols_dir "${VSC_WORK_DIR}/symbols")
file(MAKE_DIRECTORY "${_VSC_symbols_dir}")
file(ARCHIVE_EXTRACT INPUT "${_VSC_symbols}" DESTINATION "${_VSC_symbols_dir}")
foreach(_VSC_entry IN ITEMS
	VeroScript.CSharp.nuspec
	lib/net8.0/VeroScriptCSharp.pdb
	lib/net9.0/VeroScriptCSharp.pdb)
	if(NOT EXISTS "${_VSC_symbols_dir}/${_VSC_entry}")
		message(FATAL_ERROR "NuGet symbol package is missing ${_VSC_entry}")
	endif()
endforeach()

set(_VSC_extract_dir "${VSC_WORK_DIR}/package")
file(MAKE_DIRECTORY "${_VSC_extract_dir}")
file(ARCHIVE_EXTRACT INPUT "${_VSC_package}" DESTINATION "${_VSC_extract_dir}")
foreach(_VSC_entry IN ITEMS
	LICENSE
	buildTransitive/VeroScript.CSharp.targets
	VeroScript.CSharp.nuspec
	lib/net8.0/VeroScriptCSharp.dll
	lib/net8.0/VeroScriptCSharp.xml
	lib/net9.0/VeroScriptCSharp.dll
	lib/net9.0/VeroScriptCSharp.xml)
	if(NOT EXISTS "${_VSC_extract_dir}/${_VSC_entry}")
		message(FATAL_ERROR "NuGet package is missing ${_VSC_entry}")
	endif()
endforeach()

file(GLOB_RECURSE _VSC_packaged_entries
	LIST_DIRECTORIES FALSE
	RELATIVE "${_VSC_extract_dir}"
	"${_VSC_extract_dir}/*")
foreach(_VSC_entry IN LISTS _VSC_packaged_entries)
	if(_VSC_entry MATCHES "(^|/)Tests?/"
			OR _VSC_entry MATCHES "\\.(cs|cmake|def)$"
			OR _VSC_entry MATCHES "^runtimes/")
		message(FATAL_ERROR "NuGet package contains an unintended file: ${_VSC_entry}")
	endif()
endforeach()

file(READ "${_VSC_extract_dir}/VeroScript.CSharp.nuspec" _VSC_nuspec)
cmake_path(GET VSC_PROJECT PARENT_PATH _VSC_project_dir)
file(READ "${_VSC_project_dir}/../../../../LICENSE" _VSC_source_license)
file(READ "${_VSC_extract_dir}/LICENSE" _VSC_packaged_license)
if(NOT _VSC_packaged_license STREQUAL _VSC_source_license)
	message(FATAL_ERROR "NuGet license text does not match the project license")
endif()
foreach(_VSC_metadata IN ITEMS
	"<license type=\"expression\">MIT</license>"
	"<repository type=\"git\" url=\"https://github.com/VanstySanic/VeroScript\""
	"<group targetFramework=\"net8.0\" />"
	"<group targetFramework=\"net9.0\" />")
	string(FIND "${_VSC_nuspec}" "${_VSC_metadata}" _VSC_metadata_position)
	if(_VSC_metadata_position EQUAL -1)
		message(FATAL_ERROR "NuGet metadata is missing: ${_VSC_metadata}")
	endif()
endforeach()

set(_VSC_consumer_dir "${VSC_WORK_DIR}/consumer")
file(MAKE_DIRECTORY "${_VSC_consumer_dir}")
configure_file("${_VSC_project_dir}/Tests/NativeLifetimeTests.cs"
	"${_VSC_consumer_dir}/NativeLifetimeTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/VmBackendContractTests.cs"
	"${_VSC_consumer_dir}/VmBackendContractTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/LanguageStateContractTests.cs"
	"${_VSC_consumer_dir}/LanguageStateContractTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/RunModeContractTests.cs"
	"${_VSC_consumer_dir}/RunModeContractTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/JitStatisticsContractTests.cs"
	"${_VSC_consumer_dir}/JitStatisticsContractTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/AnnotationContractTests.cs"
	"${_VSC_consumer_dir}/AnnotationContractTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/NativeSdkContractTests.cs"
	"${_VSC_consumer_dir}/NativeSdkContractTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/ScriptCompileExtensionTests.cs"
	"${_VSC_consumer_dir}/ScriptCompileExtensionTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/ConsoleContractTests.cs"
	"${_VSC_consumer_dir}/ConsoleContractTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/CoreTypeContractTests.cs"
	"${_VSC_consumer_dir}/CoreTypeContractTests.cs" COPYONLY)
configure_file("${_VSC_project_dir}/Tests/ProtocolLifetimeTests.cs"
	"${_VSC_consumer_dir}/ProtocolLifetimeTests.cs" COPYONLY)
file(WRITE "${_VSC_consumer_dir}/PackageConsumer.csproj" "\
<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFrameworks>net8.0;net9.0</TargetFrameworks>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include=\"VeroScript.CSharp\" Version=\"${VSC_VERSION}\" />
  </ItemGroup>
</Project>
")
file(WRITE "${_VSC_consumer_dir}/Program.cs" [=[
using System.Collections.Concurrent;
using VeroScript;

if (VeroScriptRuntime.AbiVersion != VeroScriptRuntime.SupportedAbiVersion)
    throw new InvalidOperationException("Unexpected VeroScript Interop ABI version.");
NativeLifetimeTests.Run();
VmBackendContractTests.Run();
LanguageStateContractTests.Run();
RunModeContractTests.Run();
JitStatisticsContractTests.Run();
AnnotationContractTests.Run();
NativeSdkContractTests.Run();
ScriptCompileExtensionTests.Run();
ConsoleContractTests.Run();
CoreTypeContractTests.Run();
ProtocolLifetimeTests.Run();
using (var memorySession = new VeroScriptSession())
{
    memorySession.SetMemoryLimits(new(64, 128));
    var memory = memorySession.Memory;
    using var buffer = memory.CreateBuffer(8);
    using var scalar = new VeroScriptValue();
    scalar.Set(ulong.MaxValue);
    memory.Write(buffer, 0, VeroScriptNativeTypeKind.UInt64, scalar, VeroScriptByteOrder.Big);
    using var decoded = memory.Read(buffer, 0, VeroScriptNativeTypeKind.UInt64, VeroScriptByteOrder.Big);
    byte[] managed = [1, 2, 3];
    using var external = memory.WrapExternal(managed);
    using var lease = memory.AcquireLease(external, true);
    lease.WritableBytes[0] = 42;
    if (decoded.GetUInt64() != ulong.MaxValue || managed[0] != 42 || memory.Stats.BorrowedBytes != 3)
    {
        throw new InvalidOperationException("Packaged memory interop failed.");
    }
    memorySession.Dispose();
    if (lease.Bytes[0] != 42)
    {
        throw new InvalidOperationException("Packaged memory lease did not survive session disposal.");
    }
}
if (!VeroScriptRuntime.Capabilities.HasFlag(VeroScriptCapabilities.SessionJitCache))
    throw new InvalidOperationException("Packaged runtime lacks session JIT-cache support.");
if (VeroScriptRuntime.Translate("opaque\0message") != "opaque\0message"
    || VeroScriptRuntime.Translate(string.Empty) != string.Empty)
{
    throw new InvalidOperationException("Packaged diagnostic translation corrupted its input.");
}

using (var defaultSession = new VeroScriptSession())
{
    if (defaultSession.GetJitBackend() != VeroScriptJitBackend.None
        || defaultSession.GetJitBackendInfo() is not { Id: "none", Target: "interpreter",
            CapabilityFlags: VeroScriptJitBackendCapabilities.None, NativeCode: false })
    {
        throw new InvalidOperationException("Packaged session selected an implicit backend.");
    }
    if (!defaultSession.LoadSource("void main(){}", "package-default-backend.vs").Success
        || !defaultSession.Run().Success)
    {
        throw new InvalidOperationException("Packaged default Mixed execution failed without a backend.");
    }
    defaultSession.ApplyConfiguration(new VeroScriptConfiguration
    {
        ExecutionBackend = VeroScriptExecutionBackend.Jit
    });
    if (defaultSession.Run().Success)
    {
        throw new InvalidOperationException("Packaged forced JIT executed without a backend.");
    }
}

using (var value = new VeroScriptValue())
{
    value.Set(42);
    if (value.GetInt32() != 42)
        throw new InvalidOperationException("Packaged value round trip failed.");
}

string? jitBackend = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
using (var callableSession = new VeroScriptSession())
{
    VeroScriptCallable? retained = null;
    Task? disposal = null;
    bool disposedInCallback = false;
    callableSession.RegisterFunction("dispose_retained", VeroScriptNativeType.Void, [], (_, _) =>
    {
        disposal = Task.Run(() => retained!.Dispose());
        disposedInCallback = disposal.Wait(TimeSpan.FromSeconds(5));
    });
    if (!callableSession.LoadSource(
        "int invoke(){dispose_retained();return 42;}function()->int retain(){return invoke;}void main(){}",
        "package-callable-dispose.vs").Success)
    {
        throw new InvalidOperationException("Packaged retained-callable source failed.");
    }
    var (function, obtained) = callableSession.CallFunction("retain");
    using (function)
    {
        if (!obtained.Success) throw new InvalidOperationException("Packaged callable retrieval failed.");
        retained = callableSession.RetainCallable(function);
    }
    using (retained)
    {
        var (value, invoked) = retained.Invoke();
        using (value)
        {
            if (disposal is null || !disposal.Wait(TimeSpan.FromSeconds(5))
                || !disposedInCallback || !invoked.Success || value.GetInt32() != 42 || retained.IsValid)
            {
                throw new InvalidOperationException("Packaged callable disposal blocked or invalidated an active call.");
            }
        }
    }
}
if (!string.IsNullOrEmpty(jitBackend))
{
    using var jitSession = new VeroScriptSession();
    if (jitSession.GetJitBackendInfo().CapabilityFlags != VeroScriptJitBackendCapabilities.None)
    {
        throw new InvalidOperationException("Packaged session selected an implicit backend.");
    }
    jitSession.SetJitBackend(jitBackend);
    VeroScriptJitBackendInfo backendInfo = jitSession.GetJitBackendInfo();
    if (backendInfo.Id == "none"
        || backendInfo.NativeCode != backendInfo.CapabilityFlags.HasFlag(VeroScriptJitBackendCapabilities.NativeCode))
    {
        throw new InvalidOperationException("Packaged backend capabilities did not round trip.");
    }
    jitSession.ApplyConfiguration(new VeroScriptConfiguration
    {
        ExecutionBackend = VeroScriptExecutionBackend.Jit
    });
    var policy = new VeroScriptJitPolicy(1, 64, 64, 64);
    if (!policy.ReuseSessionCode)
        throw new InvalidOperationException("Packaged JIT policy has the wrong reuse default.");
    jitSession.SetJitPolicy(policy);
    var limits = new VeroScriptJitCacheLimits(4UL * 1024 * 1024, 16UL * 1024 * 1024);
    jitSession.SetJitCacheLimits(limits);
    if (jitSession.GetJitCacheLimits() != limits)
        throw new InvalidOperationException("Packaged JIT cache limits did not round trip.");
    VeroScriptRunResult loaded = jitSession.LoadSource(
        "int value(){return 42;}void main(){}", "package-jit-cache.vs");
    if (!loaded.Success)
        throw new InvalidOperationException("Packaged JIT cache source failed.");
    var (coldValue, coldCall) = jitSession.CallFunction("value");
    using (coldValue)
    {
        if (!coldCall.Success || coldValue.GetInt32() != 42
            || jitSession.GetLastJitStats().CompileAttempts == 0)
            throw new InvalidOperationException("Packaged cold JIT call failed.");
    }
    VeroScriptJitCacheStats cache = jitSession.GetJitCacheStats();
    var (warmValue, warmCall) = jitSession.CallFunction("value");
    using (warmValue)
    {
        if (!warmCall.Success || warmValue.GetInt32() != 42
            || jitSession.GetLastJitStats().CompileAttempts != 0
            || cache.CompileAttempts == 0 || cache.RetainedPayloadBytes == 0)
            throw new InvalidOperationException("Packaged warm JIT reuse failed.");
    }
}

string standardLibrary = Environment.GetEnvironmentVariable("VEROSCRIPT_STDLIB_DIR")
    ?? throw new InvalidOperationException("VEROSCRIPT_STDLIB_DIR is required.");
int ownerThread = Environment.CurrentManagedThreadId;
int callbackCalls = 0;
int callbackRanOffOwner = 0;
var output = new ConcurrentQueue<string>();
using (var session = new VeroScriptSession(new VeroScriptSessionOptions { Print = output.Enqueue }))
{
    session.ApplyConfiguration(new VeroScriptConfiguration
    {
        ExecutionBackend = VeroScriptExecutionBackend.Vm,
        StandardLibrarySearchPaths = [standardLibrary]
    });
    session.RegisterFunction("package_worker_probe", VeroScriptNativeType.Void, [], (_, _) =>
    {
        if (Environment.CurrentManagedThreadId != ownerThread)
            Volatile.Write(ref callbackRanOffOwner, 1);
        Interlocked.Increment(ref callbackCalls);
    });
    VeroScriptRunResult loaded = session.LoadSource("""
        import "concurrent.vs";
        export void package_worker(channel<int> result)
        {
            package_worker_probe();
            result.send(42);
        }
        void main()
        {
            task<int> work = std::concurrent::spawn_task<int>("package_worker");
            print(work.await());
        }
        """, "package-native-aot-concurrency.vs");
    if (!loaded.Success)
        throw new InvalidOperationException("Packaged concurrency source failed: "
            + string.Join(" | ", session.GetDiagnostics().Select(item => item.Message)));
    VeroScriptRunResult run = session.Run();
    if (!run.Success)
        throw new InvalidOperationException("Packaged concurrency execution failed: "
            + string.Join(" | ", session.GetDiagnostics().Select(item => item.Message)));
}
if (!output.SequenceEqual(["42"]) || Volatile.Read(ref callbackCalls) != 1
    || Volatile.Read(ref callbackRanOffOwner) != 1)
    throw new InvalidOperationException("Packaged VM worker callback contract failed.");

Console.WriteLine("VeroScript.CSharp package consumer passed.");
]=])

set(_VSC_nuget_config "${_VSC_consumer_dir}/NuGet.Config")
set(_VSC_package_source "${VSC_PACKAGE_DIR}")
string(REPLACE "&" "&amp;" _VSC_package_source "${_VSC_package_source}")
string(REPLACE "\"" "&quot;" _VSC_package_source "${_VSC_package_source}")
string(REPLACE "<" "&lt;" _VSC_package_source "${_VSC_package_source}")
string(REPLACE ">" "&gt;" _VSC_package_source "${_VSC_package_source}")
file(WRITE "${_VSC_nuget_config}"
	"<configuration><packageSources><clear/>"
	"<add key=\"local\" value=\"${_VSC_package_source}\"/>"
	"<add key=\"nuget.org\" value=\"https://api.nuget.org/v3/index.json\"/>"
	"</packageSources><packageSourceMapping>"
	"<packageSource key=\"local\"><package pattern=\"VeroScript.CSharp\"/></packageSource>"
	"<packageSource key=\"nuget.org\"><package pattern=\"*\"/></packageSource>"
	"</packageSourceMapping></configuration>")

execute_process(
	COMMAND "${VSC_DOTNET}" restore "${_VSC_consumer_dir}/PackageConsumer.csproj"
		--configfile "${_VSC_nuget_config}"
		--packages "${VSC_WORK_DIR}/packages"
		--force
		--no-cache
		--nologo
	RESULT_VARIABLE _VSC_restore_result
	COMMAND_ECHO STDOUT)
if(NOT _VSC_restore_result EQUAL 0)
	message(FATAL_ERROR "Restoring the package consumer failed with exit code ${_VSC_restore_result}")
endif()

foreach(_VSC_consumer_tfm IN ITEMS net8.0 net9.0)
	# Resolve real managed package references and exercise the packaged iOS
	# guard without invoking native/workload tasks. This is a host contract check.
	execute_process(
		COMMAND "${VSC_DOTNET}" msbuild "${_VSC_consumer_dir}/PackageConsumer.csproj"
			-nologo -verbosity:quiet -target:ResolveReferences
			-p:TargetFramework=${_VSC_consumer_tfm}
			-p:_VeroScriptMobilePlatform=ios
			-p:ResolveReferencesDependsOn=ResolveAssemblyReferences
			-p:ResolveAssemblyReferencesDependsOn=ResolvePackageDependenciesForBuild
		RESULT_VARIABLE _VSC_ios_guard_result
		OUTPUT_VARIABLE _VSC_ios_guard_output
		ERROR_VARIABLE _VSC_ios_guard_error)
	if(_VSC_ios_guard_result EQUAL 0 OR
			NOT _VSC_ios_guard_output MATCHES "iOS-specific VeroScriptCSharp assembly")
		message(FATAL_ERROR
			"The packaged iOS guard did not reject ${_VSC_consumer_tfm}: "
			"${_VSC_ios_guard_output}${_VSC_ios_guard_error}")
	endif()
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E env
			"DOTNET_CLI_TELEMETRY_OPTOUT=1"
			"DOTNET_TieredCompilation=0"
			"NUGET_PACKAGES=${VSC_WORK_DIR}/packages"
			"VEROSCRIPT_INTEROP_LIBRARY=${VSC_NATIVE_LIBRARY}"
			"VEROSCRIPT_TEST_ANNOTATION_SERVICE_LIBRARY=${VSC_NATIVE_ANNOTATION_LIBRARY}"
			"VEROSCRIPT_STDLIB_DIR=${VSC_STDLIB_DIR}"
			"${VSC_DOTNET}" run
				--project "${_VSC_consumer_dir}/PackageConsumer.csproj"
				--configuration Release
				--framework "${_VSC_consumer_tfm}"
				${_VSC_run_restore_args}
				${_VSC_run_arch_args}
				--nologo
		RESULT_VARIABLE _VSC_run_result
		WORKING_DIRECTORY "${_VSC_native_directory}"
		COMMAND_ECHO STDOUT)
	if(NOT _VSC_run_result EQUAL 0)
		message(FATAL_ERROR
			"Running the ${_VSC_consumer_tfm} package consumer failed with exit code ${_VSC_run_result}")
	endif()
endforeach()

if(DEFINED VSC_NATIVE_AOT_RID AND NOT "${VSC_NATIVE_AOT_RID}" STREQUAL "")
	set(_VSC_native_packages "${VSC_WORK_DIR}/packages")
	# .NET 9 NativeAOT passes Unix framework-library paths to the linker without
	# quoting them. Keep its isolated package cache outside a spaced build path.
	if(UNIX AND _VSC_native_packages MATCHES "[ \t]")
		set(_VSC_temp_root "$ENV{TMPDIR}")
		if(NOT IS_ABSOLUTE "${_VSC_temp_root}" OR NOT IS_DIRECTORY "${_VSC_temp_root}"
				OR _VSC_temp_root MATCHES "[ \t]")
			set(_VSC_temp_root "/tmp")
		endif()
		string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _VSC_temp_identity)
		set(_VSC_native_temp "${_VSC_temp_root}/veroscript-package-${_VSC_temp_identity}")
		if(EXISTS "${_VSC_native_temp}")
			message(FATAL_ERROR "Temporary package cache already exists: ${_VSC_native_temp}")
		endif()
		set(_VSC_native_packages "${_VSC_native_temp}/packages")
		file(MAKE_DIRECTORY "${_VSC_native_packages}")
	endif()
	execute_process(
		COMMAND "${VSC_DOTNET}" restore "${_VSC_consumer_dir}/PackageConsumer.csproj"
			--runtime "${VSC_NATIVE_AOT_RID}"
			-p:PublishAot=true
			--configfile "${_VSC_nuget_config}"
			--packages "${_VSC_native_packages}"
			--force
			--no-cache
			--nologo
		RESULT_VARIABLE _VSC_native_restore_result
		COMMAND_ECHO STDOUT)
	if(NOT _VSC_native_restore_result EQUAL 0)
		if(DEFINED _VSC_native_temp)
			file(REMOVE_RECURSE "${_VSC_native_temp}")
		endif()
		message(FATAL_ERROR
			"Restoring the NativeAOT package consumer failed with exit code ${_VSC_native_restore_result}")
	endif()

	set(_VSC_native_output "${VSC_WORK_DIR}/native-aot")
	execute_process(
		COMMAND "${VSC_DOTNET}" publish "${_VSC_consumer_dir}/PackageConsumer.csproj"
			--configuration Release
			--framework net9.0
			--runtime "${VSC_NATIVE_AOT_RID}"
			--self-contained true
			-p:PublishAot=true
			--no-restore
			--output "${_VSC_native_output}"
			--nologo
		RESULT_VARIABLE _VSC_native_publish_result
		COMMAND_ECHO STDOUT)
	if(DEFINED _VSC_native_temp)
		file(REMOVE_RECURSE "${_VSC_native_temp}")
	endif()
	if(NOT _VSC_native_publish_result EQUAL 0)
		message(FATAL_ERROR
			"Publishing the packaged SDK with NativeAOT failed with exit code ${_VSC_native_publish_result}")
	endif()
	if(WIN32)
		set(_VSC_native_executable "${_VSC_native_output}/PackageConsumer.exe")
	else()
		set(_VSC_native_executable "${_VSC_native_output}/PackageConsumer")
	endif()
	if(NOT EXISTS "${_VSC_native_executable}")
		message(FATAL_ERROR "NativeAOT package consumer was not generated: ${_VSC_native_executable}")
	endif()
	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E env
			"VEROSCRIPT_INTEROP_LIBRARY=${VSC_NATIVE_LIBRARY}"
			"VEROSCRIPT_TEST_ANNOTATION_SERVICE_LIBRARY=${VSC_NATIVE_ANNOTATION_LIBRARY}"
			"VEROSCRIPT_STDLIB_DIR=${VSC_STDLIB_DIR}"
			"${_VSC_native_executable}"
		RESULT_VARIABLE _VSC_native_run_result
		COMMAND_ECHO STDOUT)
	if(NOT _VSC_native_run_result EQUAL 0)
		message(FATAL_ERROR
			"Running the packaged NativeAOT consumer failed with exit code ${_VSC_native_run_result}")
	endif()
endif()
