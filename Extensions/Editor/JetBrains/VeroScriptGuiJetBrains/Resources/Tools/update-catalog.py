# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Refresh completion metadata from owned GUI sources; no native code is run."""
from pathlib import Path
import hashlib
import json
import re


_NON_CODE = re.compile(
	r"""//[^\r\n]*|/\*.*?\*/|r"[^"]*"|[Ll]?"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""", re.S
)
_WIDGET = re.compile(
	r'@\s*GuiWidget(?:\s*\(([^)]*)\))?\s+export\s+class\s+(\w+)\s*:\s*public\s+VS::Gui::(?:\w+::)*(\w+)\s*\{'
)
_SCOPE = re.compile(r'\bnamespace\s+(\w+(?:\s*::\s*\w+)*)\s*\{|[{}]')
_PROPERTY = re.compile(
	r'@\s*GuiProperty(?:\s*\(([^)]*)\))?\s+std::attribute<([^;={}]+)>\s+(\w+)\s*='
)
_EVENT = re.compile(
	r'@\s*GuiEvent(?:\s*\(([^)]*)\))?\s+std::multicast_delegate<[^;={}]*>\s+(\w+)\s*='
)


def masked_source(text):
	"""Preserve offsets and newlines while excluding comments and string/character literals."""
	return _NON_CODE.sub(lambda match: re.sub(r'[^\r\n]', ' ', match[0]), text)


def framework_sources(scripts):
	"""Follow local imports from the public framework, including transitive support files."""
	scripts = scripts.resolve()
	pending = [scripts / "VeroScriptGui.vs"]
	sources = {}
	while pending:
		path = pending.pop().resolve()
		if path in sources:
			continue
		if not path.is_relative_to(scripts):
			raise ValueError(f"GUI framework import leaves its source directory: {path}")
		raw = path.read_bytes()
		sources[path] = raw
		text = raw.decode("utf-8-sig")
		masked = masked_source(text)
		for match in re.finditer(r'\bimport\s+"([^"]+)"\s*;', text):
			if masked[match.start():match.start() + 6] != "import":
				continue
			dependency = (path.parent / match[1]).resolve()
			if dependency.is_file():
				pending.append(dependency)
			elif "/" in match[1] or "\\" in match[1]:
				raise ValueError(f"Missing local GUI import: {path}: {match[1]}")
			# Bare standard-library imports, such as delegate.vs, are supplied by Core.
	return sources


def widget_types(text):
	"""Read owned GUI declarations; class bounds keep neighboring metadata independent."""
	masked = masked_source(text)
	types = []
	scopes = iter(_SCOPE.finditer(masked))
	scope = next(scopes, None)
	namespace = []
	scope_lengths = []

	def qualified_name(name, offset):
		nonlocal scope
		# Each scope token is visited once, even when a file declares many widgets.
		while scope is not None and scope.start() < offset:
			if scope[0] == "}":
				if not scope_lengths:
					raise ValueError("Unbalanced GUI declaration scope")
				del namespace[scope_lengths.pop():]
			else:
				scope_lengths.append(len(namespace))
				if scope[1] is not None:
					namespace.extend(re.findall(r'\w+', scope[1]))
			scope = next(scopes, None)
		return "::".join([*namespace, name])

	def flags(match, body):
		return body[match.start(1):match.end(1)] if match[1] is not None else ""

	def mark(arguments, name):
		match = re.search(r'\bMark\s*=\s*"([^"]+)"', arguments)
		return match[1] if match else name

	for declaration in _WIDGET.finditer(masked):
		start = end = declaration.end()
		depth = 1
		while end < len(masked) and depth:
			depth += (masked[end] == "{") - (masked[end] == "}")
			end += 1
		if depth:
			raise ValueError(f"Unclosed GUI class: {declaration[2]}")
		body = text[start:end - 1]
		code = masked[start:end - 1]
		properties = []
		for member in _PROPERTY.finditer(code):
			arguments = flags(member, body)
			properties.append({
				"name": member[3], "mark": mark(arguments, member[3]),
				"type": re.sub(r'\s+', ' ', member[2]).strip(),
				"style": bool(re.search(r'\bStyle\b', flags(member, code))), "bindable": True,
			})
		events = [{"name": member[2], "mark": mark(flags(member, body), member[2])}
				  for member in _EVENT.finditer(code)]
		for annotation, entries in (("GuiProperty", properties), ("GuiEvent", events)):
			if len(re.findall(r'@\s*' + annotation + r'\b', code)) != len(entries):
				raise ValueError(f"Unsupported {annotation} declaration in {declaration[2]}")
		arguments = flags(declaration, text)
		types.append({"name": declaration[2], "mark": mark(arguments, declaration[2]),
					  "qualifiedName": qualified_name(declaration[2], declaration.start()),
					  "base": declaration[3], "properties": properties, "events": events})
	if len(re.findall(r'@\s*GuiWidget\b', masked)) != len(types):
		raise ValueError("GUI widget declaration layout changed; review the extractor")
	return types


def catalog(plugin):
	repo = plugin.parents[3]
	gui = repo / "Modules/Multimedia/VeroScriptGui"
	sources = {
		"properties": gui / "Private/VeroScriptGuiTypes.cpp",
		"events": gui / "Private/VeroScriptGuiEvent.cpp",
		"widget": gui / "Resources/Scripts/Gui/Widget.vs",
	}
	inputs = framework_sources(gui / "Resources/Scripts")
	inputs.update({path: path.read_bytes() for path in sources.values() if path not in inputs})
	text = {key: inputs[path].decode("utf-8-sig") for key, path in sources.items()}
	properties = []
	for name, mark, default, style in re.findall(
		r'\{"([^"\n]+)", "([^"\n]+)", ([^\n]+), (true|false)(?:, "[^"\n]*")?\}', text["properties"]
	):
		setter = re.search(r'void Set' + re.escape(name) + r'\(([^()<>]+) value\)', text["widget"])
		if setter is None:
			raise ValueError(f"Missing Widget setter for {name}")
		properties.append({
			"name": name, "mark": mark, "type": setter[1], "style": style == "true",
			"bindable": bool(re.search(r'void Set' + re.escape(name) + r'\(std::attribute<', text["widget"])),
		})
	events = [{"name": name, "mark": mark} for name, mark in re.findall(
		r'\{Platform::EEventType::\w+, "([^"]+)", "([^"]+)"', text["events"]
	)]
	types = [{"name": "Widget", "mark": "Widget", "qualifiedName": "VS::Gui::Widget",
			  "base": "", "properties": properties, "events": []}]
	for path, raw in sorted(inputs.items()):
		if path.suffix == ".vs":
			types.extend(widget_types(raw.decode("utf-8-sig")))
	if len(properties) < 30 or not events or len(types) < 20:
		raise ValueError("GUI metadata source layout changed; review the extractor")
	return {
		"schemaVersion": 2,
		"sources": {path.relative_to(repo).as_posix(): hashlib.sha256(raw.replace(b"\r\n", b"\n")).hexdigest()
					for path, raw in sorted(inputs.items())},
		"routedEvents": events,
		"types": types,
	}


if __name__ == "__main__":
	plugin = Path(__file__).resolve().parents[2]
	destination = plugin / "Resources/Catalog/gui.json"
	destination.parent.mkdir(parents=True, exist_ok=True)
	destination.write_text(json.dumps(catalog(plugin), ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
	print(f"Updated {destination}")
