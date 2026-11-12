# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Compile and execute consumers of the staged Windows C# runtime, without source references."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
from xml.sax.saxutils import escape


FRAMEWORKS = ("net8.0", "net9.0")
MACHINES = {"x64": 0x8664, "x86": 0x014c, "arm64": 0xaa64}
RESULT_PREFIX = "VEROSCRIPT_CSHARP_CONSUMER "
PROGRAM = r'''using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using VeroScript;

if (!RuntimeInformation.ProcessArchitecture.ToString().Equals(args[0], StringComparison.OrdinalIgnoreCase))
    throw new Exception("Consumer process architecture does not match the SDK target");
using var session = new VeroScriptSession();
var loaded = session.LoadSource("int answer() { return 42; }");
if (!loaded.Success) throw new Exception("Packaged C# SDK load failed: " + session.GetLastError());
var call = session.CallFunction("answer");
using var value = call.ReturnValue;
if (!call.Result.Success || value.GetInt32() != 42)
    throw new Exception("Packaged C# SDK native call did not return 42");
string? native = null;
foreach (ProcessModule module in Process.GetCurrentProcess().Modules)
    if (Path.GetFileName(module.FileName).Equals("VeroScriptInterop.dll", StringComparison.OrdinalIgnoreCase))
        native = Path.GetFullPath(module.FileName);
if (native is null || !native.Equals(Path.Combine(AppContext.BaseDirectory, "VeroScriptInterop.dll"), StringComparison.OrdinalIgnoreCase))
    throw new Exception("Consumer did not load its packaged native Interop library");
Console.WriteLine("VEROSCRIPT_CSHARP_CONSUMER " + JsonSerializer.Serialize(new {
    processArchitecture = RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant(),
    osArchitecture = RuntimeInformation.OSArchitecture.ToString().ToLowerInvariant(),
    runtime = RuntimeInformation.FrameworkDescription,
    runtimeVersion = Environment.Version.ToString(),
    managedAssembly = typeof(VeroScriptSession).Assembly.Location,
    nativeLibrary = native,
    answer = value.GetInt32()
}));
'''


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def pe_machine(path: Path) -> int:
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b"MZ":
            raise ValueError(f"Not a PE library: {path}")
        stream.seek(struct.unpack_from("<I", header, 60)[0])
        signature = stream.read(6)
    if len(signature) != 6 or signature[:4] != b"PE\0\0":
        raise ValueError(f"Invalid PE signature: {path}")
    return struct.unpack_from("<H", signature, 4)[0]


def validate_runtime(runtime: Path, architecture: str) -> dict:
    if architecture not in MACHINES:
        raise ValueError(f"Unsupported Windows architecture: {architecture}")
    runtime = runtime.resolve(strict=True)
    result = {}
    for framework in FRAMEWORKS:
        libraries = runtime / "lib/dotnet" / framework
        managed = libraries / "VeroScriptCSharp.dll"
        native = libraries / "VeroScriptInterop.dll"
        for file in (managed, native):
            if not file.is_file():
                raise ValueError(f"Missing packaged C# SDK library: {file}")
        if pe_machine(native) != MACHINES[architecture]:
            raise ValueError(f"Packaged Interop architecture does not match {architecture}: {native}")
        # Copy every sibling native dependency and runtime resource, rather than
        # accidentally resolving dependencies from a producer build directory.
        files = {}
        for file in sorted(libraries.rglob("*")):
            if file.is_symlink() or getattr(file, "is_junction", lambda: False)():
                raise ValueError(f"Linked C# runtime input is not supported: {file}")
            if file.is_file():
                files[file.relative_to(libraries).as_posix()] = digest(file)
        result[framework] = {"directory": str(libraries), "files": files}
    return result


def parse_result(output: str, architecture: str) -> dict:
    rows = [json.loads(line[len(RESULT_PREFIX):]) for line in output.splitlines()
            if line.startswith(RESULT_PREFIX)]
    if len(rows) != 1:
        raise ValueError("Managed consumer did not report exactly one successful native call")
    result = rows[0]
    if result.get("processArchitecture") != architecture or result.get("answer") != 42:
        raise ValueError("Managed consumer result or process architecture does not match")
    return result


def run_consumer(runtime: Path, architecture: str, work: Path, *, dotnet="dotnet",
                 runtime_dotnet=None, timeout=600) -> dict:
    inputs = validate_runtime(runtime, architecture)
    work = work.resolve()
    if work.exists():
        raise ValueError(f"Consumer work directory must be new: {work}")
    runtime_root = runtime.resolve(strict=True)
    if work.is_relative_to(runtime_root) or runtime_root.is_relative_to(work):
        raise ValueError("Consumer work directory must be separate from the SDK")
    work.mkdir(parents=True)
    environment = dict(os.environ)
    environment["DOTNET_ROLL_FORWARD"] = "Major"
    environment["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    environment["DOTNET_NOLOGO"] = "1"
    environment["MSBUILDDISABLENODEREUSE"] = "1"
    # Remove host build/CLI directories as native dependency fallbacks.
    environment["PATH"] = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32")
    build_dotnet = str(Path(shutil.which(str(dotnet)) or dotnet).resolve())
    execute_dotnet = str(Path(shutil.which(str(runtime_dotnet or dotnet)) or runtime_dotnet or dotnet).resolve())
    report = {"schemaVersion": 1, "architecture": architecture,
              "runtimeDirectory": str(runtime_root), "dotnet": build_dotnet,
              "runtimeDotnet": execute_dotnet, "rollForward": "Major",
              "inputs": inputs, "frameworks": {}, "status": "failed"}

    def command(arguments: list[str], log: Path) -> str:
        completed = subprocess.run(arguments, cwd=work, env=environment, text=True,
                                   encoding="utf-8", errors="replace", capture_output=True,
                                   timeout=timeout)
        output = completed.stdout + completed.stderr
        log.write_text(output, encoding="utf-8")
        if completed.returncode:
            raise RuntimeError(f"C# consumer command failed ({completed.returncode}); see {log}")
        return output

    try:
        for framework, item in inputs.items():
            project = work / framework
            project.mkdir()
            libraries = Path(item["directory"])
            (project / "Consumer.csproj").write_text(
                '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
                f'<OutputType>Exe</OutputType><TargetFramework>{framework}</TargetFramework>'
                '<Nullable>enable</Nullable><UseAppHost>false</UseAppHost>'
                f'<PlatformTarget>{architecture}</PlatformTarget>'
                '<NuGetAudit>false</NuGetAudit><UseSharedCompilation>false</UseSharedCompilation>'
                '</PropertyGroup><ItemGroup><Reference Include="VeroScriptCSharp"><HintPath>'
                + escape(str(libraries / "VeroScriptCSharp.dll"))
                + '</HintPath></Reference></ItemGroup></Project>', encoding="utf-8")
            (project / "Program.cs").write_text(PROGRAM, encoding="utf-8")
            output = project / "output"
            command([build_dotnet, "build", str(project / "Consumer.csproj"), "-c", "Release",
                     "-o", str(output), "--disable-build-servers", "-m:1"], project / "build.log")
            shutil.copytree(libraries, output, dirs_exist_ok=True)
            for relative, expected in item["files"].items():
                if digest(output / relative) != expected:
                    raise ValueError(f"Consumer did not retain SDK input bytes: {relative}")
            stdout = command([execute_dotnet, str(output / "Consumer.dll"), architecture],
                             project / "run.log")
            actual = parse_result(stdout, architecture)
            for field, filename in (("managedAssembly", "VeroScriptCSharp.dll"),
                                    ("nativeLibrary", "VeroScriptInterop.dll")):
                if Path(actual[field]).resolve() != (output / filename).resolve():
                    raise ValueError(f"Consumer loaded {field} outside its packaged SDK")
            report["frameworks"][framework] = {"status": "passed", **actual,
                "nativeSha256": digest(output / "VeroScriptInterop.dll"),
                "managedSha256": digest(output / "VeroScriptCSharp.dll"),
                "buildLogSha256": digest(project / "build.log"),
                "runLogSha256": digest(project / "run.log")}
            print(f"Packaged C# SDK native consumer passed: {architecture} {framework} "
                  f"on {actual['runtime']} ({actual['processArchitecture']})", flush=True)
        report["status"] = "passed"
        return report
    finally:
        (work / "consumer-result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--architecture", choices=tuple(MACHINES), required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--dotnet", default="dotnet")
    parser.add_argument("--runtime-dotnet", help="Matching-architecture dotnet host; defaults to --dotnet")
    args = parser.parse_args()
    run_consumer(args.runtime, args.architecture, args.work, dotnet=args.dotnet,
                 runtime_dotnet=args.runtime_dotnet)


if __name__ == "__main__":
    main()
