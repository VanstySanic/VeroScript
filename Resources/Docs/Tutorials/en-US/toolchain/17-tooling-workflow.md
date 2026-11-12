# Tooling workflow: connect an editor and debugger

This tutorial connects a project to the VeroScript language server and debug
adapter. You will first verify the CLI, then give the language server the same
project settings, exercise editor features, and finally start a DAP session.

## Step 1: establish a working CLI baseline

Use the CLI and VM from one Runtime release, and use the matching language server and a client that supports LSP and DAP. Save the two files below in the same writable lesson directory. Run the terminal commands from that directory and open it as the editor workspace.

### hello.vs

```VeroScript
import "lesson-utils.vs";

namespace Lesson
{
	int twice(int value)
	{
		return value * 2;
	}
}

int main(string[] args)
{
	std::chrono::now();
	int value = Lesson::twice(lessonSeed());
	print("value={}, argc={}", value, len(args));
	return 0;
}
```

### lesson-utils.vs

```VeroScript
export int lessonSeed()
{
	return 21;
}
```

```powershell
veroscript --version
veroscript --check hello.vs
veroscript --vm-backend=vs-bytecode --backend=vm hello.vs red blue
```

The check succeeds without running the script. The run prints `value=42, argc=2` and exits with code zero.

If the check fails, correct the project or installation before configuring the
editor. The language server uses the same compiler-facing analysis, so a
different result usually indicates different paths, macros, or catalogs.

## Step 2: start the language server

Configure the editor to launch the version-matched `veroscript-lsp` over standard
input and output. The editor sends `initialize`, waits for the response, and
then sends `initialized`. Use the project root as the workspace root.

Add the fields below to the `params` of the `initialize` request alongside the workspace and client capabilities. Start the server in the lesson directory so `.` names that directory. If the editor starts it elsewhere, use the absolute directory path, for example `C:/work/project` on Windows. In another project, set `stdlibPath` only when you need to point the server at a separate standard-library directory.

```json
{
	"locale": "en-US",
	"initializationOptions": {
		"veroscript": {
			"importPaths": [
				"."
			]
		}
	}
}
```

If the client supports dynamic watched-file registration, the server requests
watchers for `**/*.vs` and `**/*.vslib` after `initialized`. Otherwise configure
those patterns in the client or send `workspace/didChangeWatchedFiles` notifications
explicitly.

Open a `.vs` document and let the editor send `didOpen`. The server keeps the
open buffer in memory, so imported files can be analyzed before they are saved.
Use incremental `didChange` notifications; stale document versions are ignored.

## Step 3: exercise editor assistance

Open the `hello.vs` you saved in step 1.

1. Temporarily introduce a syntax or type error and confirm that diagnostics
   are published, then undo the error.
2. Replace `std::chrono::now();` with `std`, place the caret after it, invoke
   completion, and accept the namespace; it inserts `std::`. Type `ch` and
   accept `chrono`; it inserts `chrono::`, so you can inspect its members.
   Finish the line as `std::chrono::now();` so the sample remains valid.
   Namespace completion adds `::` only when one does not already follow the
   replacement range.
3. Temporarily replace `Lesson::twice(lessonSeed())` with `Lesson::tw` and request completion. If the client advertises `textDocument.completion.completionItem.snippetSupport`, check the parameter placeholder for `twice`. Restore the original call before continuing.
4. Hover a namespace segment, function, or type and inspect its resolved detail.
5. Go to the definition of `lessonSeed` from its call in `hello.vs` and check that it opens `lesson-utils.vs`. Find `Lesson` and `main` in the document symbols, then search for `Lesson::twice` with `workspace/symbol` and resolve a result with `workspaceSymbol/resolve`.
6. Resolve a completion item to inspect its Markdown documentation when available. Open the link on the `import "lesson-utils.vs";` line.
7. Request semantic tokens, then request a semantic-token delta with the
   previous result identifier.
8. Prepare a rename on `Lesson`, rename it to `Practice` and check that its declaration and the qualified call change together.
9. Format the document, then format a selected range and check that the edit covers its complete lines. Try `source.formatDocument`. To try `source.organizeImports`, duplicate the import line once and check that the action removes the duplicate. Save both files before running `veroscript --check hello.vs` again.

Generic and callable completions can use LSP snippets where applicable. The
server sends snippets only when the client advertises
`textDocument.completion.completionItem.snippetSupport`; other insertions use
plain text. Standard-library namespaces are useful completion/navigation targets
but are not renameable. Code actions
include full formatting and conservative removal of exact duplicate imports;
neither action rewrites unrelated source.

## Step 4: add host metadata when the project uses bindings

Follow this step if you are working with a project that uses host bindings. The two lesson files can proceed directly to step 5.

Export or obtain the binding catalogs used by the host, then add them to
`initializationOptions.veroscript.bindingCatalogPaths` in the same order used by the project. The server merges
the catalogs and supplies their functions, types, members, annotations, generic
templates, and documentation to completion, hover, symbols, and analysis.

Edit a catalog while the server is running. On the next request it is reloaded
and open documents are reanalyzed. Remember that a catalog only describes the
host interface; the host must still register the runtime implementation.

## Step 5: switch diagnostic language

Set the desired BCP 47 tag in `initialize.locale` before opening documents:

```json
{
	"locale": "zh-CN"
}
```

Restart the language server with the new `locale`. Introduce the same temporary error from step 3 and compare the diagnostic code and translated message. The code stays the same, and missing translations fall back to `en-US`. Fix the error and save the file before debugging.

## Step 6: start the debugger

Choose a transport in the DAP client. Use standard input and output when the client launches the adapter with this command from the lesson directory.

```powershell
veroscript --debug hello.vs
```

For a separately started client, start the TCP adapter from the lesson directory, then connect the client to `127.0.0.1:4711`.

```powershell
veroscript --debug --dap-port=4711 hello.vs
```

To preset script arguments with the standard input and output transport, use the following command in place of the first.

```powershell
veroscript --debug hello.vs red blue
```

In the client, launch `hello.vs` with the lesson directory as `cwd`. Set a breakpoint on the `print` line and use `["red", "blue"]` as the launch `args`, or omit `args` if you preset them with the CLI. When execution stops at the breakpoint, continue and expect `value=42, argc=2` in the debug output. The request sequence and options are described in the [debugging reference](../../../../../VeroScriptTooling/Resources/Docs/User/en-US/debugging.md).

For a DAP `launch` or `attach` request, use `program` or inline `source`, and
pass `args` for `main(string[] args)`. Use `input` for the script's standard
input, `env` for environment overrides, `cwd` for the working directory, and
`stdlibSearchPaths` for standard-library roots. Script input is separate from
the DAP protocol stream. Relative paths use `cwd`, or `workspace` when no
working directory is supplied.

Debugging, retained initialization ranges and bounded cleanup use the VM path so that breakpoints, stepping, and
variables are stable. Measure performance with a normal non-debug run.

The DAP `initialize` arguments accept a BCP 47 `locale` for diagnostic and error
messages. Clients should branch on DAP success and error fields rather than on
translated message text.

Source line breakpoints use `setBreakpoints`. Exception and function
breakpoints are not supported; their requests fail explicitly, so a client
should not display those breakpoint classes as active.

After `configurationDone`, a wire host acknowledges the request and runs the VM
on an execution worker. Use `pause` to request a cooperative pause of a running
VM; the resulting `stopped` event has reason `pause`. Use `continue`, `next`,
`stepIn`, or `stepOut` to resume, and use `terminate` to request cancellation.
The protocol remains responsive while the VM is running. `disconnect` defaults
to terminating a launched debuggee but not an attached one; in a multi-session
TCP host, `terminateDebuggee=false` leaves the listener available.

During execution, `std::console::write` and `std::console::error` produce
newline-free output. Call `std::console::flush()` to make a progress fragment
visible immediately. The adapter forwards standard output and error output as
separate DAP output events before the process exits.

Finish an LSP session with `shutdown` followed by `exit`; finish the DAP session
through the client so the adapter can release the debug process.

## Exercises

1. Add an import path and make a module resolve identically in the CLI and LSP.
2. Send an older `didChange` version after a newer one and confirm that the
   newer buffer remains active.
3. Add a binding catalog and verify that a documented host function appears in
   completion and hover.
4. Change `initialize.locale` to another packaged language and confirm that
   diagnostic codes and script results do not change.
5. Send `workspace/didChangeWatchedFiles` after changing an on-disk imported
   module and confirm that open-document diagnostics are refreshed.

For the protocol contract, read the [Language Server reference](../../../../../VeroScriptTooling/Resources/Docs/User/en-US/lsp.md).
For the public query layer, read the [Tooling module reference](../../../../../VeroScriptTooling/Resources/Docs/User/en-US/index.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](17-tooling-workflow.md) · [fr-FR](../../fr-FR/toolchain/17-tooling-workflow.md) · [ja-JP](../../ja-JP/toolchain/17-tooling-workflow.md) · [ko-KR](../../ko-KR/toolchain/17-tooling-workflow.md) · [zh-CN](../../zh-CN/toolchain/17-tooling-workflow.md) · [zh-TW](../../zh-TW/toolchain/17-tooling-workflow.md)
