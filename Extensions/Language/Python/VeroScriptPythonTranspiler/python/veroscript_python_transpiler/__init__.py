# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import ast
from dataclasses import dataclass
import json
import math
from os import PathLike
from os.path import normcase
from pathlib import Path
import subprocess
import tempfile

from veroscript_python_frontend import (
	__version__ as __version__,
	ClassDecl,
	Diagnostic,
	FunctionDecl,
	Localizer,
	ModuleDecl,
	ProjectModule,
	SourceLocation,
	TypeRef,
	evaluate_integer_expression,
	find_veroscript_executable,
	is_veroscript_identifier,
	parse_file,
	parse_files,
	parse_source,
	parse_type_annotation,
	qualified_name,
)


# Keep synchronized with MaxStatementParseDepth in VeroScriptParser.cpp.
_MAX_VEROSCRIPT_STATEMENT_NESTING = 32
_VEROSCRIPT_INT_MIN = -(2**31)
_VEROSCRIPT_INT_MAX = 2**31 - 1
_BUILTIN_CALLS = {"print", "len", "abs", "min", "max"}


def _statements_guarantee_return(statements: list[ast.stmt]) -> bool:
	"""Conservatively recognize paths that always return a value."""

	for statement in statements:
		if isinstance(statement, ast.Return):
			return True
		if (
			isinstance(statement, ast.If)
			and statement.orelse
			and _statements_guarantee_return(statement.body)
			and _statements_guarantee_return(statement.orelse)
		):
			return True
	return False


@dataclass(frozen=True)
class TranspileOptions:
	validate_generated_source: bool = True
	veroscript_executable: str | PathLike[str] | None = None
	generated_source_path: str = ""
	language: str | None = None
	catalog_directory: str | None = None
	timeout_seconds: float = 120.0


@dataclass(frozen=True)
class TranspileResult:
	source: str
	source_map_json: str
	diagnostics: tuple[Diagnostic, ...]

	@property
	def succeeded(self) -> bool:
		return bool(self.source) and not any(
			item.severity == "error" for item in self.diagnostics
		)


@dataclass(frozen=True)
class TranspiledFile:
	input_path: str
	output_name: str
	source: str
	source_map_json: str


@dataclass(frozen=True)
class TranspileProjectOptions:
	validate_generated_sources: bool = True
	veroscript_executable: str | PathLike[str] | None = None
	language: str | None = None
	catalog_directory: str | None = None
	timeout_seconds: float = 120.0


@dataclass(frozen=True)
class TranspileProjectResult:
	files: tuple[TranspiledFile, ...]
	manifest_json: str
	diagnostics: tuple[Diagnostic, ...]

	@property
	def succeeded(self) -> bool:
		return bool(self.files) and not any(
			item.severity == "error" for item in self.diagnostics
		)


class _Unsupported(Exception):
	def __init__(self, node: ast.AST, detail: str) -> None:
		super().__init__(detail)
		self.node = node
		self.detail = detail


class _Emitter:
	def __init__(
		self,
		module: ModuleDecl,
		options: TranspileOptions,
		*,
		known_class_names: set[str] | None = None,
		known_enum_names: set[str] | None = None,
		known_function_names: set[str] | None = None,
		known_class_declarations: dict[str, ClassDecl] | None = None,
		known_function_declarations: dict[str, FunctionDecl] | None = None,
		project_imports: dict[str, tuple[str, ...]] | None = None,
		export_declarations: bool = False,
	) -> None:
		self.module = module
		self.options = options
		self.lines: list[str] = []
		self.indent = 0
		self.mappings: list[dict[str, object]] = []
		self.localizer = Localizer(options.language, options.catalog_directory)
		self.diagnostics: list[Diagnostic] = list(module.diagnostics)
		local_class_names = {
			entry.name for entry in module.classes if not entry.name.startswith("_")
		}
		public_known_classes = {
			name for name in known_class_names or set() if not name.startswith("_")
		}
		public_known_enums = {
			name for name in known_enum_names or set() if not name.startswith("_")
		}
		self.class_names = (
			local_class_names | public_known_classes | public_known_enums
		)
		self.enum_names = {
			entry.name
			for entry in module.classes
			if entry.is_enum and not entry.name.startswith("_")
		} | public_known_enums
		self.function_names = {
			entry.name
			for entry in module.functions
			if not entry.name.startswith("_")
		} | {
			name
			for name in known_function_names or set()
			if not name.startswith("_")
		}
		self.project_imports = project_imports or {}
		self.class_declarations = dict(known_class_declarations or {})
		self.class_declarations.update({entry.name: entry for entry in module.classes})
		self.function_declarations = dict(known_function_declarations or {})
		self.function_declarations.update({entry.name: entry for entry in module.functions})
		self.local_types: dict[str, str] = {}
		self.current_class_name: str | None = None
		self.module_class_names = {entry.name for entry in module.classes}
		self.module_bindings = self.module_class_names | {entry.name for entry in module.functions} | {entry.name for entry in module.variables}
		for statement in (module.node.body if module.node is not None else ()):
			if isinstance(statement, (ast.Import, ast.ImportFrom)):
				self.module_bindings.update(alias.asname or alias.name.split(".")[0] for alias in statement.names)
		self.local_bindings: set[str] = set()
		self.export_declarations = export_declarations
		self.scopes: list[set[str]] = []
		self.has_instance_receiver = False
		self.current_function_returns_void = False
		self.internal_names = {
			name
			for node in ast.walk(module.node or ast.Module(body=[], type_ignores=[]))
			if (name := getattr(node, "id", None) or getattr(node, "arg", None) or getattr(node, "name", None))
		}
		self.next_internal_id = 0
		self.index_getter = self.internal_name("getitem")
		self.index_setter = self.internal_name("setitem")
		self.uses_index_helpers = False
		self.string_length = self.internal_name("string_length")
		self.uses_string_length = False
		self.string_locals: set[str] = set()

	def internal_name(self, stem: str) -> str:
		while True:
			name = f"_vs_python_{stem}_{self.next_internal_id}"
			self.next_internal_id += 1
			if name not in self.internal_names:
				self.internal_names.add(name)
				return name

	def emit_index_helpers(self) -> None:
		if not self.uses_index_helpers:
			return
		# Dynamic containers retain shared storage when passed to a script function.
		# Keep base/key evaluation at the call site; only sequences normalize keys.
		for container in ("array", "deque"):
			for setter in (False, True):
				template = "typename T, typename I" + (", typename V" if setter else "")
				signature = f"void {self.index_setter}({container}<T> values, I index, V value)" if setter else f"T {self.index_getter}({container}<T> values, I index)"
				self.line(f"template <{template}>")
				self.line(signature)
				self.line("{")
				self.line("\tint64 length = cast<int64>(len(values));")
				self.line("\tif (index < 0)")
				self.line("\t{")
				self.line("\t\tvalues[length + index] = value;" if setter else "\t\treturn values[length + index];")
				if setter:
					self.line("\t\treturn;")
				self.line("\t}")
				self.line("\tvalues[index] = value;" if setter else "\treturn values[index];")
				self.line("}")
				self.line()
		for setter in (False, True):
			template = "typename K, typename T, typename I" + (", typename V" if setter else "")
			signature = f"void {self.index_setter}(map<K,T> values, I index, V value)" if setter else f"T {self.index_getter}(map<K,T> values, I index)"
			self.line(f"template <{template}>")
			self.line(signature)
			self.line("{")
			self.line("\tvalues[index] = value;" if setter else "\treturn values[index];")
			self.line("}")
			self.line()

	def indexed_assignment(self, node: ast.Subscript, value: ast.expr, operator: str = "") -> None:
		self.uses_index_helpers = True
		base, index, result = (self.internal_name(stem) for stem in ("base", "index", "value"))
		self.line("{", node)
		self.indent += 1
		# Python evaluates a plain assignment's RHS before its subscription target.
		if not operator:
			self.line(f"auto {result} = {self.expression(value)};")
		self.line(f"auto {base} = {self.expression(node.value)};")
		self.line(f"auto {index} = {self.expression(node.slice)};")
		if operator:
			self.line(f"auto {result} = {self.index_getter}({base}, {index});")
			assigned = f"{result} {operator} {self.expression(value)}"
		else:
			assigned = result
		self.line(f"{self.index_setter}({base}, {index}, {assigned});")
		self.indent -= 1
		self.line("}")

	def attribute_assignment(self, node: ast.Attribute, value: ast.expr) -> None:
		result = self.internal_name("value")
		self.line("{", node)
		self.indent += 1
		# Python evaluates the value before resolving the attribute receiver.
		self.line(f"auto {result} = {self.expression(value)};")
		self.line(f"{self.target(node)} = {result};")
		self.indent -= 1
		self.line("}")

	def expression_type(self, node: ast.expr) -> str | None:
		# Only classify declarations needed to distinguish Python string lengths.
		if isinstance(node, ast.Constant):
			return {str: "string", int: "int", float: "double", bool: "bool"}.get(type(node.value))
		if isinstance(node, ast.Name):
			if node.id == "self" and self.has_instance_receiver:
				return self.current_class_name
			if node.id in self.local_types:
				return self.local_types[node.id]
			for variable in self.module.variables:
				if variable.name == node.id:
					return self.type_name(variable.type, node, variable.name)
			if node.id in self.class_names:
				return node.id
		if isinstance(node, ast.Attribute):
			owner = self.class_declarations.get(self.expression_type(node.value) or "")
			if owner is not None:
				for field in owner.fields:
					if field.name == node.attr:
						return self.type_name(field.type, node, field.name)
		if isinstance(node, ast.Call):
			if isinstance(node.func, ast.Name):
				if node.func.id in self.class_names:
					return node.func.id
				function = self.function_declarations.get(node.func.id)
				if function is not None:
					return self.type_name(function.return_type, node, function.name, allow_void=True)
			elif isinstance(node.func, ast.Attribute):
				owner = self.class_declarations.get(self.expression_type(node.func.value) or "")
				if owner is not None:
					for method in owner.methods:
						if method.name == node.func.attr:
							return self.type_name(method.return_type, node, method.name, allow_void=True)
		if isinstance(node, ast.Subscript):
			owner_type = self.expression_type(node.value) or ""
			if owner_type.startswith(("array<", "deque<")):
				return owner_type[owner_type.index("<") + 1:-1]
			if owner_type.startswith("map<"):
				depth = 0
				for index, character in enumerate(owner_type[4:-1], 4):
					if character == "<":
						depth += 1
					elif character == ">":
						depth -= 1
					elif character == "," and depth == 0:
						return owner_type[index + 1:-1]
		if isinstance(node, ast.IfExp):
			body_type = self.expression_type(node.body)
			if body_type == self.expression_type(node.orelse):
				return body_type
		if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
			if self.expression_type(node.left) == self.expression_type(node.right) == "string":
				return "string"
		return None

	def emit_string_length(self, node: ast.AST) -> None:
		if not self.uses_string_length:
			return
		output = Path(self.options.generated_source_path or f"{Path(self.module.source_path).stem}.vs")
		dependency = output.with_name("string.vs")
		project_outputs = {normcase(path) for paths in self.project_imports.values() for path in paths}
		if normcase(output.name) == "string.vs" or dependency.is_file() or normcase(dependency.as_posix()) in project_outputs:
			raise _Unsupported(node, "len(str) requires the standard-library string.vs; a generated or existing string.vs output shadows it")
		self.line('import "string.vs";')
		self.line()
		self.line(f"int64 {self.string_length}(string value)")
		self.line("{")
		# Encode once: per-byte char_code(string, index) would copy the string.
		self.line("\tauto bytes = std::strings::encode_utf8(value);")
		self.line("\tint64 size = bytes.size();")
		self.line("\tint64 count = 0;")
		self.line("\tfor (int64 index = 0; index < size; index++)")
		self.line("\t{")
		self.line("\t\tint byte = cast<int>(bytes.get(index));")
		self.line("\t\tif ((byte & 192) != 128)")
		self.line("\t\t{")
		self.line("\t\t\tcount++;")
		self.line("\t\t}")
		self.line("\t}")
		self.line("\treturn count;")
		self.line("}")
		self.line()

	def location(self, node: ast.AST) -> SourceLocation:
		return SourceLocation(
			self.module.source_path,
			max(1, int(getattr(node, "lineno", 1))),
			max(1, int(getattr(node, "col_offset", 0)) + 1),
			max(1, int(getattr(node, "end_lineno", getattr(node, "lineno", 1)))),
			max(1, int(getattr(node, "end_col_offset", getattr(node, "col_offset", 0))) + 1),
		)

	def identifier(self, name: str, node: ast.AST, subject: str) -> str:
		if not is_veroscript_identifier(name):
			raise _Unsupported(
				node,
				f"{subject} {name!r} is not a valid VeroScript identifier",
			)
		return name

	def fail(self, node: ast.AST, detail: str, code: str = "PYT2001") -> None:
		self.diagnostics.append(
			Diagnostic(
				code,
				self.localizer.message("python_tools.unsupported_construct", detail),
				self.location(node),
			)
		)

	def line(self, text: str = "", node: ast.AST | None = None) -> None:
		self.lines.append("\t" * self.indent + text)
		if node is not None:
			location = self.location(node)
			self.mappings.append(
				{
					"generatedLine": len(self.lines),
					"sourcePath": location.path,
					"sourceLine": location.line,
					"sourceColumn": location.column,
				}
			)

	def type_name(
		self,
		type_ref: TypeRef | None,
		node: ast.AST,
		subject: str,
		*,
		allow_void: bool = False,
	) -> str:
		if type_ref is None:
			raise _Unsupported(node, f"missing type annotation for {subject}")
		if type_ref.optional:
			raise _Unsupported(node, f"optional type {type_ref.spelling}")
		leaf = type_ref.name.rsplit(".", 1)[-1]
		primitives = {
			"None": "void",
			"NoneType": "void",
			"bool": "bool",
			"int": "int",
			"float": "double",
			"str": "string",
			"Int8": "int8",
			"UInt8": "uint8",
			"Int16": "int16",
			"UInt16": "uint16",
			"Int32": "int",
			"UInt32": "unsigned int",
			"Int64": "int64",
			"UInt64": "uint64",
			"Float32": "float",
			"Float64": "double",
		}
		if leaf in self.class_names:
			if type_ref.arguments:
				raise _Unsupported(node, f"type arguments on nominal type {type_ref.spelling}")
			return self.identifier(leaf, node, "type")
		if leaf in primitives:
			if type_ref.arguments:
				raise _Unsupported(node, f"type arguments on scalar type {type_ref.spelling}")
			result = primitives[leaf]
			if result == "void" and not allow_void:
				raise _Unsupported(node, f"None type for {subject}")
			return result
		unary = {
			"list": "array",
			"List": "array",
			"Sequence": "array",
			"MutableSequence": "array",
			"set": "set",
			"Set": "set",
			"deque": "deque",
			"Deque": "deque",
		}
		if leaf in unary and len(type_ref.arguments) == 1:
			inner = self.type_name(type_ref.arguments[0], node, subject)
			return f"{unary[leaf]}<{inner}>"
		if leaf in {"dict", "Dict", "Mapping", "MutableMapping"} and len(type_ref.arguments) == 2:
			key = self.type_name(type_ref.arguments[0], node, subject)
			value = self.type_name(type_ref.arguments[1], node, subject)
			return f"map<{key},{value}>"
		raise _Unsupported(node, f"type annotation {type_ref.spelling}")

	def expression(self, node: ast.expr) -> str:
		if isinstance(node, ast.Constant):
			if node.value is None:
				raise _Unsupported(
					node,
					"None literal without a supported optional type",
				)
			if node.value is True:
				return "true"
			if node.value is False:
				return "false"
			if isinstance(node.value, str):
				try:
					node.value.encode("utf-8")
				except UnicodeEncodeError:
					raise _Unsupported(node, "string literal containing surrogate code points") from None
				return json.dumps(node.value, ensure_ascii=False)
			if isinstance(node.value, int):
				if not _VEROSCRIPT_INT_MIN <= node.value <= _VEROSCRIPT_INT_MAX:
					raise _Unsupported(node, "integer literal out of VeroScript int range")
				return repr(node.value)
			if isinstance(node.value, float):
				if not math.isfinite(node.value):
					raise _Unsupported(node, "non-finite floating-point literal")
				return repr(node.value)
			raise _Unsupported(node, f"constant {type(node.value).__name__}")
		if isinstance(node, ast.Name):
			if node.id == "self" and self.has_instance_receiver:
				return "this"
			return self.identifier(node.id, node, "name")
		if isinstance(node, ast.Attribute):
			attribute = self.identifier(node.attr, node, "attribute")
			return f"{self.expression(node.value)}.{attribute}"
		if isinstance(node, ast.Subscript):
			if (
				isinstance(node.value, ast.Constant) and isinstance(node.value.value, str)
				or isinstance(node.value, ast.Name) and node.value.id in self.string_locals
			):
				raise _Unsupported(node, "Python string indexing requires Unicode code point semantics")
			self.uses_index_helpers = True
			return f"{self.index_getter}({self.expression(node.value)}, {self.expression(node.slice)})"
		if isinstance(node, ast.BinOp):
			binary_operators: dict[type[ast.operator], str | None] = {
				ast.Add: "+",
				ast.Sub: "-",
				ast.Mult: "*",
				ast.Div: None,
				ast.FloorDiv: None,
				ast.Mod: None,
				ast.Pow: None,
				ast.LShift: "<<",
				ast.RShift: ">>",
				ast.BitOr: "|",
				ast.BitXor: "^",
				ast.BitAnd: "&",
			}
			operator = binary_operators.get(type(node.op))
			if operator is None:
				raise _Unsupported(node, f"binary operator {type(node.op).__name__}")
			return f"({self.expression(node.left)} {operator} {self.expression(node.right)})"
		if isinstance(node, ast.UnaryOp):
			if (
				isinstance(node.op, ast.USub)
				and isinstance(node.operand, ast.Constant)
				and type(node.operand.value) is int
				and node.operand.value == -_VEROSCRIPT_INT_MIN
			):
				return f"({_VEROSCRIPT_INT_MIN})"
			unary_operators: dict[type[ast.unaryop], str] = {
				ast.Not: "!",
				ast.USub: "-",
				ast.UAdd: "+",
				ast.Invert: "~",
			}
			operator = unary_operators.get(type(node.op))
			if operator is None:
				raise _Unsupported(node, f"unary operator {type(node.op).__name__}")
			return f"({operator}{self.expression(node.operand)})"
		if isinstance(node, ast.BoolOp):
			raise _Unsupported(node, "and/or expression without proven bool operands")
		if isinstance(node, ast.Compare):
			if len(node.ops) != 1 or len(node.comparators) != 1:
				raise _Unsupported(node, "chained comparison")
			comparison_operators: dict[type[ast.cmpop], str] = {
				ast.Eq: "==",
				ast.NotEq: "!=",
				ast.Lt: "<",
				ast.LtE: "<=",
				ast.Gt: ">",
				ast.GtE: ">=",
			}
			if isinstance(node.ops[0], (ast.Is, ast.IsNot)):
				raise _Unsupported(
					node,
					"identity comparison without a supported optional type",
				)
			operator = comparison_operators.get(type(node.ops[0]))
			if operator is None:
				raise _Unsupported(node, f"comparison {type(node.ops[0]).__name__}")
			return f"({self.expression(node.left)} {operator} {self.expression(node.comparators[0])})"
		if isinstance(node, ast.IfExp):
			return (
				f"({self.expression(node.test)} ? {self.expression(node.body)} : "
				f"{self.expression(node.orelse)})"
			)
		if isinstance(node, ast.Call):
			if node.keywords:
				raise _Unsupported(node, "keyword arguments")
			name = qualified_name(node.func)
			if name in _BUILTIN_CALLS and (
				name in self.local_bindings or name in self.module_bindings
			):
				raise _Unsupported(node, f"Python binding {name} would be intercepted by a VeroScript builtin")
			forbidden = {
				"eval",
				"exec",
				"compile",
				"globals",
				"locals",
				"vars",
				"getattr",
				"setattr",
				"delattr",
				"__import__",
			}
			if name in forbidden:
				raise _Unsupported(node, f"dynamic call {name}")
			if name == "str" and (
				name in self.local_bindings
				or name not in self.module_bindings
				or name not in self.class_names and name not in self.function_names
			):
				raise _Unsupported(node, "builtin str conversion without preserved Python formatting semantics")
			if name == "len":
				if len(node.args) != 1:
					raise _Unsupported(node, "len() requires exactly one argument")
				argument_type = self.expression_type(node.args[0])
				if argument_type == "string":
					if isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str):
						self.expression(node.args[0])
						return str(len(node.args[0].value))
					self.uses_string_length = True
					return f"{self.string_length}({self.expression(node.args[0])})"
				if not argument_type or not argument_type.startswith(("array<", "map<", "set<", "deque<")):
					raise _Unsupported(node, "len() requires a known string or container type; store the value in an annotated local first")
			if (
				name in {"int", "float", "bool"}
				and name not in self.class_names
				and name not in self.function_names
				and len(node.args) == 1
			):
				casts = {"int": "int", "float": "double", "bool": "bool"}
				return f"cast<{casts[name]}>({self.expression(node.args[0])})"
			if isinstance(node.func, ast.Name):
				allowed = self.function_names | self.class_names | _BUILTIN_CALLS
				if node.func.id not in allowed:
					raise _Unsupported(node, f"unknown call {node.func.id}")
			arguments = ", ".join(self.expression(argument) for argument in node.args)
			return f"{self.expression(node.func)}({arguments})"
		if isinstance(node, (ast.List, ast.Set, ast.Dict)):
			raise _Unsupported(node, "container literal outside a typed initializer")
		if isinstance(node, ast.Tuple):
			raise _Unsupported(node, "tuple expression")
		raise _Unsupported(node, type(node).__name__)

	def initializer(self, node: ast.expr) -> str:
		if isinstance(node, (ast.List, ast.Set)):
			return "{" + ", ".join(self.initializer(value) for value in node.elts) + "}"
		if isinstance(node, ast.Dict):
			pairs: list[str] = []
			for key, value in zip(node.keys, node.values):
				if key is None:
					raise _Unsupported(node, "dictionary unpacking")
				pairs.append(f"{self.initializer(key)}: {self.initializer(value)}")
			return "{" + ", ".join(pairs) + "}"
		return self.expression(node)

	def target(self, node: ast.expr) -> str:
		if (
			isinstance(node, ast.Name)
			and node.id == "self"
			and self.has_instance_receiver
		):
			raise _Unsupported(node, "assignment to instance receiver self")
		if isinstance(node, (ast.Name, ast.Attribute, ast.Subscript)):
			return self.expression(node)
		raise _Unsupported(node, f"assignment target {type(node).__name__}")

	def statement(
		self,
		node: ast.stmt,
		depth: int = 1,
		loop_depth: int = 0,
	) -> None:
		# The final statement in a nested body also consumes one parser level.
		# The core limit leaves room for the native stack used by each control
		# statement and its block, even though the block is parsed separately.
		if depth > _MAX_VEROSCRIPT_STATEMENT_NESTING:
			raise _Unsupported(node, "statement nested too deeply")
		if isinstance(node, ast.Return):
			returns_none = (
				isinstance(node.value, ast.Constant)
				and node.value.value is None
			)
			if node.value is None or returns_none:
				if not self.current_function_returns_void:
					raise _Unsupported(
						node,
						"return without a value in a non-void function",
					)
				self.line("return;", node)
				return
			if self.current_function_returns_void:
				raise _Unsupported(node, "return value in a void function")
			self.line(f"return {self.expression(node.value)};", node)
			return
		if isinstance(node, ast.AnnAssign):
			if not isinstance(node.target, ast.Name):
				raise _Unsupported(node, "annotated non-name assignment")
			if depth != 1:
				raise _Unsupported(
					node,
					"annotated local declaration inside control flow",
				)
			if node.target.id == "self" and self.has_instance_receiver:
				raise _Unsupported(node, "redeclaration of instance receiver self")
			if self.scopes and node.target.id in self.scopes[-1]:
				raise _Unsupported(
					node,
					f"duplicate local declaration {node.target.id}",
				)
			target_name = self.identifier(node.target.id, node, "local variable")
			type_ref = parse_type_annotation(node.annotation, self.module.source_path)
			value = "" if node.value is None else f" = {self.initializer(node.value)}"
			declared_type = self.type_name(type_ref, node, node.target.id)
			self.local_types[node.target.id] = declared_type
			self.line(f"{declared_type} {target_name}{value};", node)
			if declared_type == "string":
				self.string_locals.add(node.target.id)
			if self.scopes:
				self.scopes[-1].add(node.target.id)
			return
		if isinstance(node, ast.Assign):
			if len(node.targets) != 1:
				raise _Unsupported(node, "multiple assignment")
			target = node.targets[0]
			if isinstance(target, ast.Subscript):
				self.indexed_assignment(target, node.value)
				return
			if isinstance(target, ast.Attribute):
				self.attribute_assignment(target, node.value)
				return
			if isinstance(target, ast.Name) and self.scopes and target.id not in self.scopes[-1]:
				raise _Unsupported(node, f"unannotated local declaration {target.id}")
			self.line(f"{self.target(target)} = {self.expression(node.value)};", node)
			return
		if isinstance(node, ast.AugAssign):
			operators = {
				ast.Add: "+=",
				ast.Sub: "-=",
				ast.Mult: "*=",
				ast.BitOr: "|=",
				ast.BitXor: "^=",
				ast.BitAnd: "&=",
				ast.LShift: "<<=",
				ast.RShift: ">>=",
			}
			operator = operators.get(type(node.op))
			if operator is None:
				raise _Unsupported(node, f"augmented operator {type(node.op).__name__}")
			if isinstance(node.target, ast.Subscript):
				self.indexed_assignment(node.target, node.value, operator[:-1])
				return
			self.line(f"{self.target(node.target)} {operator} {self.expression(node.value)};", node)
			return
		if isinstance(node, ast.Expr):
			if isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
				return
			self.line(f"{self.expression(node.value)};", node)
			return
		if isinstance(node, ast.If):
			self.line(f"if ({self.expression(node.test)})", node)
			self.line("{")
			self.indent += 1
			for child in node.body:
				self.statement(child, depth + 1, loop_depth)
			self.indent -= 1
			self.line("}")
			if node.orelse:
				self.line("else")
				self.line("{")
				self.indent += 1
				for child in node.orelse:
					self.statement(child, depth + 1, loop_depth)
				self.indent -= 1
				self.line("}")
			return
		if isinstance(node, ast.While):
			if node.orelse:
				raise _Unsupported(node, "while-else")
			self.line(f"while ({self.expression(node.test)})", node)
			self.line("{")
			self.indent += 1
			for child in node.body:
				self.statement(child, depth + 1, loop_depth + 1)
			self.indent -= 1
			self.line("}")
			return
		if isinstance(node, ast.Break):
			if loop_depth == 0:
				raise _Unsupported(node, "break outside a loop")
			self.line("break;", node)
			return
		if isinstance(node, ast.Continue):
			if loop_depth == 0:
				raise _Unsupported(node, "continue outside a loop")
			self.line("continue;", node)
			return
		if isinstance(node, ast.Pass):
			self.line("{}", node)
			return
		raise _Unsupported(node, type(node).__name__)

	def function(self, entry: FunctionDecl, owner: ClassDecl | None = None) -> None:
		node = entry.node
		if entry.is_async:
			raise _Unsupported(node, f"async function {entry.name}")
		constructor = owner is not None and entry.name == "__init__"
		if owner is None and entry.decorators:
			raise _Unsupported(node, f"decorator {entry.decorators[0]}")
		if owner is not None and (
			(constructor and entry.decorators)
			or (not constructor and entry.decorators not in {(), ("staticmethod",)})
		):
			raise _Unsupported(node, f"decorator {entry.decorators[0]}")
		static = entry.decorators == ("staticmethod",)
		parameters = list(entry.parameters)
		if owner is not None and not static:
			if (
				not parameters
				or parameters[0].name != "self"
				or parameters[0].kind
				not in {"positional", "positional_only"}
				or parameters[0].default is not None
			):
				raise _Unsupported(node, f"receiver of {entry.name}")
			parameters.pop(0)
		rendered_parameters: list[str] = []
		string_parameters: set[str] = set()
		parameter_types: dict[str, str] = {}
		scope = {parameter.name for parameter in parameters}
		for parameter in parameters:
			if parameter.kind not in {"positional", "positional_only"}:
				raise _Unsupported(node, f"parameter {parameter.name}")
			parameter_name = self.identifier(
				parameter.name, node, "parameter"
			)
			parameter_type = self.type_name(parameter.type, node, parameter.name)
			parameter_types[parameter.name] = parameter_type
			if parameter_type == "string":
				string_parameters.add(parameter.name)
			default = ""
			if parameter.default is not None:
				default_node = ast.parse(parameter.default, mode="eval").body
				default = f" = {self.expression(default_node)}"
			rendered_parameters.append(f"{parameter_type} {parameter_name}{default}")
		if constructor:
			assert owner is not None
			declared_return = self.type_name(
				entry.return_type,
				node,
				f"return value of {entry.name}",
				allow_void=True,
			)
			if declared_return != "void":
				raise _Unsupported(node, f"constructor return type {declared_return}")
			header = f"{owner.name}({', '.join(rendered_parameters)})"
			returns_void = True
		else:
			function_name = self.identifier(entry.name, node, "function")
			return_type = self.type_name(
				entry.return_type,
				node,
				f"return value of {entry.name}",
				allow_void=True,
			)
			export = "export " if owner is None and self.export_declarations else ""
			header = f"{export}{'static ' if owner is not None and static else ''}{return_type} {function_name}({', '.join(rendered_parameters)})"
			returns_void = return_type == "void"
		body = node.body[1:] if ast.get_docstring(node, clean=False) is not None else node.body
		if not returns_void and not _statements_guarantee_return(body):
			raise _Unsupported(
				node,
				f"non-void function {entry.name} does not return on every path",
			)
		self.line(header, node)
		self.line("{")
		self.indent += 1
		self.scopes.append(scope)
		previous_receiver = self.has_instance_receiver
		previous_returns_void = self.current_function_returns_void
		previous_string_locals = self.string_locals
		previous_local_types = self.local_types
		previous_class_name = self.current_class_name
		previous_bindings = self.local_bindings
		self.local_bindings = scope | {
			child.id for statement in body for child in ast.walk(statement)
			if isinstance(child, ast.Name) and isinstance(child.ctx, ast.Store)
		}
		self.local_types = parameter_types
		self.current_class_name = owner.name if owner is not None else None
		self.string_locals = string_parameters
		self.has_instance_receiver = owner is not None and not static
		self.current_function_returns_void = returns_void
		try:
			for child in body:
				self.statement(child)
		finally:
			self.has_instance_receiver = previous_receiver
			self.current_function_returns_void = previous_returns_void
			self.string_locals = previous_string_locals
			self.local_types = previous_local_types
			self.current_class_name = previous_class_name
			self.local_bindings = previous_bindings
			self.scopes.pop()
			self.indent -= 1
		self.line("}")

	def enum(self, entry: ClassDecl) -> None:
		if not any(
			base.rsplit(".", 1)[-1] in {"IntEnum", "IntFlag"}
			for base in entry.bases
		):
			raise _Unsupported(
				entry.node,
				f"enum {entry.name} must derive from IntEnum or IntFlag",
			)
		export = "export " if self.export_declarations else ""
		self.line(f"{export}enum class {entry.name} : int64", entry.node)
		self.line("{")
		self.indent += 1
		values: dict[str, int] = {}
		for index, (name, expression, _) in enumerate(entry.enum_values):
			member_name = self.identifier(name, expression, "enum member")
			value = evaluate_integer_expression(expression, values)
			if value is None or value < -(1 << 63) or value >= (1 << 63):
				raise _Unsupported(
					expression,
					f"enum member {name} must be a constant signed 64-bit integer",
				)
			rendered = str(value)
			if value < _VEROSCRIPT_INT_MIN or value > _VEROSCRIPT_INT_MAX:
				rendered += "i64"
			suffix = "," if index + 1 < len(entry.enum_values) else ""
			self.line(f"{member_name} = {rendered}{suffix}", expression)
			values[name] = value
		self.indent -= 1
		self.line("};")

	def class_decl(self, entry: ClassDecl) -> None:
		class_name = self.identifier(entry.name, entry.node, "class")
		if entry.node.keywords:
			raise _Unsupported(entry.node, "class keyword arguments, including metaclass, require unsupported runtime behavior")
		if entry.is_enum:
			if entry.decorators:
				raise _Unsupported(entry.node, f"decorator {entry.decorators[0]}")
			self.enum(entry)
			return
		if entry.is_dataclass:
			raise _Unsupported(entry.node, "dataclass generated runtime behavior")
		if entry.decorators:
			raise _Unsupported(entry.node, f"decorator {entry.decorators[0]}")
		unsupported_bases = [base for base in entry.bases if base.rsplit(".", 1)[-1] != "object"]
		if unsupported_bases:
			raise _Unsupported(entry.node, f"inheritance {', '.join(unsupported_bases)}")
		# Ordinary Python instances have reference semantics.
		export = "export " if self.export_declarations else ""
		self.line(f"{export}class {class_name}", entry.node)
		self.line("{")
		self.line("public:")
		self.indent += 1
		for field in entry.fields:
			if field.is_class_variable:
				raise _Unsupported(entry.node, f"class variable {field.name}")
			field_name = self.identifier(field.name, entry.node, "field")
			field_type = self.type_name(field.type, entry.node, f"field {entry.name}.{field.name}")
			if field.initializer is not None and not self.immutable_field_default(field.initializer):
				raise _Unsupported(
					field.initializer,
					f"class field default {entry.name}.{field.name} requires an immutable scalar literal or constant enum member; initialize instance values in __init__",
				)
			initializer = "" if field.initializer is None else f" = {self.initializer(field.initializer)}"
			self.line(f"{field_type} {field_name}{initializer};", field.initializer or entry.node)
		for method in entry.methods:
			decorators = set(method.decorators)
			if "property" in {value.rsplit(".", 1)[-1] for value in decorators} or any(
				value.endswith(".setter") for value in decorators
			):
				raise _Unsupported(method.node, f"property {method.name}")
			self.function(method, entry)
		self.indent -= 1
		self.line("};")

	def immutable_field_default(self, node: ast.expr) -> bool:
		# Python evaluates class defaults once, while Core initializes each instance.
		if isinstance(node, ast.Constant) and type(node.value) in {bool, int, float, str}:
			return True
		if (
			isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub))
			and isinstance(node.operand, ast.Constant) and type(node.operand.value) in {int, float}
		):
			return True
		if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id in self.enum_names:
			return True
		if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add) and all(
			isinstance(part, (ast.BinOp, ast.Add)) or isinstance(part, ast.Constant) and type(part.value) is str
			for part in ast.walk(node)
		):
			return True
		if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult)):
			try:
				left, right = ast.literal_eval(node.left), ast.literal_eval(node.right)
			except (ValueError, TypeError):
				return evaluate_integer_expression(node, {}) is not None
			if type(left) in {int, float} and type(right) in {int, float} and (type(left) is float or type(right) is float):
				try:
					value = left + right if isinstance(node.op, ast.Add) else left - right if isinstance(node.op, ast.Sub) else left * right
				except OverflowError:
					return False
				return math.isfinite(value)
		return evaluate_integer_expression(node, {}) is not None

	def emit(self) -> TranspileResult:
		if not self.module.succeeded:
			return TranspileResult("", "", tuple(self.diagnostics))
		self.line("// SPDX-FileCopyrightText: VanstySanic")
		self.line("// SPDX-License-Identifier: MIT")
		self.line()
		self.line("// Generated from typed Python by VeroScriptPythonTranspiler.")
		self.line("// Unsupported dynamic Python semantics are rejected before output.")
		self.line()
		try:
			module_node = self.module.node or ast.Module(body=[], type_ignores=[])
			allowed_imports = {
				"__future__",
				"typing",
				"typing_extensions",
				"dataclasses",
				"enum",
				"collections",
			}
			# Core imports retain declaration names; ignoring a rename could call
			# another exported declaration with the alias's name.
			for statement in module_node.body:
				if isinstance(statement, ast.Import):
					aliases = [alias for alias in statement.names if alias.name in self.project_imports]
				elif isinstance(statement, ast.ImportFrom):
					import_name = "." * statement.level + (statement.module or "")
					aliases = statement.names if import_name in self.project_imports else []
				else:
					continue
				for alias in aliases:
					if alias.asname is not None and alias.asname != alias.name:
						raise _Unsupported(alias, f"renaming project imports ({alias.name} as {alias.asname})")
			emitted_imports: set[str] = set()
			for imported in self.module.imports:
				root = imported.lstrip(".").partition(".")[0]
				project_outputs = self.project_imports.get(imported)
				if project_outputs is not None:
					for project_output in project_outputs:
						if project_output not in emitted_imports:
							quoted_output = json.dumps(
								project_output,
								ensure_ascii=False,
							)
							self.line(f"import {quoted_output};")
							emitted_imports.add(project_output)
				elif imported.startswith(".") or (root and root not in allowed_imports):
					raise _Unsupported(module_node, f"import {imported}")
			if emitted_imports:
				self.line()
			for class_entry in self.module.classes:
				if not class_entry.name.startswith("_"):
					self.class_decl(class_entry)
					self.line()
			for variable in self.module.variables:
				if variable.is_class_variable:
					raise _Unsupported(module_node, f"module ClassVar {variable.name}")
				variable_name = self.identifier(variable.name, module_node, "variable")
				type_name = self.type_name(variable.type, module_node, variable.name)
				initializer = "" if variable.initializer is None else f" = {self.initializer(variable.initializer)}"
				export = "export " if self.export_declarations else ""
				self.line(f"{export}{type_name} {variable_name}{initializer};", variable.initializer)
			if self.module.variables:
				self.line()
			for function_entry in self.module.functions:
				if not function_entry.name.startswith("_"):
					self.function(function_entry)
					self.line()
			self.emit_index_helpers()
			self.emit_string_length(module_node)
		except _Unsupported as error:
			self.fail(error.node, error.detail)
			return TranspileResult("", "", tuple(self.diagnostics))
		except RecursionError:
			self.fail(module_node, "excessive source nesting")
			return TranspileResult("", "", tuple(self.diagnostics))
		source = "\n".join(self.lines).rstrip() + "\n"
		source_map = {
			"version": 1,
			"source": self.module.source_path,
			"generated": self.options.generated_source_path
			or f"{Path(self.module.source_path).stem}.vs",
			"mappings": self.mappings,
		}
		return TranspileResult(
			source,
			json.dumps(source_map, ensure_ascii=False, indent=2) + "\n",
			tuple(self.diagnostics),
		)


def _validator(options: TranspileOptions) -> str | None:
	return find_veroscript_executable(options.veroscript_executable)


def _validate_source(
	result: TranspileResult,
	module: ModuleDecl,
	options: TranspileOptions,
) -> TranspileResult:
	if not result.succeeded or not options.validate_generated_source:
		return result
	localizer = Localizer(options.language, options.catalog_directory)
	if not math.isfinite(options.timeout_seconds) or options.timeout_seconds <= 0:
		diagnostic = Diagnostic(
			"PYT2004",
			localizer.message("python_tools.timeout_invalid"),
			SourceLocation(module.source_path),
		)
		return TranspileResult("", "", (*result.diagnostics, diagnostic))
	executable = _validator(options)
	if not executable:
		diagnostic = Diagnostic(
			"PYT2002",
			localizer.message("python_tools.validator_not_found"),
			SourceLocation(module.source_path),
		)
		return TranspileResult("", "", (*result.diagnostics, diagnostic))
	try:
		with tempfile.TemporaryDirectory() as directory:
			generated = Path(directory) / "generated.vs"
			generated.write_text(result.source, encoding="utf-8", newline="\n")
			completed = subprocess.run(
				[str(executable), "--check", str(generated)],
				check=False,
				capture_output=True,
				text=True,
				encoding="utf-8",
				errors="replace",
				timeout=options.timeout_seconds,
			)
	except (OSError, subprocess.TimeoutExpired) as error:
		detail = str(error)
	else:
		if completed.returncode == 0:
			return result
		detail = "\n".join(
			value.strip() for value in (completed.stdout, completed.stderr) if value.strip()
		)
	diagnostic = Diagnostic(
		"PYT2003",
		localizer.message("python_tools.generated_validation_failed", detail),
		SourceLocation(module.source_path),
	)
	return TranspileResult("", "", (*result.diagnostics, diagnostic))


def transpile(
	module: ModuleDecl,
	options: TranspileOptions = TranspileOptions(),
) -> TranspileResult:
	return _validate_source(_Emitter(module, options).emit(), module, options)


def transpile_source(
	source: str,
	source_path: str = "<python-source>",
	options: TranspileOptions = TranspileOptions(),
	*,
	module_name: str | None = None,
) -> TranspileResult:
	module = parse_source(
		source,
		source_path,
		module_name=module_name,
		language=options.language,
		catalog_directory=options.catalog_directory,
	)
	return transpile(module, options)


def transpile_file(
	path: str | Path,
	options: TranspileOptions = TranspileOptions(),
	*,
	module_name: str | None = None,
) -> TranspileResult:
	module = parse_file(
		path,
		module_name=module_name,
		language=options.language,
		catalog_directory=options.catalog_directory,
	)
	return transpile(module, options)


def _project_import_map(
	project_module: ProjectModule,
	module_outputs: dict[str, str],
	localizer: Localizer,
) -> tuple[dict[str, tuple[str, ...]], tuple[Diagnostic, ...]]:
	imports: dict[str, tuple[str, ...]] = {
		name: (output,) for name, output in module_outputs.items()
	}
	diagnostics: list[Diagnostic] = []
	tree = project_module.module.node
	if tree is None:
		return imports, tuple(diagnostics)

	module_parts = project_module.module.name.split(".")
	is_package = Path(project_module.relative_path).stem == "__init__"
	package_parts = module_parts if is_package else module_parts[:-1]
	for node in tree.body:
		if not isinstance(node, ast.ImportFrom) or node.level == 0:
			continue
		ascend = node.level - 1
		detail = "." * node.level + (
			node.module or ",".join(alias.name for alias in node.names)
		)
		if ascend >= len(package_parts):
			diagnostics.append(
				Diagnostic(
					"PYT2001",
					localizer.message(
						"python_tools.unsupported_construct",
						f"unresolved project-relative import {detail}",
					),
					SourceLocation(
						project_module.module.source_path,
						int(getattr(node, "lineno", 1)),
						int(getattr(node, "col_offset", 0)) + 1,
						int(getattr(node, "end_lineno", getattr(node, "lineno", 1))),
						int(getattr(node, "end_col_offset", getattr(node, "col_offset", 0))) + 1,
					),
				)
			)
			continue
		resolved_base = package_parts[: len(package_parts) - ascend]
		outputs: list[str] = []
		if node.module:
			resolved_name = ".".join((*resolved_base, *node.module.split(".")))
			output = module_outputs.get(resolved_name)
			if output is not None:
				outputs.append(output)
		else:
			for alias in node.names:
				resolved_name = ".".join((*resolved_base, *alias.name.split(".")))
				output = module_outputs.get(resolved_name)
				if output is not None and output not in outputs:
					outputs.append(output)
		if outputs:
			imports["." * node.level + (node.module or "")] = tuple(outputs)
		else:
			diagnostics.append(
				Diagnostic(
					"PYT2001",
					localizer.message(
						"python_tools.unsupported_construct",
						f"unresolved project-relative import {detail}",
					),
					SourceLocation(
						project_module.module.source_path,
						int(getattr(node, "lineno", 1)),
						int(getattr(node, "col_offset", 0)) + 1,
						int(getattr(node, "end_lineno", getattr(node, "lineno", 1))),
						int(getattr(node, "end_col_offset", getattr(node, "col_offset", 0))) + 1,
					),
				)
			)
	return imports, tuple(diagnostics)


def transpile_files(
	paths: list[str | Path] | tuple[str | Path, ...],
	options: TranspileProjectOptions = TranspileProjectOptions(),
	*,
	project_root: str | Path | None = None,
) -> TranspileProjectResult:
	project = parse_files(
		paths,
		project_root=project_root,
		language=options.language,
		catalog_directory=options.catalog_directory,
	)
	diagnostics = list(project.diagnostics)
	if not project.succeeded:
		return TranspileProjectResult((), "", tuple(diagnostics))
	if options.validate_generated_sources and (
		not math.isfinite(options.timeout_seconds) or options.timeout_seconds <= 0
	):
		diagnostics.append(
			Diagnostic(
				"PYT2004",
				Localizer(options.language, options.catalog_directory).message(
					"python_tools.timeout_invalid"
				),
				SourceLocation("<python-project>"),
			)
		)
		return TranspileProjectResult((), "", tuple(diagnostics))

	class_counts: dict[str, int] = {}
	enum_counts: dict[str, int] = {}
	type_value_counts: dict[str, int] = {}
	function_counts: dict[str, int] = {}
	module_outputs: dict[str, str] = {}
	for entry in project.modules:
		output_name = Path(entry.relative_path).with_suffix(".vs").as_posix()
		module_outputs[entry.module.name] = output_name
		for class_entry in entry.module.classes:
			if class_entry.name.startswith("_"):
				continue
			counts = enum_counts if class_entry.is_enum else class_counts
			counts[class_entry.name] = counts.get(class_entry.name, 0) + 1
			type_value_counts[class_entry.name] = (
				type_value_counts.get(class_entry.name, 0) + 1
			)
		for variable in entry.module.variables:
			type_value_counts[variable.name] = (
				type_value_counts.get(variable.name, 0) + 1
			)
		for function in entry.module.functions:
			if not function.name.startswith("_"):
				function_counts[function.name] = function_counts.get(function.name, 0) + 1

	localizer = Localizer(options.language, options.catalog_directory)
	for name, count in (*type_value_counts.items(), *function_counts.items()):
		if count > 1:
			diagnostics.append(
				Diagnostic(
					"PYT2004",
					localizer.message(
						"python_tools.duplicate_declaration", f"project symbol {name}"
					),
					SourceLocation("<python-project>"),
				)
			)
	if any(item.severity == "error" for item in diagnostics):
		return TranspileProjectResult((), "", tuple(diagnostics))

	known_classes = set(class_counts)
	known_enums = set(enum_counts)
	known_functions = set(function_counts)
	known_class_declarations = {
		entry.name: entry for module in project.modules for entry in module.module.classes
		if not entry.name.startswith("_")
	}
	known_function_declarations = {
		entry.name: entry for module in project.modules for entry in module.module.functions
		if not entry.name.startswith("_")
	}
	generated_files: list[TranspiledFile] = []
	for project_module in project.modules:
		output_name = module_outputs[project_module.module.name]
		project_imports, import_diagnostics = _project_import_map(
			project_module, module_outputs, localizer
		)
		diagnostics.extend(import_diagnostics)
		module_options = TranspileOptions(
			validate_generated_source=False,
			generated_source_path=output_name,
			language=options.language,
			catalog_directory=options.catalog_directory,
		)
		emitted = _Emitter(
			project_module.module,
			module_options,
			known_class_names=known_classes,
			known_enum_names=known_enums,
			known_function_names=known_functions,
			known_class_declarations=known_class_declarations,
			known_function_declarations=known_function_declarations,
			project_imports=project_imports,
			export_declarations=True,
		).emit()
		diagnostics.extend(
			item for item in emitted.diagnostics
			if item not in project_module.module.diagnostics
		)
		if emitted.succeeded:
			generated_files.append(
				TranspiledFile(
					project_module.input_path,
					output_name,
					emitted.source,
					emitted.source_map_json,
				)
			)
	if any(item.severity == "error" for item in diagnostics):
		return TranspileProjectResult((), "", tuple(diagnostics))

	if options.validate_generated_sources:
		executable = find_veroscript_executable(options.veroscript_executable)
		if not executable:
			diagnostics.append(
				Diagnostic(
					"PYT2002",
					localizer.message("python_tools.validator_not_found"),
					SourceLocation("<python-project>"),
				)
			)
		else:
			try:
				with tempfile.TemporaryDirectory() as directory:
					root = Path(directory)
					for generated in generated_files:
						destination = root / generated.output_name
						destination.parent.mkdir(parents=True, exist_ok=True)
						destination.write_text(
							generated.source, encoding="utf-8", newline="\n"
						)
					for generated in generated_files:
						completed = subprocess.run(
							[
								str(executable),
								"--check",
								"--allow-project-wide-access",
								"-I",
								str(root),
								str(root / generated.output_name),
							],
							check=False,
							capture_output=True,
							text=True,
							encoding="utf-8",
							errors="replace",
							timeout=options.timeout_seconds,
						)
						if completed.returncode != 0:
							detail = "\n".join(
								value.strip()
								for value in (completed.stdout, completed.stderr)
								if value.strip()
							)
							diagnostics.append(
								Diagnostic(
									"PYT2003",
									localizer.message(
										"python_tools.generated_validation_failed", detail
									),
									SourceLocation(generated.input_path),
								)
							)
							break
			except (OSError, subprocess.TimeoutExpired) as error:
				diagnostics.append(
					Diagnostic(
						"PYT2003",
						localizer.message(
							"python_tools.generated_validation_failed", error
						),
						SourceLocation("<python-project>"),
					)
				)
	if any(item.severity == "error" for item in diagnostics):
		return TranspileProjectResult((), "", tuple(diagnostics))

	manifest = {
		"version": 1,
		"kind": "veroscript.python-transpiled-project",
		"root": project.root,
		"allowProjectWideAccess": True,
		"modulePaths": [entry.output_name for entry in generated_files],
		"implicitImportPaths": [entry.output_name for entry in generated_files],
		"files": [
			{
				"input": entry.input_path,
				"source": entry.output_name,
				"sourceMap": f"{entry.output_name}.map.json",
			}
			for entry in generated_files
		],
	}
	return TranspileProjectResult(
		tuple(generated_files),
		json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
		tuple(diagnostics),
	)


__all__ = [
	"__version__",
	"TranspileOptions",
	"TranspileResult",
	"TranspiledFile",
	"TranspileProjectOptions",
	"TranspileProjectResult",
	"transpile",
	"transpile_file",
	"transpile_files",
	"transpile_source",
]
