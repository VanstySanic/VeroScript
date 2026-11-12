# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import ast
from dataclasses import dataclass
from enum import IntEnum, IntFlag
import inspect
import json
import math
from pathlib import Path
import subprocess
import sys
from types import ModuleType
from typing import Any, Callable, Iterable, Mapping, Sequence, cast

if sys.version_info >= (3, 14):
	from annotationlib import Format

from veroscript_python_frontend import (
	__version__ as __version__,
	ClassDecl,
	Diagnostic,
	FunctionDecl,
	Localizer,
	ModuleDecl,
	ParameterDecl,
	SourceLocation,
	TypeRef,
	evaluate_integer_expression,
	find_veroscript_executable,
	is_veroscript_identifier,
	parse_file,
	parse_files,
	parse_source,
)


@dataclass(frozen=True)
class MappedType:
	native: str
	script: str
	host: str = ""
	element: str = ""
	key: str = ""
	value: str = ""


@dataclass(frozen=True)
class BindGenResult:
	catalog: dict[str, object] | None
	catalog_json: str
	api_reference: str
	diagnostics: tuple[Diagnostic, ...]

	@property
	def succeeded(self) -> bool:
		return self.catalog is not None and not any(
			item.severity == "error" for item in self.diagnostics
		)


@dataclass(frozen=True)
class BindGenProjectFile:
	input_path: str
	output_name: str
	api_reference: str


@dataclass(frozen=True)
class BindGenProjectResult:
	catalog: dict[str, object] | None
	catalog_json: str
	api_references: tuple[BindGenProjectFile, ...]
	manifest_json: str
	diagnostics: tuple[Diagnostic, ...]

	@property
	def succeeded(self) -> bool:
		return self.catalog is not None and not any(
			item.severity == "error" for item in self.diagnostics
		)


_PRIMITIVES: dict[str, tuple[str, str]] = {
	"None": ("void", "void"),
	"NoneType": ("void", "void"),
	"bool": ("bool", "bool"),
	"int": ("int", "int"),
	"float": ("double", "double"),
	"str": ("string", "string"),
	"Int8": ("int8", "int8"),
	"UInt8": ("uint8", "uint8"),
	"Int16": ("int16", "int16"),
	"UInt16": ("uint16", "uint16"),
	"Int32": ("int", "int"),
	"UInt32": ("unsigned", "unsigned int"),
	"Int64": ("int64", "int64"),
	"UInt64": ("uint64", "uint64"),
	"Float32": ("float", "float"),
	"Float64": ("double", "double"),
}


def _leaf(name: str) -> str:
	return name.rsplit(".", 1)[-1]


def _qualified(module: ModuleDecl, name: str) -> str:
	namespace = module.name.replace(".", "::")
	return f"{namespace}::{name}" if namespace else name


def _identifier_diagnostics(
	module: ModuleDecl,
	localizer: Localizer,
) -> list[Diagnostic]:
	diagnostics: list[Diagnostic] = []

	def check(name: str, subject: str, location: SourceLocation) -> None:
		if is_veroscript_identifier(name):
			return
		diagnostics.append(
			Diagnostic(
				"PYB1007",
				localizer.message(
					"python_tools.unsupported_construct",
					f"{subject} {name!r} is not a valid VeroScript identifier",
				),
				location,
			)
		)

	module_location = SourceLocation(module.source_path)
	for component in module.name.split("."):
		check(component, "module name component", module_location)
	for entry in module.classes:
		if entry.name.startswith("_"):
			continue
		check(entry.name, "class", entry.location)
		if entry.is_enum:
			for name, _, location in entry.enum_values:
				check(name, "enum member", location)
			continue
		for field in entry.fields:
			check(field.name, "field", field.location)
		for method in entry.methods:
			if method.name != "__init__" and method.name.startswith("_"):
				continue
			if method.name != "__init__":
				check(method.name, "method", method.location)
			for parameter in method.parameters:
				check(parameter.name, "parameter", parameter.location)
	for function in module.functions:
		if function.name.startswith("_"):
			continue
		check(function.name, "function", function.location)
		for parameter in function.parameters:
			check(parameter.name, "parameter", parameter.location)
	return diagnostics


def _type_sets(module: ModuleDecl) -> tuple[dict[str, str], dict[str, str]]:
	records: dict[str, str] = {}
	enums: dict[str, str] = {}
	for entry in module.classes:
		if entry.name.startswith("_"):
			continue
		destination = enums if entry.is_enum else records
		destination[entry.name] = _qualified(module, entry.name)
		destination[entry.qualified_name] = _qualified(module, entry.name)
	return records, enums


def _declared_type(name: str, records: dict[str, str], enums: dict[str, str]) -> MappedType | None:
	# Explicit qualified declarations take precedence over leaf-name fallbacks
	# and scalar aliases such as Int8/Float32, which Python classes can shadow.
	leaf = _leaf(name)
	candidates = (name,) if leaf == name else (name, leaf)
	for candidate in candidates:
		record_name = records.get(candidate)
		if record_name is not None:
			return MappedType("object", record_name, host=record_name)
		enum_name = enums.get(candidate)
		if enum_name is not None:
			return MappedType("enum", enum_name, host=enum_name)
	return None


def _script_type(type_ref: TypeRef, records: dict[str, str], enums: dict[str, str]) -> str | None:
	if type_ref.optional:
		return None
	declared = _declared_type(type_ref.name, records, enums)
	if declared is not None:
		return None if type_ref.arguments else declared.script
	leaf = _leaf(type_ref.name)
	primitive = _PRIMITIVES.get(leaf)
	if primitive is not None:
		return None if type_ref.arguments or primitive[0] == "void" else primitive[1]
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
		inner = _script_type(type_ref.arguments[0], records, enums)
		return None if inner is None else f"{unary[leaf]}<{inner}>"
	if leaf in {"dict", "Dict", "Mapping", "MutableMapping"} and len(type_ref.arguments) == 2:
		key = _script_type(type_ref.arguments[0], records, enums)
		value = _script_type(type_ref.arguments[1], records, enums)
		return None if key is None or value is None else f"map<{key},{value}>"
	return None


def _map_type(
	type_ref: TypeRef | None,
	module: ModuleDecl,
	records: dict[str, str],
	enums: dict[str, str],
	diagnostics: list[Diagnostic],
	localizer: Localizer,
	subject: str,
) -> MappedType | None:
	if type_ref is None:
		diagnostics.append(
			Diagnostic(
				"PYB1001",
				localizer.message("python_tools.missing_annotation", subject),
				SourceLocation(module.source_path),
			)
		)
		return None
	if type_ref.optional:
		diagnostics.append(
			Diagnostic(
				"PYB1002",
				localizer.message("python_tools.unsupported_type", type_ref.spelling),
				type_ref.location,
			)
		)
		return None
	declared = _declared_type(type_ref.name, records, enums)
	leaf = _leaf(type_ref.name)
	primitive = _PRIMITIVES.get(leaf)
	if type_ref.arguments and (declared is not None or primitive is not None):
		diagnostics.append(
			Diagnostic(
				"PYB1002",
				localizer.message("python_tools.unsupported_type", type_ref.spelling),
				type_ref.location,
			)
		)
		return None
	if declared is not None:
		return declared
	if primitive is not None:
		return MappedType(primitive[0], primitive[1])
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
		element = _script_type(type_ref.arguments[0], records, enums)
		if element is not None:
			return MappedType(unary[leaf], f"{unary[leaf]}<{element}>", element=element)
	if leaf in {"dict", "Dict", "Mapping", "MutableMapping"} and len(type_ref.arguments) == 2:
		key = _script_type(type_ref.arguments[0], records, enums)
		value = _script_type(type_ref.arguments[1], records, enums)
		if key is not None and value is not None:
			return MappedType("map", f"map<{key},{value}>", key=key, value=value)
	diagnostics.append(
		Diagnostic(
			"PYB1002",
			localizer.message("python_tools.unsupported_type", type_ref.spelling),
			type_ref.location,
		)
	)
	return None


def _signature_fields(return_type: MappedType, parameters: Sequence[MappedType]) -> dict[str, object]:
	return {
		"returnType": return_type.native,
		"paramTypes": [entry.native for entry in parameters],
		"returnHostTypeName": return_type.host,
		"paramHostTypeNames": [entry.host for entry in parameters],
		"returnElementTypeName": return_type.element,
		"paramElementTypeNames": [entry.element for entry in parameters],
		"returnKeyTypeName": return_type.key,
		"returnValueTypeName": return_type.value,
		"paramKeyTypeNames": [entry.key for entry in parameters],
		"paramValueTypeNames": [entry.value for entry in parameters],
		"paramFlags": [0 for _ in parameters],
	}


def _has_receiver(function: FunctionDecl, name: str) -> bool:
	return (
		bool(function.parameters)
		and function.parameters[0].name == name
		and function.parameters[0].kind in {"positional", "positional_only"}
		and function.parameters[0].default is None
	)


def _function_entry(
	function: FunctionDecl,
	module: ModuleDecl,
	records: dict[str, str],
	enums: dict[str, str],
	diagnostics: list[Diagnostic],
	localizer: Localizer,
	*,
	skip_receiver: bool = False,
) -> tuple[dict[str, object] | None, list[MappedType], MappedType | None]:
	parameters = list(function.parameters[1:] if skip_receiver else function.parameters)
	mapped_parameters: list[MappedType] = []
	for parameter in parameters:
		if parameter.kind not in {"positional", "positional_only"}:
			diagnostics.append(
				Diagnostic(
					"PYB1003",
					localizer.message("python_tools.unsupported_parameter", parameter.name),
					parameter.location,
				)
			)
			continue
		if parameter.default is not None:
			diagnostics.append(
				Diagnostic(
					"PYB1003",
					localizer.message("python_tools.unsupported_parameter", f"default argument {parameter.name}"),
					parameter.location,
				)
			)
			continue
		mapped = _map_type(
			parameter.type,
			module,
			records,
			enums,
			diagnostics,
			localizer,
			f"parameter {parameter.name}",
		)
		if mapped is not None and mapped.native == "void":
			diagnostics.append(
				Diagnostic(
					"PYB1002",
					localizer.message(
						"python_tools.unsupported_type",
						f"None for parameter {parameter.name}",
					),
					parameter.location,
				)
			)
		elif mapped is not None:
			mapped_parameters.append(mapped)
	if len(mapped_parameters) != len(parameters):
		return None, mapped_parameters, None
	mapped_return: MappedType | None
	if function.name == "__init__":
		declared_return = _map_type(
			function.return_type,
			module,
			records,
			enums,
			diagnostics,
			localizer,
			f"return value of {function.qualified_name}",
		)
		if declared_return is None:
			return None, mapped_parameters, None
		if declared_return.native != "void":
			diagnostics.append(
				Diagnostic(
					"PYB1002",
					localizer.message(
						"python_tools.unsupported_type",
						f"constructor return type {declared_return.script}",
					),
					function.location,
				)
			)
			return None, mapped_parameters, None
		mapped_return = MappedType("object", "object")
	else:
		mapped_return = _map_type(
			function.return_type,
			module,
			records,
			enums,
			diagnostics,
			localizer,
			f"return value of {function.qualified_name}",
		)
		if mapped_return is None:
			return None, mapped_parameters, None
	entry: dict[str, object] = {
		"documentation": function.documentation,
		"annotations": [],
		"paramAnnotations": [],
		"paramNames": [parameter.name for parameter in parameters],
		"paramDocumentation": [],
		"returnDocumentation": "",
		**_signature_fields(mapped_return, mapped_parameters),
	}
	return entry, mapped_parameters, mapped_return


def _member_entry(
	kind: str,
	name: str,
	function: FunctionDecl,
	signature: dict[str, object],
) -> dict[str, object]:
	return {
		"kind": kind,
		"name": name,
		**signature,
		"enumValue": 0,
		"enumValueInt64": "0",
		"operator": False,
		"constMethod": False,
		"mutableThis": kind in {"constructor", "method", "propertySetter"},
		"nativeName": "",
	}


def _field_entry(name: str, documentation: str, mapped: MappedType) -> dict[str, object]:
	return {
		"kind": "field",
		"name": name,
		"documentation": documentation,
		"annotations": [],
		"paramAnnotations": [],
		"paramNames": [],
		"paramDocumentation": [],
		"returnDocumentation": "",
		**_signature_fields(mapped, ()),
		"enumValue": 0,
		"enumValueInt64": "0",
		"operator": False,
		"constMethod": False,
		"mutableThis": False,
		"nativeName": "",
	}


def _field_setter_entry(
	name: str,
	documentation: str,
	mapped: MappedType,
) -> dict[str, object]:
	return {
		"kind": "propertySetter",
		"name": name,
		"documentation": documentation,
		"annotations": [],
		"paramAnnotations": [],
		"paramNames": ["value"],
		"paramDocumentation": [""],
		"returnDocumentation": "",
		**_signature_fields(MappedType("void", "void"), (mapped,)),
		"enumValue": 0,
		"enumValueInt64": "0",
		"operator": False,
		"constMethod": False,
		"mutableThis": True,
		"nativeName": "",
	}


def _enum_type(
	entry: ClassDecl,
	module: ModuleDecl,
	diagnostics: list[Diagnostic],
	localizer: Localizer,
) -> dict[str, object] | None:
	if not any(_leaf(base) in {"IntEnum", "IntFlag"} for base in entry.bases):
		diagnostics.append(
			Diagnostic(
				"PYB1007",
				localizer.message(
					"python_tools.unsupported_construct",
					f"enum {entry.name} must derive from IntEnum or IntFlag",
				),
				entry.location,
			)
		)
		return None
	members: list[dict[str, object]] = []
	values: dict[str, int] = {}
	type_name = _qualified(module, entry.name)
	for name, expression, location in entry.enum_values:
		value = evaluate_integer_expression(expression, values)
		if value is None or value < -(1 << 63) or value >= (1 << 63):
			diagnostics.append(
				Diagnostic(
					"PYB1006",
					localizer.message("python_tools.enum_value_required", name),
					location,
				)
			)
			continue
		values[name] = value
		members.append(
			{
				"kind": "enumMember",
				"name": name,
				"documentation": "",
				"annotations": [],
				"paramAnnotations": [],
				"paramNames": [],
				"paramDocumentation": [],
				"returnDocumentation": "",
				**_signature_fields(MappedType("void", "void", host=type_name), ()),
				"enumValue": value,
				"enumValueInt64": str(value),
				"returnHostTypeName": type_name,
				"operator": False,
				"constMethod": False,
				"mutableThis": False,
				"nativeName": "",
			}
		)
	if len(members) != len(entry.enum_values):
		return None
	return {
		"name": type_name,
		"documentation": entry.documentation,
		"annotations": [],
		"kind": "enum",
		"inheritable": False,
		"scopedEnum": True,
		"enumUnderlyingType": "int64",
		"genericTemplateName": "",
		"genericTypeArguments": [],
		"hasClone": False,
		"hasEquals": False,
		"hasHash": False,
		"hasSubclass": False,
		"members": members,
	}


def _record_type(
	entry: ClassDecl,
	module: ModuleDecl,
	records: dict[str, str],
	enums: dict[str, str],
	diagnostics: list[Diagnostic],
	localizer: Localizer,
) -> dict[str, object] | None:
	unsupported_decorators = [
		decorator for decorator in entry.decorators if _leaf(decorator) != "dataclass"
	]
	if unsupported_decorators:
		diagnostics.append(
			Diagnostic(
				"PYB1004",
				localizer.message(
					"python_tools.unsupported_decorator", unsupported_decorators[0]
				),
				entry.location,
			)
		)
		return None
	unsupported_bases = [base for base in entry.bases if _leaf(base) != "object"]
	if unsupported_bases:
		diagnostics.append(
			Diagnostic(
				"PYB1005",
				localizer.message("python_tools.unsupported_inheritance", ", ".join(unsupported_bases)),
				entry.location,
			)
		)
		return None
	members: list[dict[str, object]] = []
	property_types: dict[str, MappedType] = {}
	property_setters: set[str] = set()
	for field in entry.fields:
		if field.is_class_variable:
			diagnostics.append(
				Diagnostic(
					"PYB1007",
					localizer.message("python_tools.unsupported_construct", f"class variable {field.name}"),
					field.location,
				)
			)
			continue
		mapped = _map_type(
			field.type,
			module,
			records,
			enums,
			diagnostics,
			localizer,
			f"field {entry.name}.{field.name}",
		)
		if mapped is not None and mapped.native == "void":
			diagnostics.append(
				Diagnostic(
					"PYB1002",
					localizer.message(
						"python_tools.unsupported_type",
						f"None for field {entry.name}.{field.name}",
					),
					field.location,
				)
			)
		elif mapped is not None:
			members.append(_field_entry(field.name, "", mapped))
			members.append(_field_setter_entry(field.name, "", mapped))
	for method in entry.methods:
		if method.is_async:
			diagnostics.append(
				Diagnostic(
					"PYB1007",
					localizer.message("python_tools.unsupported_construct", f"async function {method.name}"),
					method.location,
				)
			)
			continue
		decorators = {_leaf(value) for value in method.decorators}
		allowed_decorators = {"staticmethod", "classmethod", "property"}
		unknown_decorators = [
			value
			for value in method.decorators
			if _leaf(value) not in allowed_decorators and not value.endswith(".setter")
		]
		if unknown_decorators:
			diagnostics.append(
				Diagnostic(
					"PYB1004",
					localizer.message(
						"python_tools.unsupported_decorator", unknown_decorators[0]
					),
					method.location,
				)
			)
			continue
		if method.name == "__init__":
			if method.decorators or not _has_receiver(method, "self"):
				diagnostics.append(
					Diagnostic(
						"PYB1003",
						localizer.message("python_tools.unsupported_parameter", "constructor receiver"),
						method.location,
					)
				)
				continue
			signature, _, _ = _function_entry(
				method, module, records, enums, diagnostics, localizer, skip_receiver=True
			)
			if signature is not None:
				members.append(_member_entry("constructor", _qualified(module, entry.name), method, signature))
			continue
		if method.name.startswith("_"):
			continue
		if "property" in decorators:
			receiver_is_valid = (
				len(method.parameters) == 1
				and _has_receiver(method, "self")
			)
			if method.decorators != ("property",) or not receiver_is_valid:
				diagnostics.append(
					Diagnostic(
						"PYB1003",
						localizer.message(
							"python_tools.unsupported_parameter",
							f"property getter {method.name} must have signature (self) -> T",
						),
						method.location,
					)
				)
				continue
			signature, _, mapped_return = _function_entry(
				method, module, records, enums, diagnostics, localizer, skip_receiver=True
			)
			if (
				signature is not None
				and mapped_return is not None
				and mapped_return.native == "void"
			):
				diagnostics.append(
					Diagnostic(
						"PYB1003",
						localizer.message(
							"python_tools.unsupported_parameter",
							f"property getter {method.name} must return a value",
						),
						method.location,
					)
				)
				continue
			if signature is not None and mapped_return is not None:
				members.append(_member_entry("propertyGetter", method.name, method, signature))
				property_types[method.name] = mapped_return
			continue
		setter_names = [
			value.removesuffix(".setter").rsplit(".", 1)[-1]
			for value in method.decorators
			if value.endswith(".setter")
		]
		if setter_names:
			receiver_is_valid = (
				len(method.parameters) == 2
				and _has_receiver(method, "self")
				and method.parameters[1].kind in {"positional", "positional_only"}
				and method.parameters[1].default is None
			)
			property_name = setter_names[0]
			if (
				len(method.decorators) != 1
				or len(setter_names) != 1
				or property_name != method.name
				or method.decorators[0] != f"{method.name}.setter"
				or not receiver_is_valid
			):
				diagnostics.append(
					Diagnostic(
						"PYB1003",
						localizer.message(
							"python_tools.unsupported_parameter",
							f"property setter {method.name} must have signature (self, value: T) -> None",
						),
						method.location,
					)
				)
				continue
			if property_name in property_setters:
				diagnostics.append(
					Diagnostic(
						"PYB1007",
						localizer.message(
							"python_tools.unsupported_construct",
							f"duplicate property setter {property_name}",
						),
						method.location,
					)
				)
				continue
			property_setters.add(property_name)
			getter_type = property_types.get(property_name)
			if getter_type is None:
				diagnostics.append(
					Diagnostic(
						"PYB1007",
						localizer.message(
							"python_tools.unsupported_construct",
							f"property setter {property_name} has no preceding valid getter",
						),
						method.location,
					)
				)
				continue
			signature, mapped_parameters, mapped_return = _function_entry(
				method, module, records, enums, diagnostics, localizer, skip_receiver=True
			)
			if signature is None or mapped_return is None:
				continue
			if mapped_return.native != "void":
				diagnostics.append(
					Diagnostic(
						"PYB1003",
						localizer.message(
							"python_tools.unsupported_parameter",
							f"property setter {property_name} must return None",
						),
						method.location,
					)
				)
				continue
			if len(mapped_parameters) != 1 or mapped_parameters[0] != getter_type:
				diagnostics.append(
					Diagnostic(
						"PYB1003",
						localizer.message(
							"python_tools.unsupported_parameter",
							f"property setter {property_name} value type must match its getter",
						),
						method.location,
					)
				)
				continue
			members.append(_member_entry("propertySetter", property_name, method, signature))
			continue
		if method.decorators not in {(), ("staticmethod",), ("classmethod",)}:
			diagnostics.append(
				Diagnostic(
					"PYB1004",
					localizer.message(
						"python_tools.unsupported_decorator",
						", ".join(method.decorators),
					),
					method.location,
				)
			)
			continue
		static = "staticmethod" in decorators or "classmethod" in decorators
		skip_receiver = not static or "classmethod" in decorators
		expected = "cls" if "classmethod" in decorators else "self"
		if skip_receiver and not _has_receiver(method, expected):
			diagnostics.append(
				Diagnostic(
					"PYB1003",
					localizer.message("python_tools.unsupported_parameter", f"receiver of {method.name}"),
					method.location,
				)
			)
			continue
		signature, _, _ = _function_entry(
			method, module, records, enums, diagnostics, localizer, skip_receiver=skip_receiver
		)
		if signature is not None:
			members.append(
				_member_entry("staticMethod" if static else "method", method.name, method, signature)
			)
	return {
		"name": _qualified(module, entry.name),
		"documentation": entry.documentation,
		"annotations": [],
		# Python instances, including dataclass instances, have reference semantics.
		"kind": "class",
		"inheritable": False,
		"scopedEnum": False,
		"enumUnderlyingType": "",
		"genericTemplateName": "",
		"genericTypeArguments": [],
		"hasClone": False,
		"hasEquals": False,
		"hasHash": False,
		"hasSubclass": False,
		"members": members,
	}


def _api_reference(catalog: dict[str, object]) -> str:
	lines = [
		"// SPDX-FileCopyrightText: VanstySanic",
		"// SPDX-License-Identifier: MIT",
		"",
		"// Generated Python binding API reference.",
		"// Load the JSON catalog for compilation; do not compile this file as an implementation.",
		"",
	]
	type_entries = cast(list[dict[str, object]], catalog["types"])
	for type_entry in type_entries:
		name = str(type_entry["name"])
		members = cast(list[dict[str, object]], type_entry["members"])
		if type_entry["kind"] == "enum":
			values = ", ".join(
				f"{member['name']} = {member['enumValueInt64']}" for member in members
			)
			lines.append(f"enum class {name} : int64 {{ {values} }};")
			continue
		lines.append(f"{type_entry['kind']} {name}")
		lines.append("{")
		lines.append("public:")
		for member in members:
			kind = member["kind"]
			parameter_types = cast(list[str], member["paramTypes"])
			parameter_names = cast(list[str], member["paramNames"])
			params = ", ".join(
				f"{type_name} {param_name}"
				for type_name, param_name in zip(parameter_types, parameter_names)
			)
			if kind == "constructor":
				lines.append(f"\t{name.rsplit('::', 1)[-1]}({params});")
			elif kind == "field":
				lines.append(f"\t{member['returnType']} {member['name']};")
			elif kind not in {"enumMember", "propertySetter"}:
				prefix = "static " if kind == "staticMethod" else ""
				lines.append(f"\t{prefix}{member['returnType']} {member['name']}({params});")
		lines.append("};")
	functions = cast(list[dict[str, object]], catalog["functions"])
	for function in functions:
		parameter_types = cast(list[str], function["paramTypes"])
		parameter_names = cast(list[str], function["paramNames"])
		params = ", ".join(
			f"{type_name} {param_name}"
			for type_name, param_name in zip(parameter_types, parameter_names)
		)
		lines.append(f"{function['returnType']} {function['name']}({params});")
	return "\n".join(lines) + "\n"


def generate_bindings(
	module: ModuleDecl,
	*,
	language: str | None = None,
	catalog_directory: str | None = None,
	known_records: dict[str, str] | None = None,
	known_enums: dict[str, str] | None = None,
) -> BindGenResult:
	diagnostics = list(module.diagnostics)
	localizer = Localizer(language, catalog_directory)
	invalid_identifiers = _identifier_diagnostics(module, localizer)
	diagnostics.extend(invalid_identifiers)
	if invalid_identifiers:
		return BindGenResult(None, "", "", tuple(diagnostics))
	records = dict(known_records or {})
	enums = dict(known_enums or {})
	local_records, local_enums = _type_sets(module)
	for name in local_records:
		enums.pop(name, None)
	for name in local_enums:
		records.pop(name, None)
	records.update(local_records)
	enums.update(local_enums)
	types: list[dict[str, object]] = []
	for entry in module.classes:
		if entry.name.startswith("_"):
			continue
		generated = (
			_enum_type(entry, module, diagnostics, localizer)
			if entry.is_enum
			else _record_type(entry, module, records, enums, diagnostics, localizer)
		)
		if generated is not None:
			types.append(generated)
	functions: list[dict[str, object]] = []
	for function in module.functions:
		if function.name.startswith("_"):
			continue
		if function.is_async:
			diagnostics.append(
				Diagnostic(
					"PYB1007",
					localizer.message("python_tools.unsupported_construct", f"async function {function.name}"),
					function.location,
				)
			)
			continue
		if function.decorators:
			diagnostics.append(
				Diagnostic(
					"PYB1004",
					localizer.message(
						"python_tools.unsupported_decorator", function.decorators[0]
					),
					function.location,
				)
			)
			continue
		signature, _, _ = _function_entry(
			function, module, records, enums, diagnostics, localizer
		)
		if signature is not None:
			functions.append(
				{
					"name": _qualified(module, function.name),
					**signature,
					"typeParams": [],
					"scriptReturnTypeName": "",
					"genericHostObjectCast": False,
				}
			)
	if module.variables:
		for variable in module.variables:
			diagnostics.append(
				Diagnostic(
					"PYB1007",
					localizer.message("python_tools.unsupported_construct", f"module variable {variable.name}"),
					variable.location,
				)
			)
	if any(item.severity == "error" for item in diagnostics):
		return BindGenResult(None, "", "", tuple(diagnostics))
	catalog: dict[str, object] = {
		"version": 1,
		"functions": functions,
		"genericTypeTemplates": [],
		"types": types,
		"typeAliases": [],
		"annotations": [],
	}
	rendered = json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"
	return BindGenResult(catalog, rendered, _api_reference(catalog), tuple(diagnostics))


def generate_bindings_from_source(
	source: str,
	source_path: str = "<python-source>",
	*,
	module_name: str | None = None,
	language: str | None = None,
	catalog_directory: str | None = None,
) -> BindGenResult:
	return generate_bindings(
		parse_source(
			source,
			source_path,
			module_name=module_name,
			language=language,
			catalog_directory=catalog_directory,
		),
		language=language,
		catalog_directory=catalog_directory,
	)


def generate_bindings_from_file(
	path: str | Path,
	*,
	module_name: str | None = None,
	language: str | None = None,
	catalog_directory: str | None = None,
) -> BindGenResult:
	return generate_bindings(
		parse_file(
			path,
			module_name=module_name,
			language=language,
			catalog_directory=catalog_directory,
		),
		language=language,
		catalog_directory=catalog_directory,
	)


def _ambiguous_type_diagnostics(
	module: ModuleDecl, ambiguous_names: set[str], localizer: Localizer,
) -> list[Diagnostic]:
	# Project-wide leaf aliases are only usable when unique. In particular,
	# an ambiguous declared Int8 must not fall back to the numeric alias.
	local_names = {entry.name for entry in module.classes if not entry.name.startswith("_")}
	ambiguous_names = ambiguous_names - local_names
	if not ambiguous_names:
		return []
	functions = [(entry, False) for entry in module.functions if not entry.name.startswith("_")]
	annotations: list[TypeRef | None] = []
	for entry in module.classes:
		if entry.name.startswith("_") or entry.is_enum:
			continue
		annotations.extend(field.type for field in entry.fields if not field.is_class_variable)
		functions.extend(
			(method, not any(_leaf(decorator) == "staticmethod" for decorator in method.decorators))
			for method in entry.methods
			if method.name == "__init__" or not method.name.startswith("_")
		)
	for function, skip_receiver in functions:
		annotations.append(function.return_type)
		annotations.extend(parameter.type for parameter in function.parameters[int(skip_receiver):])
	diagnostics: list[Diagnostic] = []
	while annotations:
		type_ref = annotations.pop()
		if type_ref is None:
			continue
		if type_ref.name in ambiguous_names:
			diagnostics.append(Diagnostic(
				"PYB1002",
				localizer.message("python_tools.unsupported_type",
								  f"{type_ref.spelling} (ambiguous; use a qualified type name)"),
				type_ref.location,
			))
		annotations.extend(type_ref.arguments)
	return diagnostics


def generate_binding_project(
	paths: Sequence[str | Path],
	*,
	project_root: str | Path | None = None,
	language: str | None = None,
	catalog_directory: str | None = None,
) -> BindGenProjectResult:
	project = parse_files(
		paths,
		project_root=project_root,
		language=language,
		catalog_directory=catalog_directory,
	)
	diagnostics = list(project.diagnostics)
	if not project.succeeded:
		return BindGenProjectResult(None, "", (), "", tuple(diagnostics))

	records: dict[str, str] = {}
	enums: dict[str, str] = {}
	leaf_records: dict[str, list[str]] = {}
	leaf_enums: dict[str, list[str]] = {}
	for project_module in project.modules:
		module = project_module.module
		for entry in module.classes:
			if entry.name.startswith("_"):
				continue
			destination = enums if entry.is_enum else records
			leaves = leaf_enums if entry.is_enum else leaf_records
			qualified = _qualified(module, entry.name)
			destination[f"{module.name}.{entry.name}"] = qualified
			destination[qualified] = qualified
			leaves.setdefault(entry.name, []).append(qualified)
	ambiguous_names: set[str] = set()
	for name in leaf_records.keys() | leaf_enums.keys():
		record_names = leaf_records.get(name, [])
		enum_names = leaf_enums.get(name, [])
		if len(record_names) + len(enum_names) == 1:
			if record_names:
				records[name] = record_names[0]
			else:
				enums[name] = enum_names[0]
		else:
			ambiguous_names.add(name)

	types: list[dict[str, object]] = []
	functions: list[dict[str, object]] = []
	references: list[BindGenProjectFile] = []
	localizer = Localizer(language, catalog_directory)
	for project_module in project.modules:
		ambiguous = _ambiguous_type_diagnostics(project_module.module, ambiguous_names, localizer)
		if ambiguous:
			diagnostics.extend(ambiguous)
			continue
		generated = generate_bindings(
			project_module.module,
			language=language,
			catalog_directory=catalog_directory,
			known_records=records,
			known_enums=enums,
		)
		diagnostics.extend(
			item for item in generated.diagnostics
			if item not in project_module.module.diagnostics
		)
		if not generated.succeeded or generated.catalog is None:
			continue
		types.extend(cast(list[dict[str, object]], generated.catalog["types"]))
		functions.extend(cast(list[dict[str, object]], generated.catalog["functions"]))
		output_name = str(Path(project_module.relative_path).with_suffix(".vs.txt")).replace("\\", "/")
		references.append(
			BindGenProjectFile(
				project_module.input_path,
				output_name,
				generated.api_reference,
			)
		)
	if any(item.severity == "error" for item in diagnostics):
		return BindGenProjectResult(None, "", (), "", tuple(diagnostics))

	catalog: dict[str, object] = {
		"version": 1,
		"functions": functions,
		"genericTypeTemplates": [],
		"types": types,
		"typeAliases": [],
		"annotations": [],
	}
	catalog_json = json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"
	manifest = {
		"version": 1,
		"kind": "veroscript.python-binding-project",
		"root": project.root,
		"catalog": "bindings.json",
		"inputs": [entry.input_path for entry in project.modules],
		"apiReferences": [
			{"input": entry.input_path, "output": entry.output_name}
			for entry in references
		],
	}
	return BindGenProjectResult(
		catalog,
		catalog_json,
		tuple(references),
		json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
		tuple(diagnostics),
	)


def validate_catalog(
	catalog_path: str | Path,
	executable: str | Path | None = None,
	*,
	timeout_seconds: float = 120.0,
) -> tuple[bool, str]:
	if not math.isfinite(timeout_seconds) or timeout_seconds <= 0:
		return False, Localizer().message("python_tools.timeout_invalid")
	resolved = find_veroscript_executable(executable)
	if resolved is None:
		return False, Localizer().message("python_tools.validator_not_found")
	try:
		completed = subprocess.run(
			[resolved, "bindings", "validate", str(catalog_path)],
			check=False,
			capture_output=True,
			text=True,
			encoding="utf-8",
			errors="replace",
			timeout=timeout_seconds,
		)
	except (OSError, subprocess.TimeoutExpired) as error:
		return False, str(error)
	output = "\n".join(value.strip() for value in (completed.stdout, completed.stderr) if value.strip())
	return completed.returncode == 0, output


def _runtime_type_name(entry: Mapping[str, object], index: int | None = None) -> str:
	prefix = "return" if index is None else "param"
	native_value = entry[f"{prefix}Type"] if index is None else cast(
		Sequence[object], entry["paramTypes"]
	)[index]
	native = str(native_value)
	primitives = {
		"void": "void",
		"bool": "bool",
		"int8": "int8",
		"uint8": "uint8",
		"int16": "int16",
		"uint16": "uint16",
		"int": "int",
		"unsigned": "uint32",
		"uint": "uint32",
		"int64": "int64",
		"uint64": "uint64",
		"float": "float",
		"double": "double",
		"string": "string",
	}
	if native in primitives:
		return primitives[native]
	def selected(single: str, plural: str) -> str:
		value = entry[single] if index is None else cast(Sequence[object], entry[plural])[index]
		# Catalog spelling uses ::; Core binding identities use dotted names.
		return str(value).replace("::", ".")
	host = selected("returnHostTypeName", "paramHostTypeNames")
	if native == "object":
		if not host:
			raise ValueError("binding catalog host-object type is missing its name")
		return host
	if native == "enum":
		if not host:
			raise ValueError("binding catalog enum type is missing its name")
		return f"enum<{host}>"
	element = selected("returnElementTypeName", "paramElementTypeNames")
	if native in {"array", "set", "queue", "stack", "deque", "channel",
				  "priority_queue", "multi_set", "ordered_multi_set"}:
		if not element:
			raise ValueError(f"binding catalog {native} type is missing its element type")
		return f"{native}<{element}>"
	key = selected("returnKeyTypeName", "paramKeyTypeNames")
	value = selected("returnValueTypeName", "paramValueTypeNames")
	if native in {"map", "ordered_map", "multi_map", "ordered_multi_map"}:
		if not key or not value:
			raise ValueError(f"binding catalog {native} type is missing key/value types")
		return f"{native}<{key},{value}>"
	raise ValueError(f"unsupported runtime binding type: {native}")


def _binding_catalog(
	bindings: BindGenResult | BindGenProjectResult | Mapping[str, object],
) -> Mapping[str, object]:
	if isinstance(bindings, BindGenResult):
		if not bindings.succeeded or bindings.catalog is None:
			raise ValueError("cannot bind an unsuccessful BindGen result")
		return bindings.catalog
	if isinstance(bindings, BindGenProjectResult):
		if not bindings.succeeded or bindings.catalog is None:
			raise ValueError("cannot bind an unsuccessful BindGen project result")
		return bindings.catalog
	return bindings


def _callable_has_exact_positional_shape(
	callback: object,
	positional_count: int,
) -> bool:
	try:
		callable_value = cast(Callable[..., object], callback)
		# Only the call shape matters; lazy annotations must remain unevaluated.
		if sys.version_info >= (3, 14):
			signature = inspect.signature(callable_value, annotation_format=Format.STRING)
		else:
			signature = inspect.signature(callable_value)
		parameters = tuple(signature.parameters.values())
	except (TypeError, ValueError):
		return False
	return len(parameters) == positional_count and all(
		parameter.kind
		in {inspect.Parameter.POSITIONAL_ONLY, inspect.Parameter.POSITIONAL_OR_KEYWORD}
		and parameter.default is inspect.Parameter.empty
		for parameter in parameters
	)


def _bind_catalog(
	session: Any,
	catalog: Mapping[str, object],
	modules_by_owner: Mapping[str, ModuleType],
) -> None:
	if catalog.get("version") != 1:
		raise ValueError("unsupported binding catalog version")
	types = cast(Sequence[Mapping[str, object]], catalog.get("types", ()))

	# Resolve every module and top-level object before mutating the session, so
	# a missing project module cannot leave a partially registered catalog.
	resolved_types: list[tuple[Mapping[str, object], type]] = []
	for entry in types:
		qualified_name = str(entry["name"])
		owner, separator, python_name = qualified_name.rpartition("::")
		if not separator or not owner or not python_name:
			raise ValueError(f"invalid qualified binding type name: {qualified_name!r}")
		module = modules_by_owner.get(owner)
		if module is None:
			raise ValueError(
				f"no explicitly imported Python module was provided for {owner!r}"
			)
		python_object = getattr(module, python_name)
		if not isinstance(python_object, type):
			raise TypeError(f"{module.__name__}.{python_name} is not a Python type")
		resolved_types.append((entry, python_object))

	functions = cast(Sequence[Mapping[str, object]], catalog.get("functions", ()))
	resolved_functions: list[tuple[Mapping[str, object], object]] = []
	for entry in functions:
		qualified_name = str(entry["name"])
		owner, separator, python_name = qualified_name.rpartition("::")
		if not separator or not owner or not python_name:
			raise ValueError(f"invalid qualified binding function name: {qualified_name!r}")
		module = modules_by_owner.get(owner)
		if module is None:
			raise ValueError(
				f"no explicitly imported Python module was provided for {owner!r}"
			)
		callback = getattr(module, python_name)
		if not callable(callback):
			raise TypeError(f"{module.__name__}.{python_name} is not callable")
		resolved_functions.append((entry, callback))

	seen_names: set[str] = set()
	for entry, python_type in resolved_types:
		qualified_name = str(entry["name"])
		if qualified_name in seen_names:
			raise ValueError(f"duplicate runtime binding name: {qualified_name}")
		seen_names.add(qualified_name)
		kind = str(entry["kind"])
		if kind not in {"class", "struct", "enum"}:
			raise ValueError(f"unsupported runtime binding type kind: {kind}")
		if kind == "enum":
			if not issubclass(python_type, (IntEnum, IntFlag)):
				raise TypeError(
					f"{python_type.__module__}.{python_type.__qualname__} "
					"must derive from IntEnum or IntFlag"
				)
			expected_members = tuple(
				(str(member["name"]), cast(int, member["enumValue"]))
				for member in cast(
					Sequence[Mapping[str, object]], entry.get("members", ())
				)
			)
			actual_members = tuple(
				(member_name, int(member.value))
				for member_name, member in python_type.__members__.items()
			)
			if actual_members != expected_members:
				raise ValueError(
					f"{python_type.__module__}.{python_type.__qualname__} "
					"members do not match the binding catalog"
				)
			continue
		members = cast(
			Sequence[Mapping[str, object]], entry.get("members", ())
		)
		fields_by_name: dict[str, Mapping[str, object]] = {}
		getters_by_name: dict[str, Mapping[str, object]] = {}
		setters_by_name: dict[str, Mapping[str, object]] = {}
		exposed_members: dict[str, str] = {}
		constructor_count = 0
		for member in members:
			member_kind = str(member["kind"])
			member_name = str(member["name"])
			if member_kind == "constructor":
				constructor_count += 1
				if constructor_count > 1:
					raise ValueError(
						f"duplicate runtime binding constructor in {qualified_name}"
					)
			if member_kind in {"field", "method", "staticMethod", "propertyGetter"}:
				previous_kind = exposed_members.get(member_name)
				if previous_kind is not None:
					raise ValueError(
						f"runtime binding member {member_name} is declared as both "
						f"{previous_kind} and {member_kind} in {qualified_name}"
					)
				exposed_members[member_name] = member_kind
			destination = (
				fields_by_name
				if member_kind == "field"
				else getters_by_name
				if member_kind == "propertyGetter"
				else setters_by_name
				if member_kind == "propertySetter"
				else None
			)
			if destination is not None:
				if member_name in destination:
					raise ValueError(
						f"duplicate {member_kind} runtime binding: "
						f"{qualified_name}.{member_name}"
					)
				destination[member_name] = member
		for member_name, target in (*fields_by_name.items(), *getters_by_name.items()):
			target_parameters = cast(Sequence[object], target["paramTypes"])
			if target_parameters or str(target["returnType"]) == "void":
				raise ValueError(
					f"invalid runtime binding target contract: "
					f"{qualified_name}.{member_name}"
				)
		for member_name, setter in setters_by_name.items():
			setter_target = getters_by_name.get(member_name)
			if setter_target is None:
				setter_target = fields_by_name.get(member_name)
			if setter_target is None:
				raise ValueError(
					f"runtime binding setter has no field or property getter: "
					f"{qualified_name}.{member_name}"
				)
			setter_parameters = cast(Sequence[object], setter["paramTypes"])
			if (
				str(setter["returnType"]) != "void"
				or len(setter_parameters) != 1
				or _runtime_type_name(setter, 0) != _runtime_type_name(setter_target)
			):
				raise ValueError(
					f"runtime binding setter contract does not match its target: "
					f"{qualified_name}.{member_name}"
				)
		writable_names = set(setters_by_name)
		for member in members:
			member_kind = str(member["kind"])
			if member_kind not in {
				"constructor",
				"field",
				"method",
				"staticMethod",
				"propertyGetter",
				"propertySetter",
			}:
				raise ValueError(
					f"unsupported runtime binding member kind: {member_kind}"
				)
			if member_kind in {
				"field",
				"method",
				"staticMethod",
				"propertyGetter",
			}:
				_runtime_type_name(member)
			if member_kind in {"constructor", "method", "staticMethod"}:
				for index in range(
					len(cast(Sequence[object], member["paramTypes"]))
				):
					_runtime_type_name(member, index)
			member_name = str(member["name"])
			parameter_count = len(cast(Sequence[object], member["paramTypes"]))
			if member_kind == "constructor":
				constructor = inspect.getattr_static(python_type, "__init__")
				if not _callable_has_exact_positional_shape(
					constructor, parameter_count + 1
				):
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						"__init__ does not match the binding catalog"
					)
			if member_kind in {"method", "staticMethod"}:
				descriptor = inspect.getattr_static(python_type, member_name)
				callback = getattr(python_type, member_name)
				if not callable(callback):
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						f"{member_name} is not callable"
					)
				if member_kind == "method" and isinstance(
					descriptor, (staticmethod, classmethod)
				):
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						f"{member_name} must be an instance method"
					)
				if member_kind == "staticMethod" and not isinstance(
					descriptor, (staticmethod, classmethod)
				):
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						f"{member_name} must be a static or class method"
					)
				shape_callback = (
					descriptor.__func__
					if isinstance(descriptor, (staticmethod, classmethod))
					else descriptor
				)
				receiver_count = 0 if isinstance(descriptor, staticmethod) else 1
				if not _callable_has_exact_positional_shape(
					shape_callback, parameter_count + receiver_count
				):
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						f"{member_name} does not match the binding catalog"
					)
			elif member_kind == "propertyGetter":
				descriptor = inspect.getattr_static(python_type, member_name)
				if not isinstance(descriptor, property) or descriptor.fget is None:
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						f"{member_name} is not a readable property"
					)
				if not _callable_has_exact_positional_shape(descriptor.fget, 1):
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						f"{member_name} getter must accept exactly self"
					)
				if member_name in writable_names and descriptor.fset is None:
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						f"{member_name} is not a writable property"
					)
				if (
					member_name in writable_names
					and descriptor.fset is not None
					and not _callable_has_exact_positional_shape(descriptor.fset, 2)
				):
					raise TypeError(
						f"{python_type.__module__}.{python_type.__qualname__}."
						f"{member_name} setter must accept exactly self and value"
					)
	for entry, callback in resolved_functions:
		qualified_name = str(entry["name"])
		if qualified_name in seen_names:
			raise ValueError(f"duplicate runtime binding name: {qualified_name}")
		seen_names.add(qualified_name)
		_runtime_type_name(entry)
		parameter_count = len(cast(Sequence[object], entry["paramTypes"]))
		for index in range(parameter_count):
			_runtime_type_name(entry, index)
		if not _callable_has_exact_positional_shape(callback, parameter_count):
			raise TypeError(f"{qualified_name} does not match the binding catalog")

	registered: dict[str, Any] = {}
	for entry, python_type in resolved_types:
		qualified_name = str(entry["name"])
		if entry["kind"] == "enum":
			session.register_enum(
				qualified_name.replace("::", "."),
				python_type,
				documentation=str(entry.get("documentation", "")),
				underlying_type=str(entry.get("enumUnderlyingType", "int64")) or "int64",
			)
			continue
		kind = str(entry["kind"])
		if kind == "class":
			binding = session.register_class(
				qualified_name.replace("::", "."),
				python_type,
				documentation=str(entry.get("documentation", "")),
				inheritable=bool(entry.get("inheritable", False)),
			)
		elif kind == "struct":
			binding = session.register_struct(
				qualified_name.replace("::", "."),
				python_type,
				documentation=str(entry.get("documentation", "")),
				inheritable=bool(entry.get("inheritable", False)),
			)
		else:
			raise ValueError(f"unsupported runtime binding type kind: {kind}")
		registered[qualified_name] = binding

	# Cross-module member signatures may reference a type whose catalog entry
	# appears later. Register every host type before attaching any members.
	for entry, _ in resolved_types:
		qualified_name = str(entry["name"])
		if entry["kind"] == "enum":
			continue
		binding = registered[qualified_name]
		members = cast(Sequence[Mapping[str, object]], entry.get("members", ()))
		writable_names = {
			str(member["name"])
			for member in members
			if member["kind"] == "propertySetter"
		}
		handled_properties: set[str] = set()
		for member in members:
			kind = str(member["kind"])
			name = str(member["name"])
			parameters = tuple(
				_runtime_type_name(member, index)
				for index in range(len(cast(Sequence[object], member["paramTypes"])))
			)
			documentation = str(member.get("documentation", ""))
			if kind == "constructor":
				binding.constructor(parameters=parameters, documentation=documentation)
			elif kind == "field":
				binding.field(
					name,
					type_name=_runtime_type_name(member),
					writable=name in writable_names,
					documentation=documentation,
				)
			elif kind == "method":
				binding.method(
					name,
					return_type=_runtime_type_name(member),
					parameters=parameters,
					documentation=documentation,
				)
			elif kind == "staticMethod":
				binding.static_method(
					name,
					return_type=_runtime_type_name(member),
					parameters=parameters,
					documentation=documentation,
				)
			elif kind == "propertyGetter":
				binding.property(
					name,
					type_name=_runtime_type_name(member),
					writable=name in writable_names,
					documentation=documentation,
				)
				handled_properties.add(name)
			elif kind == "propertySetter" and name not in handled_properties:
				# Field setters are registered together with their field. A
				# setter without a matching getter is not a valid Python property.
				continue
	for entry, callback in resolved_functions:
		qualified_name = str(entry["name"])
		parameters = tuple(
			_runtime_type_name(entry, index)
			for index in range(len(cast(Sequence[object], entry["paramTypes"])))
		)
		session.register_function(
			qualified_name.replace("::", "."),
			callback,
			return_type=_runtime_type_name(entry),
			parameters=parameters,
			documentation=str(entry.get("documentation", "")),
		)


def bind_module(
	session: Any,
	module: ModuleType,
	bindings: BindGenResult | Mapping[str, object],
) -> None:
	"""Register one generated catalog against an explicitly imported module.

    BindGen never imports or executes analyzed source. At runtime the
    application imports the module itself and passes that module here, making
    execution and dependency policy explicit while closing the generated
    catalog-to-``veroscript.Session`` registration path.
    """

	catalog = _binding_catalog(bindings)
	owners = {
		str(entry["name"]).rpartition("::")[0]
		for section in ("types", "functions")
		for entry in cast(Sequence[Mapping[str, object]], catalog.get(section, ()))
	}
	owners.discard("")
	if len(owners) > 1:
		raise ValueError("a multi-module binding catalog requires bind_modules()")
	owner = next(iter(owners), module.__name__.replace(".", "::"))
	_bind_catalog(session, catalog, {owner: module})


def bind_modules(
	session: Any,
	modules: Mapping[str, ModuleType] | Sequence[ModuleType],
	bindings: BindGenProjectResult | BindGenResult | Mapping[str, object],
) -> None:
	"""Register a generated project against explicitly imported modules.

    Mapping keys are dotted source-module names (for example,
    ``"sample.models"``). A sequence uses each module's ``__name__``. Every
    catalog owner must be present; validation occurs before session mutation.
    """

	module_items: Iterable[tuple[str, ModuleType]]
	if isinstance(modules, Mapping):
		module_items = modules.items()
	else:
		module_items = ((module.__name__, module) for module in modules)
	modules_by_owner: dict[str, ModuleType] = {}
	for source_name, module in module_items:
		if not isinstance(module, ModuleType):
			raise TypeError("modules must contain imported Python module objects")
		owner = str(source_name).replace(".", "::")
		if not owner or owner in modules_by_owner:
			raise ValueError(f"duplicate or empty Python module name: {source_name!r}")
		modules_by_owner[owner] = module
	if not modules_by_owner:
		raise ValueError("at least one explicitly imported Python module is required")
	_bind_catalog(session, _binding_catalog(bindings), modules_by_owner)


__all__ = [
	"__version__",
	"BindGenResult",
	"BindGenProjectFile",
	"BindGenProjectResult",
	"MappedType",
	"bind_module",
	"bind_modules",
	"generate_bindings",
	"generate_bindings_from_file",
	"generate_bindings_from_source",
	"generate_binding_project",
	"validate_catalog",
]
