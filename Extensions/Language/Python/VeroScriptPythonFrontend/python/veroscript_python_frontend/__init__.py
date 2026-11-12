# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import ast
from dataclasses import dataclass, field
import os
from pathlib import Path
import sys
import tempfile
from typing import Any, Iterable, Mapping, Sequence
import uuid

from .localization import Localizer
from ._toolchain import find_veroscript_executable
from ._version import discover_version


__version__ = discover_version()


# Keep this set synchronized with VeroScriptLexer::GetLanguageKeywords().
# Python accepts several of these spellings as identifiers (including ``do``),
# while the VeroScript lexer deliberately classifies them as keywords.
_VEROSCRIPT_KEYWORDS = frozenset(
	{
		"array",
		"auto",
		"bool",
		"break",
		"case",
		"catch",
		"channel",
		"char",
		"class",
		"const",
		"constexpr",
		"continue",
		"default",
		"delete",
		"deque",
		"do",
		"double",
		"else",
		"enum",
		"explicit",
		"export",
		"fallthrough",
		"false",
		"final",
		"finally",
		"float",
		"fn",
		"for",
		"friend",
		"function",
		"goto",
		"if",
		"import",
		"in",
		"int",
		"int8",
		"int16",
		"int32",
		"int64",
		"interface",
		"long",
		"map",
		"multimap",
		"multiset",
		"namespace",
		"new",
		"nullptr",
		"operator",
		"ordered_map",
		"ordered_multimap",
		"ordered_multiset",
		"override",
		"priority_queue",
		"private",
		"protected",
		"public",
		"queue",
		"ref",
		"return",
		"set",
		"short",
		"stack",
		"static",
		"string",
		"struct",
		"switch",
		"template",
		"this",
		"throw",
		"true",
		"try",
		"typedef",
		"uint8",
		"uint16",
		"uint32",
		"uint64",
		"unsigned",
		"using",
		"virtual",
		"void",
		"where",
		"while",
		"wstring",
	}
)


def is_veroscript_identifier(value: str) -> bool:
	"""Return whether *value* is a non-keyword ASCII VeroScript identifier."""

	if not value or value in _VEROSCRIPT_KEYWORDS:
		return False
	first = value[0]
	if not ("a" <= first <= "z" or "A" <= first <= "Z" or first == "_"):
		return False
	return all(
		"a" <= character <= "z"
		or "A" <= character <= "Z"
		or "0" <= character <= "9"
		or character == "_"
		for character in value[1:]
	)


def evaluate_integer_expression(
	node: ast.expr,
	names: Mapping[str, int] | None = None,
) -> int | None:
	"""Safely evaluate the integer-only expression subset used by enums."""

	try:
		return _evaluate_integer_expression(node, names)
	except RecursionError:
		return None


def _evaluate_integer_expression(
	node: ast.expr,
	names: Mapping[str, int] | None,
) -> int | None:
	values = names or {}
	if isinstance(node, ast.Constant) and type(node.value) is int:
		return node.value
	if isinstance(node, ast.Name):
		return values.get(node.id)
	if isinstance(node, ast.UnaryOp):
		operand = _evaluate_integer_expression(node.operand, values)
		if operand is None:
			return None
		if isinstance(node.op, ast.USub):
			return -operand
		if isinstance(node.op, ast.UAdd):
			return operand
		if isinstance(node.op, ast.Invert):
			return ~operand
		return None
	if isinstance(node, ast.BinOp):
		left = _evaluate_integer_expression(node.left, values)
		right = _evaluate_integer_expression(node.right, values)
		if left is None or right is None:
			return None
		if isinstance(node.op, ast.Add):
			return left + right
		if isinstance(node.op, ast.Sub):
			return left - right
		if isinstance(node.op, ast.Mult):
			return left * right
		if isinstance(node.op, ast.FloorDiv):
			return left // right if right != 0 else None
		if isinstance(node.op, ast.Mod):
			return left % right if right != 0 else None
		if isinstance(node.op, ast.LShift):
			return left << right if 0 <= right < 64 else None
		if isinstance(node.op, ast.RShift):
			return left >> right if 0 <= right < 64 else None
		if isinstance(node.op, ast.BitOr):
			return left | right
		if isinstance(node.op, ast.BitAnd):
			return left & right
		if isinstance(node.op, ast.BitXor):
			return left ^ right
	return None


@dataclass(frozen=True)
class SourceLocation:
	path: str
	line: int = 1
	column: int = 1
	end_line: int = 1
	end_column: int = 1

	def to_dict(self) -> dict[str, object]:
		return {
			"path": self.path,
			"line": self.line,
			"column": self.column,
			"endLine": self.end_line,
			"endColumn": self.end_column,
		}


@dataclass(frozen=True)
class Diagnostic:
	code: str
	message: str
	location: SourceLocation
	severity: str = "error"

	def format(self) -> str:
		return (
			f"{self.location.path}:{self.location.line}:{self.location.column}: "
			f"{self.severity} {self.code}: {self.message}"
		)

	def to_dict(self) -> dict[str, object]:
		return {
			"severity": self.severity,
			"code": self.code,
			"message": self.message,
			"location": self.location.to_dict(),
		}


@dataclass(frozen=True)
class TypeRef:
	spelling: str
	name: str
	arguments: tuple["TypeRef", ...] = ()
	optional: bool = False
	location: SourceLocation = field(default_factory=lambda: SourceLocation("<python>"))

	def to_dict(self) -> dict[str, object]:
		return {
			"spelling": self.spelling,
			"name": self.name,
			"arguments": [argument.to_dict() for argument in self.arguments],
			"optional": self.optional,
			"location": self.location.to_dict(),
		}


@dataclass(frozen=True)
class ParameterDecl:
	name: str
	type: TypeRef | None
	kind: str
	default: str | None
	location: SourceLocation

	def to_dict(self) -> dict[str, object]:
		return {
			"name": self.name,
			"type": None if self.type is None else self.type.to_dict(),
			"kind": self.kind,
			"default": self.default,
			"location": self.location.to_dict(),
		}


@dataclass
class FunctionDecl:
	name: str
	qualified_name: str
	parameters: tuple[ParameterDecl, ...]
	return_type: TypeRef | None
	decorators: tuple[str, ...]
	documentation: str
	is_async: bool
	location: SourceLocation
	node: ast.FunctionDef | ast.AsyncFunctionDef = field(repr=False)

	def to_dict(self) -> dict[str, object]:
		return {
			"kind": "function",
			"name": self.name,
			"qualifiedName": self.qualified_name,
			"parameters": [parameter.to_dict() for parameter in self.parameters],
			"returnType": None if self.return_type is None else self.return_type.to_dict(),
			"decorators": list(self.decorators),
			"documentation": self.documentation,
			"async": self.is_async,
			"location": self.location.to_dict(),
		}


@dataclass
class FieldDecl:
	name: str
	type: TypeRef | None
	initializer: ast.expr | None
	initializer_source: str | None
	is_class_variable: bool
	location: SourceLocation

	def to_dict(self) -> dict[str, object]:
		return {
			"kind": "field",
			"name": self.name,
			"type": None if self.type is None else self.type.to_dict(),
			"initializer": self.initializer_source,
			"classVariable": self.is_class_variable,
			"location": self.location.to_dict(),
		}


@dataclass
class ClassDecl:
	name: str
	qualified_name: str
	bases: tuple[str, ...]
	decorators: tuple[str, ...]
	documentation: str
	fields: tuple[FieldDecl, ...]
	methods: tuple[FunctionDecl, ...]
	enum_values: tuple[tuple[str, ast.expr, SourceLocation], ...]
	location: SourceLocation
	node: ast.ClassDef = field(repr=False)

	@property
	def is_enum(self) -> bool:
		return any(base.rsplit(".", 1)[-1] in {"Enum", "IntEnum", "IntFlag"} for base in self.bases)

	@property
	def is_dataclass(self) -> bool:
		return any(name.rsplit(".", 1)[-1] == "dataclass" for name in self.decorators)

	def to_dict(self) -> dict[str, object]:
		return {
			"kind": "class",
			"name": self.name,
			"qualifiedName": self.qualified_name,
			"bases": list(self.bases),
			"decorators": list(self.decorators),
			"documentation": self.documentation,
			"fields": [entry.to_dict() for entry in self.fields],
			"methods": [entry.to_dict() for entry in self.methods],
			"enumValues": [
				{"name": name, "expression": ast.unparse(value), "location": location.to_dict()}
				for name, value, location in self.enum_values
			],
			"location": self.location.to_dict(),
		}


@dataclass
class ModuleDecl:
	source_path: str
	name: str
	imports: tuple[str, ...]
	functions: tuple[FunctionDecl, ...]
	classes: tuple[ClassDecl, ...]
	variables: tuple[FieldDecl, ...]
	diagnostics: tuple[Diagnostic, ...]
	node: ast.Module | None = field(default=None, repr=False)

	@property
	def succeeded(self) -> bool:
		return not any(item.severity == "error" for item in self.diagnostics)

	def to_dict(self) -> dict[str, object]:
		return {
			"schemaVersion": 1,
			"sourcePath": self.source_path,
			"name": self.name,
			"imports": list(self.imports),
			"functions": [entry.to_dict() for entry in self.functions],
			"classes": [entry.to_dict() for entry in self.classes],
			"variables": [entry.to_dict() for entry in self.variables],
			"diagnostics": [entry.to_dict() for entry in self.diagnostics],
		}


@dataclass(frozen=True)
class ProjectModule:
	input_path: str
	relative_path: str
	output_name: str
	module: ModuleDecl

	def to_dict(self) -> dict[str, object]:
		return {
			"input": self.input_path,
			"relativePath": self.relative_path,
			"output": self.output_name,
			"module": self.module.to_dict(),
		}


@dataclass(frozen=True)
class ProjectDecl:
	root: str
	modules: tuple[ProjectModule, ...]
	diagnostics: tuple[Diagnostic, ...]

	@property
	def succeeded(self) -> bool:
		return not any(item.severity == "error" for item in self.diagnostics)

	def to_dict(self) -> dict[str, object]:
		return {
			"schemaVersion": 1,
			"kind": "veroscript.python-frontend-project",
			"root": self.root,
			"modules": [entry.to_dict() for entry in self.modules],
			"diagnostics": [entry.to_dict() for entry in self.diagnostics],
		}


def _location(node: ast.AST, source_path: str) -> SourceLocation:
	return SourceLocation(
		source_path,
		max(1, int(getattr(node, "lineno", 1))),
		max(1, int(getattr(node, "col_offset", 0)) + 1),
		max(1, int(getattr(node, "end_lineno", getattr(node, "lineno", 1)))),
		max(1, int(getattr(node, "end_col_offset", getattr(node, "col_offset", 0))) + 1),
	)


def qualified_name(node: ast.AST) -> str | None:
	if isinstance(node, ast.Name):
		return node.id
	if isinstance(node, ast.Attribute):
		parent = qualified_name(node.value)
		return None if parent is None else f"{parent}.{node.attr}"
	if isinstance(node, ast.Call):
		return qualified_name(node.func)
	return None


def _type_ref(node: ast.expr | None, source_path: str) -> TypeRef | None:
	if node is None:
		return None
	spelling = ast.unparse(node)
	location = _location(node, source_path)
	if isinstance(node, ast.Constant) and node.value is None:
		return TypeRef(spelling, "None", location=location)
	if isinstance(node, (ast.Name, ast.Attribute)):
		name = qualified_name(node)
		return TypeRef(spelling, name or spelling, location=location)
	if isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr):
		left = _type_ref(node.left, source_path)
		right = _type_ref(node.right, source_path)
		values = tuple(value for value in (left, right) if value is not None)
		non_none = tuple(value for value in values if value.name not in {"None", "NoneType"})
		if len(values) == 2 and len(non_none) == 1:
			value = non_none[0]
			return TypeRef(spelling, value.name, value.arguments, True, location)
		return TypeRef(spelling, "Union", values, False, location)
	if isinstance(node, ast.Subscript):
		name = qualified_name(node.value) or ast.unparse(node.value)
		elements: Sequence[ast.expr]
		if isinstance(node.slice, ast.Tuple):
			elements = node.slice.elts
		else:
			elements = (node.slice,)
		if not elements:
			# Keep an explicit empty subscription distinct from a bare type;
			# consumers must not silently accept int[()] as int, for example.
			return TypeRef(spelling, spelling, location=location)
		arguments = tuple(
			value
			for value in (_type_ref(element, source_path) for element in elements)
			if value is not None
		)
		leaf = name.rsplit(".", 1)[-1]
		if leaf == "Optional" and len(arguments) == 1:
			value = arguments[0]
			return TypeRef(spelling, value.name, value.arguments, True, location)
		return TypeRef(spelling, name, arguments, False, location)
	return TypeRef(spelling, spelling, location=location)


def parse_type_annotation(node: ast.expr | None, source_path: str = "<python-source>") -> TypeRef | None:
	"""Normalize an annotation AST without reparsing source or loading catalogs."""
	return _type_ref(node, source_path)


def _decorators(nodes: Iterable[ast.expr]) -> tuple[str, ...]:
	return tuple(
		ast.unparse(node) if isinstance(node, ast.Call) else qualified_name(node) or ast.unparse(node)
		for node in nodes
	)


def _parameters(
	node: ast.FunctionDef | ast.AsyncFunctionDef,
	source_path: str,
) -> tuple[ParameterDecl, ...]:
	arguments = node.args
	positional = [*arguments.posonlyargs, *arguments.args]
	defaults: list[ast.expr | None] = [None] * (len(positional) - len(arguments.defaults))
	defaults.extend(arguments.defaults)
	values: list[ParameterDecl] = []
	positional_only = len(arguments.posonlyargs)
	for index, (argument, default) in enumerate(zip(positional, defaults)):
		values.append(
			ParameterDecl(
				argument.arg,
				_type_ref(argument.annotation, source_path),
				"positional_only" if index < positional_only else "positional",
				None if default is None else ast.unparse(default),
				_location(argument, source_path),
			)
		)
	if arguments.vararg is not None:
		values.append(
			ParameterDecl(
				arguments.vararg.arg,
				_type_ref(arguments.vararg.annotation, source_path),
				"vararg",
				None,
				_location(arguments.vararg, source_path),
			)
		)
	for argument, default in zip(arguments.kwonlyargs, arguments.kw_defaults):
		values.append(
			ParameterDecl(
				argument.arg,
				_type_ref(argument.annotation, source_path),
				"keyword_only",
				None if default is None else ast.unparse(default),
				_location(argument, source_path),
			)
		)
	if arguments.kwarg is not None:
		values.append(
			ParameterDecl(
				arguments.kwarg.arg,
				_type_ref(arguments.kwarg.annotation, source_path),
				"kwarg",
				None,
				_location(arguments.kwarg, source_path),
			)
		)
	return tuple(values)


def _function(
	node: ast.FunctionDef | ast.AsyncFunctionDef,
	source_path: str,
	scope: str = "",
) -> FunctionDecl:
	return FunctionDecl(
		node.name,
		f"{scope}.{node.name}" if scope else node.name,
		_parameters(node, source_path),
		_type_ref(node.returns, source_path),
		_decorators(node.decorator_list),
		ast.get_docstring(node, clean=True) or "",
		isinstance(node, ast.AsyncFunctionDef),
		_location(node, source_path),
		node,
	)


def _annotated_field(node: ast.AnnAssign, source_path: str) -> FieldDecl | None:
	if not isinstance(node.target, ast.Name):
		return None
	type_ref = _type_ref(node.annotation, source_path)
	is_class_variable = bool(
		type_ref is not None and type_ref.name.rsplit(".", 1)[-1] == "ClassVar"
	)
	if is_class_variable and type_ref is not None and len(type_ref.arguments) == 1:
		type_ref = type_ref.arguments[0]
	return FieldDecl(
		node.target.id,
		type_ref,
		node.value,
		None if node.value is None else ast.unparse(node.value),
		is_class_variable,
		_location(node, source_path),
	)


def _class(node: ast.ClassDef, source_path: str, scope: str = "") -> ClassDecl:
	qualified = f"{scope}.{node.name}" if scope else node.name
	fields: list[FieldDecl] = []
	methods: list[FunctionDecl] = []
	enum_values: list[tuple[str, ast.expr, SourceLocation]] = []
	bases = tuple(qualified_name(base) or ast.unparse(base) for base in node.bases)
	is_enum = any(base.rsplit(".", 1)[-1] in {"Enum", "IntEnum", "IntFlag"} for base in bases)
	for child in node.body:
		if isinstance(child, ast.AnnAssign):
			field_value = _annotated_field(child, source_path)
			if field_value is not None:
				fields.append(field_value)
		elif isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
			methods.append(_function(child, source_path, qualified))
		elif is_enum and isinstance(child, ast.Assign) and len(child.targets) == 1:
			target = child.targets[0]
			if isinstance(target, ast.Name):
				enum_values.append((target.id, child.value, _location(child, source_path)))
	return ClassDecl(
		node.name,
		qualified,
		bases,
		_decorators(node.decorator_list),
		ast.get_docstring(node, clean=True) or "",
		tuple(fields),
		tuple(methods),
		tuple(enum_values),
		_location(node, source_path),
		node,
	)


def _class_diagnostics(
	node: ast.ClassDef,
	source_path: str,
	localizer: Localizer,
) -> list[Diagnostic]:
	diagnostics: list[Diagnostic] = []
	declarations: dict[str, str] = {}
	property_setters: set[str] = set()
	bases = tuple(qualified_name(base) or ast.unparse(base) for base in node.bases)
	is_enum = any(
		base.rsplit(".", 1)[-1] in {"Enum", "IntEnum", "IntFlag"}
		for base in bases
	)
	for index, child in enumerate(node.body):
		declaration_name: str | None = None
		declaration_kind: str | None = None
		is_docstring = (
			index == 0
			and isinstance(child, ast.Expr)
			and isinstance(child.value, ast.Constant)
			and isinstance(child.value.value, str)
		)
		is_enum_value = (
			is_enum
			and isinstance(child, ast.Assign)
			and len(child.targets) == 1
			and isinstance(child.targets[0], ast.Name)
		)
		is_named_field = (
			isinstance(child, ast.AnnAssign)
			and isinstance(child.target, ast.Name)
		)
		if is_named_field:
			assert isinstance(child, ast.AnnAssign)
			assert isinstance(child.target, ast.Name)
			declaration_name = child.target.id
			declaration_kind = "field"
		elif is_enum_value:
			assert isinstance(child, ast.Assign)
			target = child.targets[0]
			assert isinstance(target, ast.Name)
			declaration_name = target.id
			declaration_kind = "enum"
		elif isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
			declaration_name = child.name
			decorators = _decorators(child.decorator_list)
			is_property_getter = "property" in decorators
			is_property_setter = f"{child.name}.setter" in decorators
			if is_property_getter:
				declaration_kind = "property"
			elif (
				is_property_setter
				and declarations.get(child.name) == "property"
				and child.name not in property_setters
			):
				property_setters.add(child.name)
				continue
			else:
				declaration_kind = "method"
		if (
			is_docstring
			or is_enum_value
			or is_named_field
			or isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Pass))
		):
			if declaration_name is not None:
				if declaration_name in declarations:
					diagnostics.append(
						Diagnostic(
							"PYF0003",
							localizer.message(
								"python_tools.duplicate_declaration",
								f"{node.name}.{declaration_name}",
							),
							_location(child, source_path),
						)
					)
				elif declaration_kind is not None:
					declarations[declaration_name] = declaration_kind
			continue
		diagnostics.append(
			Diagnostic(
				"PYF0006",
				localizer.message(
					"python_tools.unsupported_construct",
					f"class member {type(child).__name__}",
				),
				_location(child, source_path),
			)
		)
	return diagnostics


def parse_source(
	source: str,
	source_path: str = "<python-source>",
	*,
	module_name: str | None = None,
	language: str | None = None,
	catalog_directory: str | None = None,
) -> ModuleDecl:
	try:
		return _parse_source(
			source, source_path, module_name=module_name,
			language=language, catalog_directory=catalog_directory,
		)
	except RecursionError:
		localizer = Localizer(language, catalog_directory)
		name = module_name or (Path(source_path).stem if not source_path.startswith("<") else "module")
		diagnostic = Diagnostic(
			"PYF0006",
			localizer.message("python_tools.unsupported_construct", "excessive source nesting"),
			SourceLocation(source_path),
		)
		return ModuleDecl(source_path, name, (), (), (), (), (diagnostic,))


def _parse_source(
	source: str,
	source_path: str,
	*,
	module_name: str | None,
	language: str | None,
	catalog_directory: str | None,
) -> ModuleDecl:
	localizer = Localizer(language, catalog_directory)
	name = module_name or (Path(source_path).stem if not source_path.startswith("<") else "module")
	diagnostics: list[Diagnostic] = []
	if sys.version_info < (3, 11) or sys.version_info >= (3, 16):
		diagnostics.append(
			Diagnostic(
				"PYF0001",
				localizer.message("python_tools.python_version_required"),
				SourceLocation(source_path),
			)
		)
		return ModuleDecl(source_path, name, (), (), (), (), tuple(diagnostics))
	try:
		tree = ast.parse(source, filename=source_path, type_comments=True)
	except SyntaxError as error:
		location = SourceLocation(
			source_path,
			error.lineno or 1,
			error.offset or 1,
			error.end_lineno or error.lineno or 1,
			error.end_offset or error.offset or 1,
		)
		diagnostics.append(
			Diagnostic(
				"PYF0002",
				localizer.message("python_tools.syntax_error", error.msg),
				location,
			)
		)
		return ModuleDecl(source_path, name, (), (), (), (), tuple(diagnostics))

	imports: list[str] = []
	functions: list[FunctionDecl] = []
	classes: list[ClassDecl] = []
	variables: list[FieldDecl] = []
	names: set[str] = set()
	for child in tree.body:
		if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
			for declaration in ast.walk(child):
				if (
					isinstance(declaration, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
					and bool(getattr(declaration, "type_params", None))
				):
					diagnostics.append(
						Diagnostic(
							"PYF0006",
							localizer.message(
								"python_tools.unsupported_construct",
								f"type parameters on {declaration.name}",
							),
							_location(declaration, source_path),
						)
					)
		declaration_name: str | None = None
		if isinstance(child, ast.Import):
			imports.extend(alias.name for alias in child.names)
			continue
		if isinstance(child, ast.ImportFrom):
			imports.append("." * child.level + (child.module or ""))
			continue
		if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
			declaration_name = child.name
			functions.append(_function(child, source_path))
		elif isinstance(child, ast.ClassDef):
			declaration_name = child.name
			classes.append(_class(child, source_path))
			diagnostics.extend(_class_diagnostics(child, source_path, localizer))
		elif isinstance(child, ast.AnnAssign):
			field_value = _annotated_field(child, source_path)
			if field_value is None:
				diagnostics.append(
					Diagnostic(
						"PYF0005",
						localizer.message("python_tools.unsupported_top_level", type(child.target).__name__),
						_location(child, source_path),
					)
				)
				continue
			declaration_name = field_value.name
			variables.append(field_value)
		elif isinstance(child, ast.Expr) and isinstance(child.value, ast.Constant) and isinstance(child.value.value, str):
			continue
		elif (
			isinstance(child, ast.If)
			and isinstance(child.test, ast.Name)
			and child.test.id == "TYPE_CHECKING"
			and not child.orelse
		):
			continue
		else:
			diagnostics.append(
				Diagnostic(
					"PYF0005",
					localizer.message("python_tools.unsupported_top_level", type(child).__name__),
					_location(child, source_path),
				)
			)
			continue
		if declaration_name in names:
			diagnostics.append(
				Diagnostic(
					"PYF0003",
					localizer.message("python_tools.duplicate_declaration", declaration_name),
					_location(child, source_path),
				)
			)
		else:
			names.add(declaration_name)
	return ModuleDecl(
		source_path,
		name,
		tuple(imports),
		tuple(functions),
		tuple(classes),
		tuple(variables),
		tuple(diagnostics),
		tree,
	)


def parse_file(
	path: str | Path,
	*,
	module_name: str | None = None,
	language: str | None = None,
	catalog_directory: str | None = None,
) -> ModuleDecl:
	source_path = str(Path(path))
	localizer = Localizer(language, catalog_directory)
	try:
		source = Path(path).read_text(encoding="utf-8-sig")
	except (OSError, UnicodeError) as error:
		return ModuleDecl(
			source_path,
			module_name or Path(path).stem,
			(),
			(),
			(),
			(),
			(
				Diagnostic(
					"PYF0004",
					localizer.message("python_tools.source_read_failed", error),
					SourceLocation(source_path),
				),
			),
		)
	return parse_source(
		source,
		source_path,
		module_name=module_name,
		language=language,
		catalog_directory=catalog_directory,
	)


def _module_name(relative_path: Path) -> str:
	parts = list(relative_path.with_suffix("").parts)
	if parts and parts[-1] == "__init__":
		parts.pop()
	return ".".join(parts) if parts else relative_path.parent.name or "module"


def parse_files(
	paths: Sequence[str | Path],
	*,
	project_root: str | Path | None = None,
	language: str | None = None,
	catalog_directory: str | None = None,
) -> ProjectDecl:
	localizer = Localizer(language, catalog_directory)
	if not paths:
		diagnostic = Diagnostic(
			"PYF0007",
			localizer.message("python_tools.unsupported_construct", "empty project"),
			SourceLocation("<python-project>"),
		)
		return ProjectDecl("", (), (diagnostic,))

	resolved = [Path(path).expanduser().resolve() for path in paths]
	if project_root is None:
		try:
			common = Path(os.path.commonpath([str(path.parent) for path in resolved]))
		except ValueError:
			diagnostic = Diagnostic(
				"PYF0007",
				localizer.message(
					"python_tools.unsupported_construct",
					"source files do not share a project root",
				),
				SourceLocation("<python-project>"),
			)
			return ProjectDecl("", (), (diagnostic,))
	else:
		common = Path(project_root).expanduser().resolve()

	diagnostics: list[Diagnostic] = []
	modules: list[ProjectModule] = []
	seen_paths: set[str] = set()
	seen_modules: set[str] = set()
	seen_outputs: set[str] = set()
	for original, source_path in zip(paths, resolved):
		path_key = os.path.normcase(str(source_path))
		if path_key in seen_paths:
			diagnostics.append(
				Diagnostic(
					"PYF0007",
					localizer.message(
						"python_tools.duplicate_declaration", str(original)
					),
					SourceLocation(str(original)),
				)
			)
			continue
		seen_paths.add(path_key)
		try:
			relative = source_path.relative_to(common)
		except ValueError:
			diagnostics.append(
				Diagnostic(
					"PYF0007",
					localizer.message(
						"python_tools.unsupported_construct",
						f"source outside project root: {original}",
					),
					SourceLocation(str(original)),
				)
			)
			continue
		name = _module_name(relative)
		if name in seen_modules:
			diagnostics.append(
				Diagnostic(
					"PYF0007",
					localizer.message("python_tools.duplicate_declaration", name),
					SourceLocation(str(original)),
				)
			)
			continue
		seen_modules.add(name)
		output_name = relative.with_suffix(".python-ast.json").as_posix()
		output_key = os.path.normcase(output_name)
		if output_key in seen_outputs:
			diagnostics.append(
				Diagnostic(
					"PYF0007",
					localizer.message(
						"python_tools.duplicate_declaration", output_name
					),
					SourceLocation(str(original)),
				)
			)
			continue
		seen_outputs.add(output_key)
		module = parse_file(
			source_path,
			module_name=name,
			language=language,
			catalog_directory=catalog_directory,
		)
		diagnostics.extend(module.diagnostics)
		modules.append(
			ProjectModule(
				str(source_path),
				relative.as_posix(),
				output_name,
				module,
			)
		)
	return ProjectDecl(str(common), tuple(modules), tuple(diagnostics))


def write_text_atomic(path: str | Path, content: str) -> None:
	destination = Path(path)
	destination.parent.mkdir(parents=True, exist_ok=True)
	temporary: Path | None = None
	try:
		with tempfile.NamedTemporaryFile(
			"w",
			encoding="utf-8",
			newline="\n",
			dir=destination.parent,
			delete=False,
		) as output:
			temporary = Path(output.name)
			output.write(content)
			output.flush()
			os.fsync(output.fileno())
		if destination.is_symlink():
			raise OSError(f"existing generated output is a symbolic link: {destination}")
		temporary.replace(destination)
	except BaseException:
		if temporary is not None:
			temporary.unlink(missing_ok=True)
		raise


def write_text_files_atomic(
	files: Mapping[str | Path, str] | Sequence[tuple[str | Path, str]],
) -> None:
	"""Replace multiple generated text files as one best-effort transaction."""
	source_entries = files.items() if isinstance(files, Mapping) else files
	entries = [(Path(path), content) for path, content in source_entries]
	normalized: set[str] = set()
	for path, _ in entries:
		key = os.path.normcase(str(path.expanduser().resolve()))
		if key in normalized:
			raise ValueError("generated output paths must be distinct")
		normalized.add(key)

	nonce = uuid.uuid4().hex
	staged: list[tuple[Path, Path, Path, bool]] = []
	try:
		for index, (target, content) in enumerate(entries):
			target.parent.mkdir(parents=True, exist_ok=True)
			temporary = target.with_name(f"{target.name}.tmp.{nonce}.{index}")
			backup = target.with_name(f"{target.name}.bak.{nonce}.{index}")
			staged.append(
				(target, temporary, backup, target.exists() or target.is_symlink())
			)
			with temporary.open("w", encoding="utf-8", newline="\n") as output:
				output.write(content)
				output.flush()
				os.fsync(output.fileno())
		for target, _, backup, existed in staged:
			if existed:
				if target.is_symlink() or not target.is_file():
					raise OSError(f"existing generated output is not a file: {target}")
				target.replace(backup)
		committed: list[Path] = []
		try:
			for target, temporary, _, _ in staged:
				temporary.replace(target)
				committed.append(target)
		except BaseException:
			for target in committed:
				target.unlink(missing_ok=True)
			raise
	except BaseException:
		for target, temporary, backup, existed in reversed(staged):
			temporary.unlink(missing_ok=True)
			if existed and backup.exists():
				target.unlink(missing_ok=True)
				backup.replace(target)
		raise
	else:
		for _, _, backup, _ in staged:
			backup.unlink(missing_ok=True)


__all__ = [
	"__version__",
	"ClassDecl",
	"Diagnostic",
	"FieldDecl",
	"FunctionDecl",
	"Localizer",
	"ModuleDecl",
	"ParameterDecl",
	"ProjectDecl",
	"ProjectModule",
	"SourceLocation",
	"TypeRef",
	"find_veroscript_executable",
	"evaluate_integer_expression",
	"is_veroscript_identifier",
	"parse_file",
	"parse_files",
	"parse_source",
	"parse_type_annotation",
	"qualified_name",
	"write_text_atomic",
	"write_text_files_atomic",
]
