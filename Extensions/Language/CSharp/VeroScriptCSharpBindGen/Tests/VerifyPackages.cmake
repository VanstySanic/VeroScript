# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

if(NOT DEFINED VSC_DOTNET OR NOT DEFINED VSC_ROOT OR NOT DEFINED VSC_VERSION
		OR NOT DEFINED VSC_WORK_DIR OR NOT DEFINED VSC_EXECUTABLE)
	message(FATAL_ERROR "VSC_DOTNET, VSC_ROOT, VSC_VERSION, VSC_WORK_DIR, and VSC_EXECUTABLE are required")
endif()
if(NOT EXISTS "${VSC_EXECUTABLE}")
	message(FATAL_ERROR "VeroScript executable does not exist: ${VSC_EXECUTABLE}")
endif()
file(REMOVE_RECURSE "${VSC_WORK_DIR}")
file(MAKE_DIRECTORY "${VSC_WORK_DIR}/packages" "${VSC_WORK_DIR}/consumer" "${VSC_WORK_DIR}/tool")
set(_pack_artifacts_arguments)
if(DEFINED VSC_ARTIFACTS_DIR AND NOT VSC_ARTIFACTS_DIR STREQUAL "")
	list(APPEND _pack_artifacts_arguments --artifacts-path "${VSC_ARTIFACTS_DIR}")
	set(_bindgen_output_directory "${VSC_ARTIFACTS_DIR}/bin/VeroScriptCSharpBindGen/release_$(TargetFramework)")
	set(_transpiler_output_directory "${VSC_ARTIFACTS_DIR}/bin/VeroScriptCSharpTranspiler/release_$(TargetFramework)")
else()
	set(_bindgen_output_directory "${VSC_ROOT}/VeroScriptCSharpBindGen/bin/Release/$(TargetFramework)")
	set(_transpiler_output_directory "${VSC_ROOT}/VeroScriptCSharpTranspiler/bin/Release/$(TargetFramework)")
endif()
foreach(_project IN ITEMS
	"${VSC_ROOT}/VeroScriptCSharpFrontend/VeroScriptCSharpFrontend.csproj"
	"${VSC_ROOT}/VeroScriptCSharpTranspiler/VeroScriptCSharpTranspiler.csproj"
	"${VSC_ROOT}/VeroScriptCSharpTranspiler/Cli/VeroScriptCSharpTranspiler.Cli.csproj"
	"${VSC_ROOT}/VeroScriptCSharpBindGen/VeroScriptCSharpBindGen.csproj"
	"${VSC_ROOT}/VeroScriptCSharpBindGen/Cli/VeroScriptCSharpBindGen.Cli.csproj")
	execute_process(COMMAND "${VSC_DOTNET}" pack "${_project}" --configuration Release
		--output "${VSC_WORK_DIR}/packages" -p:Version=${VSC_VERSION}
		-p:PackageVersion=${VSC_VERSION} ${_pack_artifacts_arguments} --nologo
		RESULT_VARIABLE _pack_result)
	if(NOT _pack_result EQUAL 0)
		message(FATAL_ERROR "Failed to pack ${_project}")
	endif()
endforeach()
foreach(_package IN ITEMS
	VeroScript.CSharpFrontend
	VeroScript.CSharpBindGen
	VeroScript.CSharpBindGen.Tool
	VeroScript.CSharpTranspiler
	VeroScript.CSharpTranspiler.Tool)
	foreach(_extension IN ITEMS nupkg snupkg)
		if(NOT EXISTS "${VSC_WORK_DIR}/packages/${_package}.${VSC_VERSION}.${_extension}")
			message(FATAL_ERROR
				"Expected package artifact was not generated: ${_package}.${VSC_VERSION}.${_extension}")
		endif()
	endforeach()
endforeach()
foreach(_tool_package IN ITEMS
	VeroScript.CSharpBindGen.Tool
	VeroScript.CSharpTranspiler.Tool)
	set(_tool_package_path
		"${VSC_WORK_DIR}/packages/${_tool_package}.${VSC_VERSION}.nupkg")
	set(_tool_package_extract_dir "${VSC_WORK_DIR}/package-content/${_tool_package}")
	file(MAKE_DIRECTORY "${_tool_package_extract_dir}")
	file(ARCHIVE_EXTRACT INPUT "${_tool_package_path}"
		DESTINATION "${_tool_package_extract_dir}")
	foreach(_tool_tfm IN ITEMS net8.0 net9.0)
		foreach(_notice IN ITEMS Roslyn-LICENSE.txt Roslyn-ThirdPartyNotices.rtf)
			file(READ "${VSC_ROOT}/Resources/Licenses/${_notice}" _source_notice)
			file(READ "${_tool_package_extract_dir}/tools/${_tool_tfm}/any/licenses/${_notice}" _packed_notice)
			if(NOT _source_notice STREQUAL _packed_notice)
				message(FATAL_ERROR "Packed tool lost its complete dependency notice: ${_notice}")
			endif()
		endforeach()
		file(GLOB _tool_runtime_configs LIST_DIRECTORIES false
			"${_tool_package_extract_dir}/tools/${_tool_tfm}/any/*.runtimeconfig.json")
		list(LENGTH _tool_runtime_configs _tool_runtime_config_count)
		if(NOT _tool_runtime_config_count EQUAL 1)
			message(FATAL_ERROR
				"Packed tool must contain exactly one ${_tool_tfm} runtime configuration: ${_tool_package_path}")
		endif()
		list(GET _tool_runtime_configs 0 _tool_runtime_config)
		file(READ "${_tool_runtime_config}" _tool_runtime_config_json)
		string(JSON _tool_roll_forward ERROR_VARIABLE _tool_json_error
			GET "${_tool_runtime_config_json}" runtimeOptions rollForward)
		if(NOT _tool_json_error STREQUAL "NOTFOUND"
				OR NOT _tool_roll_forward STREQUAL "Major")
			message(FATAL_ERROR
				"Packed tool must roll forward to a supported later major runtime: ${_tool_runtime_config}")
		endif()
	endforeach()
endforeach()
string(CONFIGURE [=[
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net9.0</TargetFramework>
    <RollForward>Major</RollForward>
    <Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup>
  <ItemGroup>
    <PackageReference Include="VeroScript.CSharpBindGen" Version="@VSC_VERSION@" />
    <PackageReference Include="VeroScript.CSharpTranspiler" Version="@VSC_VERSION@" />
  </ItemGroup>
</Project>
]=] _consumer_project @ONLY)
file(WRITE "${VSC_WORK_DIR}/consumer/Consumer.csproj" "${_consumer_project}")
file(WRITE "${VSC_WORK_DIR}/consumer/Program.cs" [=[
using VeroScript.CSharpBindGen;
using VeroScript.CSharpTranspiler;
var result = CSharpBindGen.GenerateBindingsFromSource("public sealed class Api { public int Add(int x) => x + 1; }",
    bindGenOptions: new CSharpBindGenOptions
    {
        EmitRegistrationSource = false,
        EmitAotBridgeSource = true
    });
if (!result.Succeeded || !result.CatalogJson.Contains("Api", StringComparison.Ordinal)
    || !result.AotBridgeSource.Contains("VeroScriptGeneratedProgram", StringComparison.Ordinal)) return 1;
var abstractHost = CSharpBindGen.GenerateBindingsFromSource(
    "public abstract class AbstractHost { public AbstractHost() {} public abstract int Read(); }",
    bindGenOptions: new() { EmitAotBridgeSource = true });
if (!abstractHost.Succeeded || abstractHost.RegistrationSource.Contains("new global::AbstractHost(", StringComparison.Ordinal)
    || abstractHost.AotBridgeSource.Contains("new global::AbstractHost(", StringComparison.Ordinal)) return 3;
using (var catalog = System.Text.Json.JsonDocument.Parse(abstractHost.CatalogJson))
{
    if (catalog.RootElement.GetProperty("types")[0].GetProperty("members").EnumerateArray()
        .Any(member => member.GetProperty("kind").GetString() == "constructor")) return 4;
}
var nestedGeneric = CSharpBindGen.GenerateBindingsFromSource(
    "[System.AttributeUsage(System.AttributeTargets.Class)] internal sealed class VeroScriptBindingAttribute : System.Attribute {} "
    + "public class Outer<T> { [VeroScriptBinding] public sealed class Inner { public int Value; } }",
    bindGenOptions: new() { RequireBindingAttribute = true, EmitAotBridgeSource = true });
if (nestedGeneric.Succeeded || !nestedGeneric.Diagnostics.Any(diagnostic => diagnostic.Code == "VSCB1001")
    || nestedGeneric.CatalogJson.Length != 0 || nestedGeneric.RegistrationSource.Length != 0
    || nestedGeneric.AotBridgeSource.Length != 0) return 5;
var convertedReference = CSharpBindGen.GenerateBindingsFromSource(
    "public sealed class RefHost { public static void Update(ref string value) {} }",
    bindGenOptions: new() { EmitRegistrationSource = false, EmitAotBridgeSource = true });
if (convertedReference.Succeeded || !convertedReference.Diagnostics.Any(diagnostic => diagnostic.Code == "VSCB3007")
    || convertedReference.CatalogJson.Length != 0 || convertedReference.AotBridgeSource.Length != 0) return 6;
var transpiled = CSharpTranspiler.TranspileSource(
    "public static class Program { public static int Main() => 0; }", options: new()
    {
        ValidateGeneratedSource = false
    });
if (!transpiled.Succeeded || !transpiled.Source.Contains("int main()", StringComparison.Ordinal)) return 2;
Console.WriteLine("C# tool package consumer passed.");
return 0;
]=])

file(MAKE_DIRECTORY "${VSC_WORK_DIR}/sdk-consumer")
string(CONFIGURE [=[
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><OutputType>Exe</OutputType><TargetFrameworks>net8.0;net9.0</TargetFrameworks>
    <RollForward>Major</RollForward>
    <Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup>
  <ItemGroup>
    <Reference Include="VeroScriptCSharpFrontend">
      <HintPath>@_bindgen_output_directory@/VeroScriptCSharpFrontend.dll</HintPath>
    </Reference>
    <Reference Include="VeroScriptCSharpBindGen">
      <HintPath>@_bindgen_output_directory@/VeroScriptCSharpBindGen.dll</HintPath>
    </Reference>
    <Reference Include="VeroScriptCSharpTranspiler">
      <HintPath>@_transpiler_output_directory@/VeroScriptCSharpTranspiler.dll</HintPath>
    </Reference>
  </ItemGroup>
</Project>
]=] _sdk_consumer_project @ONLY)
file(WRITE "${VSC_WORK_DIR}/sdk-consumer/SdkConsumer.csproj" "${_sdk_consumer_project}")
file(WRITE "${VSC_WORK_DIR}/sdk-consumer/Program.cs" [=[
using VeroScript.CSharpBindGen;
using VeroScript.CSharpTranspiler;

const string source = "public static class Program { public static int Main() => 0; }";
CSharpBindGenResult bindings = CSharpBindGen.GenerateBindingsFromSource(source,
    bindGenOptions: new() { EmitRegistrationSource = false });
if (!bindings.Succeeded) return 1;
CSharpTranspileResult transpiled = CSharpTranspiler.TranspileSource(source,
    options: new() { ValidateGeneratedSource = false });
if (!transpiled.Succeeded || !transpiled.Source.Contains("int main()", StringComparison.Ordinal)) return 2;
Console.WriteLine("C# installed SDK assembly consumer passed.");
return 0;
]=])
foreach(_sdk_consumer_tfm IN ITEMS net8.0 net9.0)
	execute_process(COMMAND "${VSC_DOTNET}" run
		--project "${VSC_WORK_DIR}/sdk-consumer/SdkConsumer.csproj"
		--configuration Release --framework "${_sdk_consumer_tfm}" --nologo
		RESULT_VARIABLE _sdk_consumer_result)
	if(NOT _sdk_consumer_result EQUAL 0)
		message(FATAL_ERROR
			"Installed SDK assembly consumer failed for ${_sdk_consumer_tfm}")
	endif()
endforeach()

file(WRITE "${VSC_WORK_DIR}/consumer/NuGet.Config"
"<configuration><packageSources><clear/><add key=\"local\" value=\"${VSC_WORK_DIR}/packages\"/><add key=\"nuget.org\" value=\"https://api.nuget.org/v3/index.json\"/></packageSources><packageSourceMapping><packageSource key=\"local\"><package pattern=\"VeroScript.*\"/></packageSource><packageSource key=\"nuget.org\"><package pattern=\"*\"/></packageSource></packageSourceMapping></configuration>")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
	"NUGET_PACKAGES=${VSC_WORK_DIR}/nuget-cache"
	"${VSC_DOTNET}" run --project "${VSC_WORK_DIR}/consumer/Consumer.csproj"
	--configuration Release
	RESULT_VARIABLE _consumer_result)
if(NOT _consumer_result EQUAL 0)
	message(FATAL_ERROR "Packed BindGen consumer failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
	"NUGET_PACKAGES=${VSC_WORK_DIR}/nuget-cache"
	"${VSC_DOTNET}" tool install --tool-path "${VSC_WORK_DIR}/tool"
	--configfile "${VSC_WORK_DIR}/consumer/NuGet.Config"
	VeroScript.CSharpBindGen.Tool --version ${VSC_VERSION}
	RESULT_VARIABLE _tool_install_result)
if(NOT _tool_install_result EQUAL 0)
	message(FATAL_ERROR "Packed BindGen tool installation failed")
endif()
if(WIN32)
	set(_tool "${VSC_WORK_DIR}/tool/veroscript-csharp-bindgen.exe")
else()
	set(_tool "${VSC_WORK_DIR}/tool/veroscript-csharp-bindgen")
endif()
execute_process(COMMAND "${_tool}" --version RESULT_VARIABLE _tool_result
	OUTPUT_VARIABLE _tool_version OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _tool_result EQUAL 0
		OR NOT _tool_version STREQUAL "VeroScriptCSharpBindGen ${VSC_VERSION}")
	message(FATAL_ERROR "Packed BindGen tool version check failed: ${_tool_version}")
endif()
file(MAKE_DIRECTORY "${VSC_WORK_DIR}/transpiler-tool")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
	"NUGET_PACKAGES=${VSC_WORK_DIR}/nuget-cache"
	"${VSC_DOTNET}" tool install --tool-path "${VSC_WORK_DIR}/transpiler-tool"
	--configfile "${VSC_WORK_DIR}/consumer/NuGet.Config"
	VeroScript.CSharpTranspiler.Tool --version ${VSC_VERSION}
	RESULT_VARIABLE _transpiler_tool_install_result)
if(NOT _transpiler_tool_install_result EQUAL 0)
	message(FATAL_ERROR "Packed C# transpiler tool installation failed")
endif()
if(WIN32)
	set(_transpiler_tool "${VSC_WORK_DIR}/transpiler-tool/veroscript-csharp-transpiler.exe")
else()
	set(_transpiler_tool "${VSC_WORK_DIR}/transpiler-tool/veroscript-csharp-transpiler")
endif()
execute_process(COMMAND "${_transpiler_tool}" --version
	RESULT_VARIABLE _transpiler_tool_result OUTPUT_VARIABLE _transpiler_tool_version
	OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _transpiler_tool_result EQUAL 0
		OR NOT _transpiler_tool_version STREQUAL "VeroScriptCSharpTranspiler ${VSC_VERSION}")
	message(FATAL_ERROR
		"Packed C# transpiler tool version check failed: ${_transpiler_tool_version}")
endif()
file(MAKE_DIRECTORY "${VSC_WORK_DIR}/transpiler-execution")
file(WRITE "${VSC_WORK_DIR}/transpiler-execution/Input.cs" [=[
using System;
public static class Program
{
    public static void Main()
    {
        uint value = 4294967295U;
        ulong wide = 18446744073709551615UL;
        Console.WriteLine(value / 3U);
        Console.WriteLine(value % 3U);
        Console.WriteLine(wide / 3UL);
        Console.WriteLine(wide % 3UL);
        int signed = 2147483647;
        signed += 1;
        Console.WriteLine(signed);
        float rounded = 16777216f;
        int integer = 16777217;
        Console.WriteLine(integer == rounded);
        Console.WriteLine(integer > rounded);
        bool chooseInteger = true;
        Console.WriteLine((chooseInteger ? integer : rounded) == rounded);
        Console.Write(true);
        Console.WriteLine(false);
        uint zero = 0U;
        try { Console.WriteLine(value / zero); }
        catch { Console.WriteLine("caught"); }
    }
}
]=])
execute_process(COMMAND "${_transpiler_tool}"
	"${VSC_WORK_DIR}/transpiler-execution/Input.cs"
	--output "${VSC_WORK_DIR}/transpiler-execution/Output.vs"
	--veroscript "${VSC_EXECUTABLE}"
	RESULT_VARIABLE _transpile_result TIMEOUT 60)
if(NOT _transpile_result EQUAL 0)
	message(FATAL_ERROR "Installed transpiler tool failed to generate validated scalar code")
endif()
execute_process(COMMAND "${VSC_EXECUTABLE}"
	"${VSC_WORK_DIR}/transpiler-execution/Output.vs"
	RESULT_VARIABLE _execution_result OUTPUT_VARIABLE _execution_output
	ERROR_VARIABLE _execution_error TIMEOUT 30)
string(REPLACE "\r\n" "\n" _execution_output "${_execution_output}")
if(NOT _execution_result EQUAL 0 OR NOT _execution_output STREQUAL
		"1431655765\n0\n6148914691236517205\n0\n-2147483648\nTrue\nFalse\nTrue\nTrueFalse\ncaught\n")
	message(FATAL_ERROR
		"Installed transpiler scalar output differs from CLR semantics: ${_execution_output} ${_execution_error}")
endif()
file(WRITE "${VSC_WORK_DIR}/transpiler-execution/Unsupported.cs"
	"public static class Program { public static void Main() { string text = checked(\"a\" + \"b\"); } }\n")
execute_process(COMMAND "${_transpiler_tool}"
	"${VSC_WORK_DIR}/transpiler-execution/Unsupported.cs"
	--output "${VSC_WORK_DIR}/transpiler-execution/Unsupported.vs" --no-validate
	RESULT_VARIABLE _rejection_result OUTPUT_VARIABLE _rejection_output
	ERROR_VARIABLE _rejection_error TIMEOUT 30)
if(_rejection_result EQUAL 0 OR NOT "${_rejection_output}${_rejection_error}" MATCHES "VSCT2001"
		OR EXISTS "${VSC_WORK_DIR}/transpiler-execution/Unsupported.vs")
	message(FATAL_ERROR "Installed transpiler tool silently accepted an unsupported constant context")
endif()
file(WRITE "${VSC_WORK_DIR}/consumer/InputA.cs"
	"namespace Packed; public sealed class InputA { public int Value { get; set; } }\n")
file(WRITE "${VSC_WORK_DIR}/consumer/InputB.cs"
	"namespace Packed; public sealed class InputB { public int Read(InputA value) => value.Value; }\n")
execute_process(COMMAND "${_tool}"
	"${VSC_WORK_DIR}/consumer/InputA.cs" "${VSC_WORK_DIR}/consumer/InputB.cs"
	--catalog "${VSC_WORK_DIR}/project/bindings.json"
	--registration "${VSC_WORK_DIR}/project/Bindings.g.cs"
	--aot-bridge "${VSC_WORK_DIR}/project/Bindings.Aot.g.cs"
	--api-reference-dir "${VSC_WORK_DIR}/project/api"
	--manifest "${VSC_WORK_DIR}/project/bindings.manifest.json"
	RESULT_VARIABLE _project_tool_result)
if(NOT _project_tool_result EQUAL 0
		OR NOT EXISTS "${VSC_WORK_DIR}/project/bindings.json"
		OR NOT EXISTS "${VSC_WORK_DIR}/project/Bindings.g.cs"
		OR NOT EXISTS "${VSC_WORK_DIR}/project/Bindings.Aot.g.cs"
		OR NOT EXISTS "${VSC_WORK_DIR}/project/api/InputA.api.vs.txt"
		OR NOT EXISTS "${VSC_WORK_DIR}/project/api/InputB.api.vs.txt"
		OR NOT EXISTS "${VSC_WORK_DIR}/project/bindings.manifest.json")
	message(FATAL_ERROR "Packed BindGen tool multi-file project generation failed")
endif()

# Check the installed tools against directory aliases, not only path spellings.
set(_path_test_root "${VSC_WORK_DIR}/linked-paths")
file(MAKE_DIRECTORY "${_path_test_root}/source")
if(WIN32)
	file(WRITE "${_path_test_root}/CreateJunction.ps1" [=[
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

param([string]$AliasPath, [string]$SourcePath)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Junction -Path $AliasPath -Target $SourcePath | Out-Null
Add-Type -TypeDefinition @'
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using System.Text;
public static class VolumePathFixture
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetVolumeNameForVolumeMountPoint(string root, StringBuilder volume, uint capacity);
}
'@
$volume = New-Object System.Text.StringBuilder(128)
$root = [System.IO.Path]::GetPathRoot($SourcePath)
if ([VolumePathFixture]::GetVolumeNameForVolumeMountPoint($root, $volume, 128)) {
    $source = Join-Path $SourcePath 'Input.cs'
    [System.IO.File]::WriteAllText((Join-Path (Split-Path $AliasPath -Parent) 'volume-source.txt'),
        $volume.ToString() + $source.Substring($root.Length))
}
]=])
	execute_process(COMMAND "$ENV{SYSTEMROOT}/System32/WindowsPowerShell/v1.0/powershell.exe"
		-NoProfile -NonInteractive -ExecutionPolicy Bypass
		-File "${_path_test_root}/CreateJunction.ps1"
		"${_path_test_root}/alias" "${_path_test_root}/source"
		RESULT_VARIABLE _link_result)
else()
	file(CREATE_LINK "${_path_test_root}/source" "${_path_test_root}/alias"
		SYMBOLIC RESULT _link_result)
endif()
if(NOT _link_result STREQUAL "0")
	message(FATAL_ERROR "Could not create linked-directory CLI fixture: ${_link_result}")
endif()
set(_path_test_source "public static class Input { public static int Main() => 0; }\n")
file(WRITE "${_path_test_root}/source/Input.cs" "${_path_test_source}")
set(_case_insensitive FALSE)
if(EXISTS "${_path_test_root}/source/input.cs")
	set(_case_insensitive TRUE)
endif()
foreach(_linked_tool IN ITEMS bindgen transpiler)
	if(_linked_tool STREQUAL "bindgen")
		set(_linked_command "${_tool}")
		set(_overwrite_arguments --catalog "${_path_test_root}/alias/Input.cs")
		set(_duplicate_arguments --catalog "${_path_test_root}/source/duplicate.json"
			--api-reference "${_path_test_root}/alias/duplicate.json")
		set(_valid_arguments --catalog "${_path_test_root}/alias/bindings.json")
	else()
		set(_linked_command "${_transpiler_tool}")
		set(_overwrite_arguments --no-validate --output "${_path_test_root}/alias/Input.cs")
		set(_duplicate_arguments --no-validate --output "${_path_test_root}/source/duplicate.vs"
			--source-map "${_path_test_root}/alias/duplicate.vs")
		set(_valid_arguments --no-validate --output "${_path_test_root}/alias/valid.vs")
	endif()
	foreach(_rejected_arguments IN ITEMS _overwrite_arguments _duplicate_arguments)
		execute_process(COMMAND "${_linked_command}" "${_path_test_root}/source/Input.cs"
			${${_rejected_arguments}}
			RESULT_VARIABLE _linked_result OUTPUT_VARIABLE _linked_output
			ERROR_VARIABLE _linked_error TIMEOUT 30)
		file(READ "${_path_test_root}/source/Input.cs" _preserved_source)
		if(NOT _linked_result EQUAL 2 OR NOT _preserved_source STREQUAL _path_test_source
				OR EXISTS "${_path_test_root}/source/duplicate.json"
				OR EXISTS "${_path_test_root}/source/duplicate.vs")
			message(FATAL_ERROR
				"Installed ${_linked_tool} did not reject a linked output before staging: ${_linked_output} ${_linked_error}")
		endif()
	endforeach()
	execute_process(COMMAND "${_linked_command}" "${_path_test_root}/source/Input.cs"
		${_valid_arguments} RESULT_VARIABLE _linked_result TIMEOUT 30)
	if(NOT _linked_result EQUAL 0)
		message(FATAL_ERROR "Installed ${_linked_tool} rejected a distinct output in a linked directory")
	endif()

	# Unix is not necessarily case-sensitive: WSL mounted Windows disks and
	# common macOS volumes must protect existing input files too.
	if(_linked_tool STREQUAL "bindgen")
		set(_case_arguments --catalog "${_path_test_root}/source/input.cs")
	else()
		set(_case_arguments --no-validate --output "${_path_test_root}/source/input.cs")
	endif()
	if(_case_insensitive)
		execute_process(COMMAND "${_linked_command}" "${_path_test_root}/source/Input.cs"
			${_case_arguments} RESULT_VARIABLE _case_result
			OUTPUT_VARIABLE _case_output ERROR_VARIABLE _case_error TIMEOUT 30)
		file(READ "${_path_test_root}/source/Input.cs" _preserved_source)
		if(NOT _case_result EQUAL 2 OR NOT _preserved_source STREQUAL _path_test_source)
			message(FATAL_ERROR "Installed ${_linked_tool} accepted a case alias of its input: ${_case_output} ${_case_error}")
		endif()
	else()
		# Do not reject genuinely different files on a case-sensitive filesystem.
		file(WRITE "${_path_test_root}/source/input.cs" "distinct output fixture\n")
		execute_process(COMMAND "${_linked_command}" "${_path_test_root}/source/Input.cs"
			${_case_arguments} RESULT_VARIABLE _case_result
			OUTPUT_VARIABLE _case_output ERROR_VARIABLE _case_error TIMEOUT 30)
		file(READ "${_path_test_root}/source/Input.cs" _preserved_source)
		file(REMOVE "${_path_test_root}/source/input.cs")
		if(NOT _case_result EQUAL 0 OR NOT _preserved_source STREQUAL _path_test_source)
			message(FATAL_ERROR "Installed ${_linked_tool} rejected a distinct case-sensitive output: ${_case_output} ${_case_error}")
		endif()
	endif()

	# New output names may alias each other only when committed. On a
	# case-insensitive Unix mount, rollback must also restore earlier outputs.
	set(_atomic_root "${_path_test_root}/atomic-${_linked_tool}")
	file(MAKE_DIRECTORY "${_atomic_root}")
	set(_atomic_original "preserve existing generated output\n")
	if(_linked_tool STREQUAL "bindgen")
		set(_atomic_existing "${_atomic_root}/catalog.json")
		set(_atomic_lower "${_atomic_root}/extra.txt")
		set(_atomic_upper "${_atomic_root}/EXTRA.TXT")
		set(_atomic_arguments --catalog "${_atomic_existing}"
			--api-reference "${_atomic_lower}" --registration "${_atomic_upper}")
	else()
		set(_atomic_existing "${_atomic_root}/Input.vs")
		set(_atomic_lower "${_atomic_root}/Input.vs.map.json")
		set(_atomic_upper "${_atomic_root}/INPUT.VS.MAP.JSON")
		set(_atomic_arguments --no-validate --output-dir "${_atomic_root}"
			--manifest "${_atomic_upper}")
	endif()
	file(WRITE "${_atomic_existing}" "${_atomic_original}")
	execute_process(COMMAND "${_linked_command}" "${_path_test_root}/source/Input.cs"
		${_atomic_arguments} RESULT_VARIABLE _atomic_result
		OUTPUT_VARIABLE _atomic_output ERROR_VARIABLE _atomic_error TIMEOUT 30)
	file(READ "${_path_test_root}/source/Input.cs" _preserved_source)
	file(READ "${_atomic_existing}" _preserved_output)
	file(GLOB _atomic_leftovers "${_atomic_root}/*.tmp.*" "${_atomic_root}/*.bak.*")
	if(NOT _preserved_source STREQUAL _path_test_source OR _atomic_leftovers)
		message(FATAL_ERROR "Installed ${_linked_tool} changed its source or left staging files: ${_atomic_leftovers}")
	endif()
	if(_case_insensitive)
		if(NOT _atomic_result EQUAL 2 OR NOT _preserved_output STREQUAL _atomic_original
				OR EXISTS "${_atomic_lower}" OR EXISTS "${_atomic_upper}")
			message(FATAL_ERROR "Installed ${_linked_tool} failed to restore aliased outputs: ${_atomic_output} ${_atomic_error}")
		endif()
	elseif(NOT _atomic_result EQUAL 0 OR NOT EXISTS "${_atomic_lower}"
			OR NOT EXISTS "${_atomic_upper}" OR _preserved_output STREQUAL _atomic_original)
		message(FATAL_ERROR "Installed ${_linked_tool} rejected distinct case-sensitive outputs: ${_atomic_output} ${_atomic_error}")
	endif()

	if(WIN32)
		file(TO_NATIVE_PATH "${_path_test_root}/source/Input.cs" _native_source)
		set(_device_sources "\\\\?\\${_native_source}" "\\\\.\\${_native_source}")
		if(EXISTS "${_path_test_root}/volume-source.txt")
			file(READ "${_path_test_root}/volume-source.txt" _volume_source)
			list(APPEND _device_sources "${_volume_source}")
		endif()
		foreach(_device_source IN LISTS _device_sources)
			if(_linked_tool STREQUAL "bindgen")
				set(_device_overwrite --catalog "${_device_source}")
				set(_device_output "${_path_test_root}/source/device.json")
				string(REGEX REPLACE "Input\\.cs$" "device.json" _device_aliased_output "${_device_source}")
				set(_device_duplicate --catalog "${_device_output}"
					--api-reference "${_device_aliased_output}")
				set(_device_valid --catalog "${_device_aliased_output}")
			else()
				set(_device_overwrite --no-validate --output "${_device_source}")
				set(_device_output "${_path_test_root}/source/device.vs")
				string(REGEX REPLACE "Input\\.cs$" "device.vs" _device_aliased_output "${_device_source}")
				set(_device_duplicate --no-validate --output "${_device_output}"
					--source-map "${_device_aliased_output}")
				set(_device_valid --no-validate --output "${_device_aliased_output}")
			endif()
			foreach(_device_rejected IN ITEMS _device_overwrite _device_duplicate)
				execute_process(COMMAND "${_linked_command}" "${_path_test_root}/source/Input.cs"
					${${_device_rejected}} RESULT_VARIABLE _device_result
					OUTPUT_VARIABLE _device_output_text ERROR_VARIABLE _device_error TIMEOUT 30)
				file(READ "${_path_test_root}/source/Input.cs" _preserved_source)
				if(NOT _device_result EQUAL 2 OR NOT _preserved_source STREQUAL _path_test_source
						OR EXISTS "${_device_output}")
					message(FATAL_ERROR
						"Installed ${_linked_tool} accepted a conflicting device path: ${_device_output_text} ${_device_error}")
				endif()
			endforeach()
			execute_process(COMMAND "${_linked_command}" "${_path_test_root}/source/Input.cs"
				${_device_valid} RESULT_VARIABLE _device_result TIMEOUT 30)
			if(NOT _device_result EQUAL 0 OR NOT EXISTS "${_device_output}")
				message(FATAL_ERROR "Installed ${_linked_tool} rejected a distinct device-path output")
			endif()
			# Keep the next alias's duplicate-output check free of existing files.
			file(REMOVE "${_device_output}")
		endforeach()

		# Exercise UTF-16 names and final paths larger than the initial buffer.
		string(REPEAT "segment" 25 _long_segment)
		set(_long_root "${_path_test_root}/資料 with spaces/${_long_segment}/${_long_segment}/${_long_segment}")
		file(MAKE_DIRECTORY "${_long_root}")
		file(WRITE "${_long_root}/Input.cs" "${_path_test_source}")
		if(_linked_tool STREQUAL "bindgen")
			set(_long_output "${_long_root}/bindings.json")
			set(_long_arguments --catalog "${_long_output}")
		else()
			set(_long_output "${_long_root}/script.vs")
			set(_long_arguments --no-validate --output "${_long_output}")
		endif()
		execute_process(COMMAND "${_linked_command}" "${_long_root}/Input.cs" ${_long_arguments}
			RESULT_VARIABLE _long_result OUTPUT_VARIABLE _long_output_text
			ERROR_VARIABLE _long_error TIMEOUT 30)
		file(READ "${_long_root}/Input.cs" _preserved_source)
		if(NOT _long_result EQUAL 0 OR NOT EXISTS "${_long_output}"
				OR NOT _preserved_source STREQUAL _path_test_source)
			message(FATAL_ERROR "Installed ${_linked_tool} failed with a long Unicode path: ${_long_output_text} ${_long_error}")
		endif()
	endif()
endforeach()
