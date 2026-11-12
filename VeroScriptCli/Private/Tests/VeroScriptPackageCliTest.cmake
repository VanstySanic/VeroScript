# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

set(_forced_backend mixed)
set(_jit_selection "")
if(VERO_JIT_BACKEND)
	set(_forced_backend jit)
	set(_jit_selection "--jit-backend=${VERO_JIT_BACKEND}")
else()
	# Without a concrete backend, exercise the interpreter and mixed fallback.
	set(_jit_selection "--backend=mixed")
endif()

if(NOT DEFINED VERO_CLI OR NOT DEFINED VERO_PACKAGE)
	message(FATAL_ERROR "VERO_CLI and VERO_PACKAGE are required")
endif()

set(_package_main "${VERO_PACKAGE}.main.vs")
set(_package_left "${VERO_PACKAGE}.left.vs")
set(_package_right "${VERO_PACKAGE}.right.vs")
set(_secondary_package "${VERO_PACKAGE}.secondary.vsapp")
set(_library_package "${VERO_PACKAGE}.library.vslib")
set(_library_source "${VERO_PACKAGE}.library.vs")
set(_library_app "${VERO_PACKAGE}.library-app.vsapp")
set(_library_app_source "${VERO_PACKAGE}.library-app.vs")
set(_library_private_probe "${VERO_PACKAGE}.library-private-probe.vs")
set(_library_deleted_probe "${VERO_PACKAGE}.library-deleted-probe.vs")
set(_implicit_library_app "${VERO_PACKAGE}.implicit-library-app.vsapp")
set(_implicit_library_source "${VERO_PACKAGE}.implicit-library-app.vs")
set(_typed_library_package "${VERO_PACKAGE}.typed-library.vslib")
set(_typed_library_source "${VERO_PACKAGE}.typed-library.vs")
set(_typed_library_app "${VERO_PACKAGE}.typed-library-app.vsapp")
set(_typed_library_app_source "${VERO_PACKAGE}.typed-library-app.vs")
set(_typed_library_private_probe "${VERO_PACKAGE}.typed-library-private-probe.vs")
set(_typed_library_private_method_probe "${VERO_PACKAGE}.typed-library-private-method-probe.vs")
set(_surface_library_package "${VERO_PACKAGE}.surface-library.vslib")
set(_surface_library_source "${VERO_PACKAGE}.surface-library.vs")
set(_surface_library_app "${VERO_PACKAGE}.surface-library-app.vsapp")
set(_surface_library_app_source "${VERO_PACKAGE}.surface-library-app.vs")
set(_nested_library_package "${VERO_PACKAGE}.nested-library.vslib")
set(_nested_library_source "${VERO_PACKAGE}.nested-library.vs")
set(_nested_library_app "${VERO_PACKAGE}.nested-library-app.vsapp")
set(_nested_library_app_source "${VERO_PACKAGE}.nested-library-app.vs")
set(_generic_library_package "${VERO_PACKAGE}.generic-library.vslib")
set(_generic_library_source "${VERO_PACKAGE}.generic-library.vs")
set(_generic_library_app "${VERO_PACKAGE}.generic-library-app.vsapp")
set(_generic_library_app_source "${VERO_PACKAGE}.generic-library-app.vs")
set(_generic_nested_package "${VERO_PACKAGE}.generic-nested.vslib")
set(_generic_nested_source "${VERO_PACKAGE}.generic-nested.vs")
set(_generic_nested_app "${VERO_PACKAGE}.generic-nested-app.vsapp")
set(_generic_nested_app_source "${VERO_PACKAGE}.generic-nested-app.vs")
set(_diamond_base_package "${VERO_PACKAGE}.diamond-base.vslib")
set(_diamond_base_source "${VERO_PACKAGE}.diamond-base.vs")
set(_diamond_left_package "${VERO_PACKAGE}.diamond-left.vslib")
set(_diamond_left_source "${VERO_PACKAGE}.diamond-left.vs")
set(_diamond_right_package "${VERO_PACKAGE}.diamond-right.vslib")
set(_diamond_right_source "${VERO_PACKAGE}.diamond-right.vs")
set(_diamond_app "${VERO_PACKAGE}.diamond-app.vsapp")
set(_diamond_app_source "${VERO_PACKAGE}.diamond-app.vs")
set(_diamond_reverse_app "${VERO_PACKAGE}.diamond-reverse-app.vsapp")
set(_diamond_reverse_app_source "${VERO_PACKAGE}.diamond-reverse-app.vs")
set(_diamond_transitive_app "${VERO_PACKAGE}.diamond-transitive-app.vsapp")
set(_diamond_transitive_app_source "${VERO_PACKAGE}.diamond-transitive-app.vs")
set(_secondary_main "${VERO_PACKAGE}.secondary.vs")
file(REMOVE
	"${VERO_PACKAGE}"
	"${VERO_PACKAGE}.tmp"
	"${_package_main}"
	"${_package_left}"
	"${_package_right}"
	"${_secondary_package}"
	"${_secondary_package}.tmp"
	"${_secondary_main}"
	"${_library_package}"
	"${_library_package}.tmp"
	"${_library_source}"
	"${_library_app}"
	"${_library_app}.tmp"
	"${_library_app_source}"
	"${_library_private_probe}"
	"${_library_deleted_probe}"
	"${_implicit_library_app}"
	"${_implicit_library_app}.tmp"
	"${_implicit_library_source}"
	"${_typed_library_package}"
	"${_typed_library_package}.tmp"
	"${_typed_library_source}"
	"${_typed_library_app}"
	"${_typed_library_app}.tmp"
	"${_typed_library_app_source}"
	"${_typed_library_private_probe}"
	"${_typed_library_private_method_probe}"
	"${_surface_library_package}"
	"${_surface_library_package}.tmp"
	"${_surface_library_source}"
	"${_surface_library_app}"
	"${_surface_library_app}.tmp"
	"${_surface_library_app_source}"
	"${_nested_library_package}"
	"${_nested_library_package}.tmp"
	"${_nested_library_source}"
	"${_nested_library_app}"
	"${_nested_library_app}.tmp"
	"${_nested_library_app_source}"
	"${_generic_library_package}"
	"${_generic_library_package}.tmp"
	"${_generic_library_source}"
	"${_generic_library_app}"
	"${_generic_library_app}.tmp"
	"${_generic_library_app_source}"
	"${_generic_nested_package}"
	"${_generic_nested_package}.tmp"
	"${_generic_nested_source}"
	"${_generic_nested_app}"
	"${_generic_nested_app}.tmp"
	"${_generic_nested_app_source}"
	"${_diamond_base_package}"
	"${_diamond_base_source}"
	"${_diamond_left_package}"
	"${_diamond_left_source}"
	"${_diamond_right_package}"
	"${_diamond_right_source}"
	"${_diamond_app}"
	"${_diamond_app_source}"
	"${_diamond_reverse_app}"
	"${_diamond_reverse_app_source}"
	"${_diamond_transitive_app}"
	"${_diamond_transitive_app_source}")

# Numeric command-line options must consume the complete value. Accepting a
# valid prefix (for example, "7junk") makes malformed IDE/package settings look
# successful while silently running with a different value.
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --dap-port=123junk
	RESULT_VARIABLE _invalid_dap_port_result
	OUTPUT_VARIABLE _invalid_dap_port_out
	ERROR_VARIABLE _invalid_dap_port_err)
if(NOT _invalid_dap_port_result EQUAL 2
	OR NOT _invalid_dap_port_err MATCHES "--dap-port expects an integer")
	message(FATAL_ERROR
		"invalid DAP port was not rejected strictly (${_invalid_dap_port_result})\n"
		"${_invalid_dap_port_out}\n${_invalid_dap_port_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --package-version 7junk
	RESULT_VARIABLE _invalid_package_version_result
	OUTPUT_VARIABLE _invalid_package_version_out
	ERROR_VARIABLE _invalid_package_version_err)
if(NOT _invalid_package_version_result EQUAL 2
	OR NOT _invalid_package_version_err MATCHES
		"--package-version expects an unsigned integer")
	message(FATAL_ERROR
		"invalid package version was not rejected strictly "
		"(${_invalid_package_version_result})\n"
		"${_invalid_package_version_out}\n${_invalid_package_version_err}")
endif()

file(WRITE "${_package_main}"
	"void main() { print(package_left() + package_right()); }\n")
file(WRITE "${_package_left}"
	"export int package_left() { return 20; }\n")
file(WRITE "${_package_right}"
	"export int package_right() { return 22; }\n")
file(WRITE "${_secondary_main}"
	"void main() { print(7); }\n")
file(WRITE "${_library_source}"
	"int library_hidden() { return 99; }\n"
	"export int library_answer() { return 42; }\n"
	"export int library_removed(string reason) = delete;\n")
file(WRITE "${_library_app_source}"
	"import \"${_library_package}\";\n"
	"void main() { print(library_answer()); }\n")
file(WRITE "${_library_private_probe}"
	"import \"${_library_package}\";\n"
	"void main() { print(library_hidden()); }\n")
file(WRITE "${_library_deleted_probe}"
	"import \"${_library_package}\";\n"
	"void main() { print(library_removed(\"unsupported\")); }\n")
file(WRITE "${_implicit_library_source}"
	"void main() { print(library_answer()); }\n")
file(WRITE "${_typed_library_source}"
	"struct InternalState { int value; }\n"
	"export interface IValue { virtual int value() const = 0; }\n"
	"export struct Score : IValue {\n"
	"public: int amount;\n"
	"Score(int initial) { amount = initial; }\n"
	"int value() const { return amount; }\n"
	"}\n"
	"export class Counter {\n"
	"private: int current = 0;\n"
	"public: Counter(int initial) { current = initial; }\n"
	"int add(int delta = 1) { current += delta; return current; }\n"
	"}\n"
	"export class OpaqueCounter {\n"
	"private: InternalState state;\n"
	"int secret() { return state.value; }\n"
	"public: int value() const { return 9; }\n"
	"}\n"
	"export int shared_score = 5;\n")
file(WRITE "${_typed_library_app_source}"
	"import \"${_typed_library_package}\";\n"
	"import \"${_library_package}\";\n"
	"void main() {\n"
	"  Score score(20);\n"
	"  Counter counter(20);\n"
	"  OpaqueCounter opaque();\n"
	"  print(score.value() + counter.add() + opaque.value() + shared_score + library_answer() - 55);\n"
	"}\n")
file(WRITE "${_typed_library_private_probe}"
	"import \"${_typed_library_package}\";\n"
	"void main() { Counter counter(1); print(counter.current); }\n")
file(WRITE "${_typed_library_private_method_probe}"
	"import \"${_typed_library_package}\";\n"
	"void main() { OpaqueCounter value(); print(value.secret()); }\n")
file(WRITE "${_surface_library_source}"
	"namespace library::api {\n"
	"export enum class Mode : uint16 { Add = 1, Multiply = 2 }\n"
	"export using Count = uint64;\n"
	"export interface IValue { virtual int value() const = 0; }\n"
	"export struct ForwardUser {\n"
	"public: ForwardTarget target;\n"
	"static int shared;\n"
	"static int doubled(int value) { return value * 2; }\n"
	"}\n"
	"export struct ForwardTarget { int amount; }\n"
	"export class Base { public: virtual int calculate(int value) { return value; } }\n"
	"export class Derived : public Base, IValue {\n"
	"public: int calculate(int value) override { return value + 1; }\n"
	"int value() const { return 40; }\n"
	"}\n"
	"export class ProtectedBase {\n"
	"protected: int bias = 6;\n"
	"public: virtual int base_value() const { return bias; }\n"
	"}\n"
	"export int select(int value) { return value + 10; }\n"
	"export int select(string value) { return 2; }\n"
	"export Count total = 1u64;\n"
	"export string label = \"ok\";\n"
	"export Derived singleton = Derived();\n"
	"}\n")
file(WRITE "${_surface_library_app_source}"
	"class LocalDerived : public library::api::ProtectedBase {\n"
	"public: int read_bias() const { return bias; }\n"
	"}\n"
	"class LocalValue : library::api::IValue {\n"
	"public: int value() const { return 2; }\n"
	"}\n"
	"void main() {\n"
	"  library::api::Derived value();\n"
	"  library::api::Base base = value;\n"
	"  library::api::Count extra = 1u64;\n"
	"  library::api::total += extra;\n"
	"  library::api::Mode mode = library::api::Mode::Add;\n"
	"  library::api::ForwardUser forward();\n"
	"  LocalDerived local();\n"
	"  LocalValue local_value();\n"
	"  forward.target.amount = 3;\n"
	"  library::api::ForwardUser::shared = 4;\n"
	"  print(value.calculate(1) + value.value());\n"
	"  print(library::api::total);\n"
	"  print(mode);\n"
	"  print(forward.target.amount);\n"
	"  print(library::api::ForwardUser::shared);\n"
	"  print(library::api::ForwardUser::doubled(7));\n"
	"  print(base.calculate(1));\n"
	"  print(library::api::label);\n"
	"  print(library::api::singleton.value());\n"
	"  print(local.read_bias());\n"
	"  print(local_value.value());\n"
	"  print(library::api::select(1) + library::api::select(\"xy\"));\n"
	"}\n")
file(WRITE "${_nested_library_source}"
	"import \"${_library_package}\";\n"
	"export int forwarded_answer() { return library_answer(); }\n")
file(WRITE "${_nested_library_app_source}"
	"import \"${_nested_library_package}\";\n"
	"void main() { print(forwarded_answer()); }\n")
file(WRITE "${_generic_library_source}"
	"int hidden_increment(int value) { return value + 1; }\n"
	"export T identity<T>(T value) { return value; }\n"
	"export int apply_hidden<T>(T unused, int value) { return hidden_increment(value); }\n"
	"export interface IReadable<T> { virtual T read() const = 0; }\n"
	"export struct Box<T> {\n"
	"  T value;\n"
	"  Box(T initial) { value = initial; }\n"
	"  T get() const { return value; }\n"
	"}\n"
	"export class Holder<T> {\n"
	"private: T value;\n"
	"public: Holder(T initial) { value = initial; }\n"
	"T get() const { return value; }\n"
	"}\n")
file(WRITE "${_generic_library_app_source}"
	"import \"${_generic_library_package}\";\n"
	"class StringReader : IReadable<string> { public: string read() const { return \"generic\"; } }\n"
	"void main() {\n"
	"  Box<int> box(42);\n"
	"  Holder<string> holder(\"generic\");\n"
	"  StringReader reader();\n"
	"  print(identity<int>(box.get()));\n"
	"  print(identity<string>(holder.get()) + reader.read());\n"
	"  print(apply_hidden<string>(\"unused\", 41));\n"
	"}\n")
file(WRITE "${_generic_nested_source}"
	"export import \"${_generic_library_package}\";\n"
	"export int nested_marker() { return 1; }\n")
file(WRITE "${_generic_nested_app_source}"
	"import \"${_generic_nested_package}\";\n"
	"void main() { Box<int> box(41); print(identity<int>(box.get()) + nested_marker()); }\n")
file(WRITE "${_diamond_base_source}"
	"export int initialization_count = 0;\n"
	"int initialize_once() { initialization_count += 1; return 40; }\n"
	"export int initialized_value = initialize_once();\n"
	"export int base_value() { return initialized_value; }\n"
	"export T identity<T>(T value) { return value; }\n")
file(WRITE "${_diamond_left_source}"
	"import \"${_diamond_base_package}\";\n"
	"export int left_value() { return identity<int>(base_value()) + 1; }\n"
	"export int left_initialization_count() { return initialization_count; }\n"
	"export string left_text() { return identity<string>(\"ok\"); }\n")
file(WRITE "${_diamond_right_source}"
	"import \"${_diamond_base_package}\";\n"
	"export int right_value() { return identity<int>(base_value()) + 2; }\n"
	"export string right_text() { return identity<string>(\"ok\"); }\n")
file(WRITE "${_diamond_app_source}"
	"import \"${_diamond_base_package}\";\n"
	"import \"${_diamond_left_package}\";\n"
	"import \"${_diamond_right_package}\";\n"
	"void main() {\n"
	"  print(base_value() + left_value() + right_value() - 81);\n"
	"  print(initialization_count);\n"
	"  print(left_text() + right_text());\n"
	"}\n")
file(WRITE "${_diamond_reverse_app_source}"
	"import \"${_diamond_right_package}\";\n"
	"import \"${_diamond_left_package}\";\n"
	"import \"${_diamond_base_package}\";\n"
	"void main() {\n"
	"  print(base_value() + left_value() + right_value() - 81);\n"
	"  print(initialization_count);\n"
	"  print(left_text() + right_text());\n"
	"}\n")
file(WRITE "${_diamond_transitive_app_source}"
	"import \"${_diamond_left_package}\";\n"
	"import \"${_diamond_right_package}\";\n"
	"void main() {\n"
	"  print(left_value() + right_value() - 41);\n"
	"  print(left_initialization_count());\n"
	"  print(left_text() + right_text());\n"
	"}\n")

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${VERO_PACKAGE}"
		--package-version 7
		--strip-package-debug
		--package-file "${_package_left}"
		--package-file "${_package_right}"
		"${_package_main}"
	RESULT_VARIABLE _build_result
	OUTPUT_VARIABLE _build_out
	ERROR_VARIABLE _build_err)
if(NOT _build_result EQUAL 0)
	message(FATAL_ERROR
		"package CLI build failed (${_build_result})\n${_build_out}\n${_build_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --check "${VERO_PACKAGE}"
	RESULT_VARIABLE _check_result
	OUTPUT_VARIABLE _check_out
	ERROR_VARIABLE _check_err)
if(NOT _check_result EQUAL 0)
	message(FATAL_ERROR
		"package CLI check failed (${_check_result})\n${_check_out}\n${_check_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" package inspect "${VERO_PACKAGE}"
	RESULT_VARIABLE _inspect_result
	OUTPUT_VARIABLE _inspect_out
	ERROR_VARIABLE _inspect_err)
if(NOT _inspect_result EQUAL 0
	OR NOT _inspect_out MATCHES "Kind: application"
	OR NOT _inspect_out MATCHES "Version: 7"
	OR NOT _inspect_out MATCHES "Debug info: stripped")
	message(FATAL_ERROR
		"package CLI inspect failed (${_inspect_result})\n${_inspect_out}\n${_inspect_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --backend=${_forced_backend} "${VERO_PACKAGE}"
	RESULT_VARIABLE _run_result
	OUTPUT_VARIABLE _run_out
	ERROR_VARIABLE _run_err)
if(NOT _run_result EQUAL 0)
	message(FATAL_ERROR
		"package CLI run failed (${_run_result})\n${_run_out}\n${_run_err}")
endif()
string(FIND "${_run_out}" "42" _answer_index)
if(_answer_index EQUAL -1)
	message(FATAL_ERROR "package CLI output mismatch\n${_run_out}\n${_run_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_secondary_package}"
		--package-version 8
		"${_secondary_main}"
	RESULT_VARIABLE _secondary_build_result
	OUTPUT_VARIABLE _secondary_build_out
	ERROR_VARIABLE _secondary_build_err)
if(NOT _secondary_build_result EQUAL 0)
	message(FATAL_ERROR
		"secondary package CLI build failed (${_secondary_build_result})\n"
		"${_secondary_build_out}\n${_secondary_build_err}")
endif()
file(SHA256 "${VERO_PACKAGE}" _primary_hash)
file(SHA256 "${_secondary_package}" _secondary_hash)
if(_primary_hash STREQUAL _secondary_hash)
	message(FATAL_ERROR "independent package outputs must not be identical")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_secondary_package}"
	RESULT_VARIABLE _secondary_run_result
	OUTPUT_VARIABLE _secondary_run_out
	ERROR_VARIABLE _secondary_run_err)
if(NOT _secondary_run_result EQUAL 0)
	message(FATAL_ERROR
		"secondary package CLI run failed (${_secondary_run_result})\n"
		"${_secondary_run_out}\n${_secondary_run_err}")
endif()
string(FIND "${_secondary_run_out}" "7" _secondary_answer_index)
if(_secondary_answer_index EQUAL -1)
	message(FATAL_ERROR
		"secondary package CLI output mismatch\n"
		"${_secondary_run_out}\n${_secondary_run_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_library_package}"
		--package-version 9
		"${_library_source}"
	RESULT_VARIABLE _library_build_result
	OUTPUT_VARIABLE _library_build_out
	ERROR_VARIABLE _library_build_err)
if(NOT _library_build_result EQUAL 0)
	message(FATAL_ERROR
		"library package CLI build failed (${_library_build_result})\n"
		"${_library_build_out}\n${_library_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --check "${_library_package}"
	RESULT_VARIABLE _library_check_result
	OUTPUT_VARIABLE _library_check_out
	ERROR_VARIABLE _library_check_err)
if(NOT _library_check_result EQUAL 0)
	message(FATAL_ERROR
		"library package CLI check failed (${_library_check_result})\n"
		"${_library_check_out}\n${_library_check_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_library_package}"
	RESULT_VARIABLE _library_run_result
	OUTPUT_VARIABLE _library_run_out
	ERROR_VARIABLE _library_run_err)
if(_library_run_result EQUAL 0)
	message(FATAL_ERROR "library package must not run directly")
endif()
string(FIND "${_library_run_err}" "cannot be run directly" _library_error_index)
if(_library_error_index EQUAL -1)
	message(FATAL_ERROR
		"library run rejection diagnostic mismatch\n${_library_run_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_library_app}"
		"${_library_app_source}"
	RESULT_VARIABLE _library_app_build_result
	OUTPUT_VARIABLE _library_app_build_out
	ERROR_VARIABLE _library_app_build_err)
if(NOT _library_app_build_result EQUAL 0)
	message(FATAL_ERROR
		"application/library link failed (${_library_app_build_result})\n"
		"${_library_app_build_out}\n${_library_app_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_library_app}"
	RESULT_VARIABLE _library_app_run_result
	OUTPUT_VARIABLE _library_app_run_out
	ERROR_VARIABLE _library_app_run_err)
if(NOT _library_app_run_result EQUAL 0)
	message(FATAL_ERROR
		"linked application run failed (${_library_app_run_result})\n"
		"${_library_app_run_out}\n${_library_app_run_err}")
endif()
string(FIND "${_library_app_run_out}" "42" _library_app_answer_index)
if(_library_app_answer_index EQUAL -1)
	message(FATAL_ERROR
		"linked application output mismatch\n"
		"${_library_app_run_out}\n${_library_app_run_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --check "${_library_private_probe}"
	RESULT_VARIABLE _library_private_result
	OUTPUT_VARIABLE _library_private_out
	ERROR_VARIABLE _library_private_err)
if(_library_private_result EQUAL 0)
	message(FATAL_ERROR
		"private library function must not be visible to an importing application")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --lang en-US --check "${_library_deleted_probe}"
	RESULT_VARIABLE _library_deleted_result
	OUTPUT_VARIABLE _library_deleted_out
	ERROR_VARIABLE _library_deleted_err)
if(_library_deleted_result EQUAL 0)
	message(FATAL_ERROR
		"deleted exported library function must remain unavailable after packaging")
endif()
string(FIND "${_library_deleted_err}" "deleted declaration" _library_deleted_error_index)
if(_library_deleted_error_index EQUAL -1)
	message(FATAL_ERROR
		"deleted library diagnostic mismatch\n${_library_deleted_out}\n${_library_deleted_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--implicit-import "${_library_package}"
		--emit-package "${_implicit_library_app}"
		"${_implicit_library_source}"
	RESULT_VARIABLE _implicit_library_build_result
	OUTPUT_VARIABLE _implicit_library_build_out
	ERROR_VARIABLE _implicit_library_build_err)
if(NOT _implicit_library_build_result EQUAL 0)
	message(FATAL_ERROR
		"implicit binary library import failed (${_implicit_library_build_result})\n"
		"${_implicit_library_build_out}\n${_implicit_library_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_implicit_library_app}"
	RESULT_VARIABLE _implicit_library_run_result
	OUTPUT_VARIABLE _implicit_library_run_out
	ERROR_VARIABLE _implicit_library_run_err)
if(NOT _implicit_library_run_result EQUAL 0)
	message(FATAL_ERROR
		"implicit-library application run failed (${_implicit_library_run_result})\n"
		"${_implicit_library_run_out}\n${_implicit_library_run_err}")
endif()
string(FIND "${_implicit_library_run_out}" "42" _implicit_library_answer_index)
if(_implicit_library_answer_index EQUAL -1)
	message(FATAL_ERROR
		"implicit-library application output mismatch\n"
		"${_implicit_library_run_out}\n${_implicit_library_run_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_typed_library_package}"
		--strip-package-debug
		"${_typed_library_source}"
	RESULT_VARIABLE _typed_library_build_result
	OUTPUT_VARIABLE _typed_library_build_out
	ERROR_VARIABLE _typed_library_build_err)
if(NOT _typed_library_build_result EQUAL 0)
	message(FATAL_ERROR
		"typed library build failed (${_typed_library_build_result})\n"
		"${_typed_library_build_out}\n${_typed_library_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_typed_library_app}"
		"${_typed_library_app_source}"
	RESULT_VARIABLE _typed_library_app_build_result
	OUTPUT_VARIABLE _typed_library_app_build_out
	ERROR_VARIABLE _typed_library_app_build_err)
if(NOT _typed_library_app_build_result EQUAL 0)
	message(FATAL_ERROR
		"typed library application link failed (${_typed_library_app_build_result})\n"
		"${_typed_library_app_build_out}\n${_typed_library_app_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_typed_library_app}"
	RESULT_VARIABLE _typed_library_app_run_result
	OUTPUT_VARIABLE _typed_library_app_run_out
	ERROR_VARIABLE _typed_library_app_run_err)
if(NOT _typed_library_app_run_result EQUAL 0)
	message(FATAL_ERROR
		"typed library application run failed (${_typed_library_app_run_result})\n"
		"${_typed_library_app_run_out}\n${_typed_library_app_run_err}")
endif()
string(FIND "${_typed_library_app_run_out}" "42" _typed_library_answer_index)
if(_typed_library_answer_index EQUAL -1)
	message(FATAL_ERROR
		"typed library application output mismatch\n"
		"${_typed_library_app_run_out}\n${_typed_library_app_run_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --check "${_typed_library_private_probe}"
	RESULT_VARIABLE _typed_library_private_result
	OUTPUT_VARIABLE _typed_library_private_out
	ERROR_VARIABLE _typed_library_private_err)
if(_typed_library_private_result EQUAL 0)
	message(FATAL_ERROR
		"private class field must remain inaccessible across a package boundary")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --check "${_typed_library_private_method_probe}"
	RESULT_VARIABLE _typed_library_private_method_result
	OUTPUT_VARIABLE _typed_library_private_method_out
	ERROR_VARIABLE _typed_library_private_method_err)
if(_typed_library_private_method_result EQUAL 0)
	message(FATAL_ERROR
		"private class method must remain inaccessible across a package boundary")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_surface_library_package}"
		--strip-package-debug
		"${_surface_library_source}"
	RESULT_VARIABLE _surface_library_build_result
	OUTPUT_VARIABLE _surface_library_build_out
	ERROR_VARIABLE _surface_library_build_err)
if(NOT _surface_library_build_result EQUAL 0)
	message(FATAL_ERROR
		"complete exported package surface failed (${_surface_library_build_result})\n"
		"${_surface_library_build_out}\n${_surface_library_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--implicit-import "${_surface_library_package}"
		--emit-package "${_surface_library_app}"
		"${_surface_library_app_source}"
	RESULT_VARIABLE _surface_library_app_build_result
	OUTPUT_VARIABLE _surface_library_app_build_out
	ERROR_VARIABLE _surface_library_app_build_err)
if(NOT _surface_library_app_build_result EQUAL 0)
	message(FATAL_ERROR
		"exported package surface application failed (${_surface_library_app_build_result})\n"
		"${_surface_library_app_build_out}\n${_surface_library_app_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_surface_library_app}"
	RESULT_VARIABLE _surface_library_app_run_result
	OUTPUT_VARIABLE _surface_library_app_run_out
	ERROR_VARIABLE _surface_library_app_run_err)
if(NOT _surface_library_app_run_result EQUAL 0)
	message(FATAL_ERROR
		"exported package surface run failed (${_surface_library_app_run_result})\n"
		"${_surface_library_app_run_out}\n${_surface_library_app_run_err}")
endif()
foreach(_expected_line IN ITEMS "42" "2" "library.api.Mode(1)" "3" "4" "14" "ok" "40" "6" "13")
	string(FIND "${_surface_library_app_run_out}" "${_expected_line}" _surface_line_index)
	if(_surface_line_index EQUAL -1)
		message(FATAL_ERROR
			"exported package surface output missing '${_expected_line}'\n"
			"${_surface_library_app_run_out}\n${_surface_library_app_run_err}")
	endif()
endforeach()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_nested_library_package}"
		"${_nested_library_source}"
	RESULT_VARIABLE _nested_library_result
	OUTPUT_VARIABLE _nested_library_out
	ERROR_VARIABLE _nested_library_err)
if(NOT _nested_library_result EQUAL 0)
	message(FATAL_ERROR
		"nested library build failed (${_nested_library_result})\n"
		"${_nested_library_out}\n${_nested_library_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_nested_library_app}"
		"${_nested_library_app_source}"
	RESULT_VARIABLE _nested_app_build_result
	OUTPUT_VARIABLE _nested_app_build_out
	ERROR_VARIABLE _nested_app_build_err)
if(NOT _nested_app_build_result EQUAL 0)
	message(FATAL_ERROR
		"nested-library application build failed (${_nested_app_build_result})\n"
		"${_nested_app_build_out}\n${_nested_app_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_nested_library_app}"
	RESULT_VARIABLE _nested_app_run_result
	OUTPUT_VARIABLE _nested_app_run_out
	ERROR_VARIABLE _nested_app_run_err)
if(NOT _nested_app_run_result EQUAL 0 OR NOT _nested_app_run_out MATCHES "42")
	message(FATAL_ERROR
		"nested-library application run failed (${_nested_app_run_result})\n"
		"${_nested_app_run_out}\n${_nested_app_run_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_generic_library_package}"
		"${_generic_library_source}"
	RESULT_VARIABLE _generic_library_result
	OUTPUT_VARIABLE _generic_library_out
	ERROR_VARIABLE _generic_library_err)
if(NOT _generic_library_result EQUAL 0)
	message(FATAL_ERROR
		"open-generic library build failed (${_generic_library_result})\n"
		"${_generic_library_out}\n${_generic_library_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_generic_library_app}"
		"${_generic_library_app_source}"
	RESULT_VARIABLE _generic_app_build_result
	OUTPUT_VARIABLE _generic_app_build_out
	ERROR_VARIABLE _generic_app_build_err)
if(NOT _generic_app_build_result EQUAL 0)
	message(FATAL_ERROR
		"open-generic application build failed (${_generic_app_build_result})\n"
		"${_generic_app_build_out}\n${_generic_app_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_generic_library_app}"
	RESULT_VARIABLE _generic_app_run_result
	OUTPUT_VARIABLE _generic_app_run_out
	ERROR_VARIABLE _generic_app_run_err)
if(NOT _generic_app_run_result EQUAL 0
	OR NOT _generic_app_run_out MATCHES "42"
	OR NOT _generic_app_run_out MATCHES "generic")
	message(FATAL_ERROR
		"open-generic application run failed (${_generic_app_run_result})\n"
		"${_generic_app_run_out}\n${_generic_app_run_err}")
endif()

execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_generic_nested_package}"
		"${_generic_nested_source}"
	RESULT_VARIABLE _generic_nested_result
	OUTPUT_VARIABLE _generic_nested_out
	ERROR_VARIABLE _generic_nested_err)
if(NOT _generic_nested_result EQUAL 0)
	message(FATAL_ERROR
		"nested open-generic library build failed (${_generic_nested_result})\n"
		"${_generic_nested_out}\n${_generic_nested_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_generic_nested_app}"
		"${_generic_nested_app_source}"
	RESULT_VARIABLE _generic_nested_app_build_result
	OUTPUT_VARIABLE _generic_nested_app_build_out
	ERROR_VARIABLE _generic_nested_app_build_err)
if(NOT _generic_nested_app_build_result EQUAL 0)
	message(FATAL_ERROR
		"nested open-generic app build failed (${_generic_nested_app_build_result})\n"
		"${_generic_nested_app_build_out}\n${_generic_nested_app_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} "${_generic_nested_app}"
	RESULT_VARIABLE _generic_nested_app_run_result
	OUTPUT_VARIABLE _generic_nested_app_run_out
	ERROR_VARIABLE _generic_nested_app_run_err)
if(NOT _generic_nested_app_run_result EQUAL 0
	OR NOT _generic_nested_app_run_out MATCHES "42")
	message(FATAL_ERROR
		"nested open-generic app run failed (${_generic_nested_app_run_result})\n"
		"${_generic_nested_app_run_out}\n${_generic_nested_app_run_err}")
endif()

foreach(_diamond_part IN ITEMS base left right)
	execute_process(
		COMMAND "${VERO_CLI}" ${_jit_selection}
			--emit-package "${_diamond_${_diamond_part}_package}"
			"${_diamond_${_diamond_part}_source}"
		RESULT_VARIABLE _diamond_build_result
		OUTPUT_VARIABLE _diamond_build_out
		ERROR_VARIABLE _diamond_build_err)
	if(NOT _diamond_build_result EQUAL 0)
		message(FATAL_ERROR
			"diamond ${_diamond_part} package build failed "
			"(${_diamond_build_result})\n"
			"${_diamond_build_out}\n${_diamond_build_err}")
	endif()
endforeach()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection}
		--emit-package "${_diamond_app}"
		"${_diamond_app_source}"
	RESULT_VARIABLE _diamond_app_build_result
	OUTPUT_VARIABLE _diamond_app_build_out
	ERROR_VARIABLE _diamond_app_build_err)
if(NOT _diamond_app_build_result EQUAL 0)
	message(FATAL_ERROR
		"diamond package application build failed (${_diamond_app_build_result})\n"
		"${_diamond_app_build_out}\n${_diamond_app_build_err}")
endif()
execute_process(
	COMMAND "${VERO_CLI}" ${_jit_selection} --backend=${_forced_backend} "${_diamond_app}"
	RESULT_VARIABLE _diamond_app_run_result
	OUTPUT_VARIABLE _diamond_app_run_out
	ERROR_VARIABLE _diamond_app_run_err)
if(NOT _diamond_app_run_result EQUAL 0
	OR NOT _diamond_app_run_out MATCHES "42[\r\n]+1[\r\n]+okok")
	message(FATAL_ERROR
		"diamond dependency run/initialization mismatch "
		"(${_diamond_app_run_result})\n"
		"${_diamond_app_run_out}\n${_diamond_app_run_err}")
endif()

foreach(_diamond_variant IN ITEMS reverse transitive)
	execute_process(
		COMMAND "${VERO_CLI}" ${_jit_selection}
			--emit-package "${_diamond_${_diamond_variant}_app}"
			"${_diamond_${_diamond_variant}_app_source}"
		RESULT_VARIABLE _diamond_variant_build_result
		OUTPUT_VARIABLE _diamond_variant_build_out
		ERROR_VARIABLE _diamond_variant_build_err)
	if(NOT _diamond_variant_build_result EQUAL 0)
		message(FATAL_ERROR
			"diamond ${_diamond_variant} application build failed "
			"(${_diamond_variant_build_result})\n"
			"${_diamond_variant_build_out}\n${_diamond_variant_build_err}")
	endif()
	execute_process(
		COMMAND "${VERO_CLI}" ${_jit_selection} --backend=${_forced_backend} "${_diamond_${_diamond_variant}_app}"
		RESULT_VARIABLE _diamond_variant_run_result
		OUTPUT_VARIABLE _diamond_variant_run_out
		ERROR_VARIABLE _diamond_variant_run_err)
	if(NOT _diamond_variant_run_result EQUAL 0
		OR NOT _diamond_variant_run_out MATCHES "42[\r\n]+1[\r\n]+okok")
		message(FATAL_ERROR
			"diamond ${_diamond_variant} dependency run/initialization mismatch "
			"(${_diamond_variant_run_result})\n"
			"${_diamond_variant_run_out}\n${_diamond_variant_run_err}")
	endif()
endforeach()

file(REMOVE
	"${VERO_PACKAGE}"
	"${VERO_PACKAGE}.tmp"
	"${_package_main}"
	"${_package_left}"
	"${_package_right}"
	"${_secondary_package}"
	"${_secondary_package}.tmp"
	"${_secondary_main}"
	"${_library_package}"
	"${_library_package}.tmp"
	"${_library_source}"
	"${_library_app}"
	"${_library_app}.tmp"
	"${_library_app_source}"
	"${_library_private_probe}"
	"${_library_deleted_probe}"
	"${_implicit_library_app}"
	"${_implicit_library_app}.tmp"
	"${_implicit_library_source}"
	"${_typed_library_package}"
	"${_typed_library_package}.tmp"
	"${_typed_library_source}"
	"${_typed_library_app}"
	"${_typed_library_app}.tmp"
	"${_typed_library_app_source}"
	"${_typed_library_private_probe}"
	"${_typed_library_private_method_probe}"
	"${_surface_library_package}"
	"${_surface_library_package}.tmp"
	"${_surface_library_source}"
	"${_surface_library_app}"
	"${_surface_library_app}.tmp"
	"${_surface_library_app_source}"
	"${_nested_library_package}"
	"${_nested_library_package}.tmp"
	"${_nested_library_source}"
	"${_nested_library_app}"
	"${_nested_library_app}.tmp"
	"${_nested_library_app_source}"
	"${_generic_library_package}"
	"${_generic_library_package}.tmp"
	"${_generic_library_source}"
	"${_generic_library_app}"
	"${_generic_library_app}.tmp"
	"${_generic_library_app_source}"
	"${_generic_nested_package}"
	"${_generic_nested_package}.tmp"
	"${_generic_nested_source}"
	"${_generic_nested_app}"
	"${_generic_nested_app}.tmp"
	"${_generic_nested_app_source}"
	"${_diamond_base_package}"
	"${_diamond_base_source}"
	"${_diamond_left_package}"
	"${_diamond_left_source}"
	"${_diamond_right_package}"
	"${_diamond_right_source}"
	"${_diamond_app}"
	"${_diamond_app_source}"
	"${_diamond_reverse_app}"
	"${_diamond_reverse_app_source}"
	"${_diamond_transitive_app}"
	"${_diamond_transitive_app_source}")
