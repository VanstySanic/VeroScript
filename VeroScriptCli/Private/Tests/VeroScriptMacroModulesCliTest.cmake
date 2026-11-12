# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)
foreach(_required IN ITEMS VERO_CLI VERO_WORK)
	if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
		message(FATAL_ERROR "${_required} is required")
	endif()
endforeach()
file(MAKE_DIRECTORY "${VERO_WORK}")
function(run_cli expected)
	execute_process(COMMAND "${VERO_CLI}" ${ARGN}
		WORKING_DIRECTORY "${VERO_WORK}"
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
	string(REPLACE "\r\n" "\n" output "${output}")
	if(NOT result EQUAL 0 OR NOT output MATCHES "${expected}")
		message(FATAL_ERROR "macro CLI failed (${result}): ${ARGN}\n${output}\n${error}")
	endif()
endfunction()
function(expect_cli_failure)
	execute_process(COMMAND "${VERO_CLI}" ${ARGN}
		WORKING_DIRECTORY "${VERO_WORK}"
		RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
	if(NOT result MATCHES "^[1-9][0-9]*$")
		message(FATAL_ERROR "expected CLI rejection (${result}): ${ARGN}\n${output}\n${error}")
	endif()
endfunction()
file(WRITE "${VERO_WORK}/library.vs" [=[
#define_export VALUE 21
#define_export DOUBLE(x) ((x) * 2)
int initialized = 0;
initialized += 1;
export int value() { return VALUE + initialized - 1; }
]=])
run_cli("" --emit-package library.vslib --package-version 7 library.vs)
run_cli("Kind: library" package inspect library.vslib)
file(WRITE "${VERO_WORK}/implicit-main.vs" [=[
#ifdef PRIVATE
#error private macro leaked
#endif
int main(){print(DOUBLE(VALUE));return 0;}
]=])
run_cli("^42\n$" --backend=vm --bc-cache=off --implicit-import library.vslib implicit-main.vs)
run_cli("^42\n$" --backend=mixed --bc-cache=off --implicit-import library.vs implicit-main.vs)
run_cli("" --emit-package mixed.VsLiB library.vs)
run_cli("Kind: library" package inspect mixed.VsLiB)
file(WRITE "${VERO_WORK}/mixed-import.vs"
	"import \"mixed.VsLiB\"; int main(){print(DOUBLE(value()));return 0;}\n")
run_cli("^42\n$" --backend=vm --bc-cache=off mixed-import.vs)
file(WRITE "${VERO_WORK}/compiler-entry.vs"
	"import \"compiler.vs\"; export void register_extension(std::compiler::registration reg){}\n")
run_cli("" compiler-package compiler-entry.vs compiler.VsLiB)
file(WRITE "${VERO_WORK}/compiler-import.vs" "import compiler \"compiler.VsLiB\"; int main(){return 0;}\n")
run_cli("" --check compiler-import.vs)
file(WRITE "${VERO_WORK}/configured-extension.vs" [=[
import "compiler.vs";
export void register_extension(std::compiler::registration reg)
{
    std::compiler::configuration_value config = std::compiler::configuration("setting");
    if (!config.exists() || config.value() != "a=b")
    {
        throw "configuration must retain the exact string value";
    }
}
]=])
run_cli("" compiler-package configured-extension.vs configured-extension.vslib)
file(WRITE "${VERO_WORK}/configured-consumer.vs" "int main(){return 0;}\n")
foreach(locale IN ITEMS en-US fr-FR ja-JP ko-KR zh-CN zh-TW)
	run_cli("" --lang "${locale}" --backend=vm --no-compiler-imports --compiler-config setting=a=b
		--compiler-config empty= --compiler-instructions 0 --compiler-timeout 0
		--compiler-extension configured-extension.vslib configured-consumer.vs)
endforeach()
expect_cli_failure(--compiler-config "{\"setting\":\"not-json\"}" --check configured-consumer.vs)
expect_cli_failure(--compiler-config "{\"setting\":\"a=b\"}"
	--compiler-extension configured-extension.vslib --check configured-consumer.vs)
expect_cli_failure(--compiler-config setting=a=b --compiler-config setting=other --check configured-consumer.vs)
expect_cli_failure(--compiler-instructions -1 --check configured-consumer.vs)
expect_cli_failure(--compiler-timeout invalid --check configured-consumer.vs)
# The retired suffix must not select the compiled-library loading or output path.
file(COPY_FILE "${VERO_WORK}/library.vslib" "${VERO_WORK}/retired.vspkg")
expect_cli_failure(package inspect retired.vspkg)
expect_cli_failure(--emit-package retired-output.vspkg library.vs)
expect_cli_failure(compiler-package library.vs retired-compiler.vspkg)
file(WRITE "${VERO_WORK}/retired-import.vs" "import \"retired.vspkg\";\n")
expect_cli_failure(--check retired-import.vs)
foreach(retired_output IN ITEMS retired-output.vspkg retired-compiler.vspkg)
	if(EXISTS "${VERO_WORK}/${retired_output}")
		message(FATAL_ERROR "retired library output was created: ${retired_output}")
	endif()
endforeach()
file(WRITE "${VERO_WORK}/facade.vs" "export import \"library.vslib\";\n")
run_cli("" --emit-package facade.vslib facade.vs)
file(WRITE "${VERO_WORK}/main.vs"
	"import \"library.vslib\"; import \"facade.vslib\";\nint main(){print(DOUBLE(value()));return 0;}\n")
foreach(backend IN ITEMS vm mixed)
	run_cli("^42\n$" "--backend=${backend}" --bc-cache=off main.vs)
endforeach()
if(DEFINED VERO_JIT_BACKEND AND NOT "${VERO_JIT_BACKEND}" STREQUAL "")
	run_cli("^42\n$" "--jit-backend=${VERO_JIT_BACKEND}" --backend=jit --bc-cache=off main.vs)
endif()
file(WRITE "${VERO_WORK}/cached.vs" "import \"library.vslib\"; int main(){print(DOUBLE(VALUE));return 0;}\n")
run_cli("^42\n$" --backend=vm --bc-cache-dir "${VERO_WORK}/cache" cached.vs)
run_cli("^42\n$" --backend=vm --bc-cache-dir "${VERO_WORK}/cache" cached.vs)
file(WRITE "${VERO_WORK}/library.vs" "#define_export VALUE 22\n#define_export DOUBLE(x) ((x) * 2)\n")
run_cli("" --emit-package library.vslib --package-version 7 library.vs)
run_cli("^44\n$" --backend=vm --bc-cache-dir "${VERO_WORK}/cache" cached.vs)
file(WRITE "${VERO_WORK}/aot-library.vs" "#define PRIVATE 999\n#define_export VALUE 21\n#define_export DOUBLE(x) ((x) * 2)\n")
run_cli("^42\n$" --backend=vm --bc-cache-dir "${VERO_WORK}/implicit-cache" --implicit-import aot-library.vs implicit-main.vs)
run_cli("^42\n$" --backend=vm --bc-cache-dir "${VERO_WORK}/implicit-cache" --implicit-import aot-library.vs implicit-main.vs)
file(WRITE "${VERO_WORK}/aot-library.vs" "#define PRIVATE 999\n#define_export VALUE 22\n#define_export DOUBLE(x) ((x) * 2)\n")
run_cli("^44\n$" --backend=vm --bc-cache-dir "${VERO_WORK}/implicit-cache" --implicit-import aot-library.vs implicit-main.vs)
file(WRITE "${VERO_WORK}/aot-library.vs" "#define PRIVATE 999\n#define_export VALUE 21\n#define_export DOUBLE(x) ((x) * 2)\n")
file(WRITE "${VERO_WORK}/aot-main.vs" "import \"aot-library.vs\";\n#ifdef PRIVATE\n#error private macro leaked\n#endif\nint main(){print(DOUBLE(VALUE));return 0;}\n")
foreach(target IN ITEMS cpp csharp python)
	run_cli("" aot "--target=${target}" aot-main.vs -o "macro.${target}")
	run_cli("" aot "--target=${target}" --implicit-import aot-library.vs implicit-main.vs -o "implicit-macro.${target}")
	if(NOT EXISTS "${VERO_WORK}/macro.${target}")
		message(FATAL_ERROR "macro AOT did not create ${target} output")
	endif()
	if(NOT EXISTS "${VERO_WORK}/implicit-macro.${target}")
		message(FATAL_ERROR "implicit macro AOT did not create ${target} output")
	endif()
endforeach()
