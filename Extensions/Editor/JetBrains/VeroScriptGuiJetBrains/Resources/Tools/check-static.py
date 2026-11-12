# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Small source/resource checks, without Gradle, JVM or native process startup."""
from pathlib import Path
import json
import re
import runpy
import sys
import xml.etree.ElementTree as ET


def check(plugin):
	repo = plugin.parents[3]
	descriptor = ET.parse(plugin / "Resources/META-INF/plugin.xml").getroot()
	assert descriptor.findtext("id") == "com.vanstysanic.veroscript.gui"
	assert "com.vanstysanic.veroscript" in [node.text for node in descriptor.findall("depends")]
	classes = set()
	sources = list((plugin / "src").rglob("*.kt"))
	for path in sources:
		text = path.read_text(encoding="utf-8")
		assert text.startswith("// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n"), path
		package = re.search(r'^package ([\w.]+)', text, re.M)[1]
		classes.update(package + "." + name for name in re.findall(r'^(?:abstract )?(?:data )?(?:class|object) (\w+)', text, re.M))
	for element in descriptor.iter():
		for key in ("implementationClass", "implementation", "class", "className", "forClass"):
			if element.get(key):
				assert element.get(key) in classes, (key, element.get(key))
	resources = plugin / "Resources/Localization/messages"
	bundles = {}
	for path in sorted(resources.glob("*.properties")):
		pairs = [line.partition("=") for line in path.read_text(encoding="utf-8").splitlines()
				 if line and not line.startswith("#")]
		assert all(separator and value.strip() for key, separator, value in pairs), path
		keys = [key for key, _, _ in pairs]
		assert len(keys) == len(set(keys)), path
		bundles[path.name] = {key: value for key, _, value in pairs}
	assert len(bundles) == 6
	reference = bundles["VeroScriptGuiBundle.properties"]
	for filename, bundle in bundles.items():
		assert bundle.keys() == reference.keys(), filename
		for key, value in bundle.items():
			assert re.findall(r'\{\d+\}', value) == re.findall(r'\{\d+\}', reference[key]), (filename, key)
			assert "'" not in value, (filename, key, "Review MessageFormat quoting")
	for path in sources:
		for key in re.findall(r'message\("([\w.]+)"', path.read_text(encoding="utf-8")):
			assert key in reference, (path, key)
	for key in ("run.previewName", "run.checkName", "run.compileName"):
		assert key in reference
	# Inspect owned inputs only; Gradle caches/sandboxes may contain a complete SDK.
	owned = [path for root in (plugin / "src", plugin / "Resources") for path in root.rglob("*")]
	owned.extend(plugin.iterdir())
	for path in owned:
		if path.is_file() and "Licenses" not in path.parts and path.suffix in {
			".kt", ".kts", ".properties", ".md", ".xml", ".svg", ".py", ".json"
		}:
			assert all(line == line.rstrip() for line in path.read_text(encoding="utf-8").splitlines()), path
	docs = list((plugin / "Resources/Docs").glob("*/*/index.md"))
	assert len(docs) == 12
	link_count = 0
	for path in docs + [plugin / "README.md"]:
		for target in re.findall(r'\]\(([^)]+)\)', path.read_text(encoding="utf-8")):
			if re.match(r'[\w+.-]+:', target) or target.startswith("#"):
				continue
			link_count += 1
			assert (path.parent / target.split("#")[0]).resolve().is_file(), (path, target)
	module_manifest = json.loads((plugin / "Resources/Docs/manifest.json").read_text(encoding="utf-8"))
	route = "$module/" + module_manifest["module"] + "/Resources/Docs/manifest.json"
	documentation = json.loads((repo / "Resources/Docs/documentation.json").read_text(encoding="utf-8"))
	assert documentation["modules"].count(route) == 1
	for locale in documentation["locales"]:
		root_guide = repo / "Resources/Docs/User" / locale / "index.md"
		assert "VeroScriptGuiJetBrains/Resources/Docs/User/" + locale + "/index.md" in root_guide.read_text(encoding="utf-8"), root_guide
	generator = runpy.run_path(str(plugin / "Resources/Tools/update-catalog.py"))
	actual = json.loads((plugin / "Resources/Catalog/gui.json").read_text(encoding="utf-8"))
	assert actual["schemaVersion"] == 2
	assert generator["catalog"](plugin) == actual, "GUI source metadata changed; regenerate and review the catalog"
	names = {item["name"] for item in actual["types"]}
	assert len(names) == len(actual["types"]), "Duplicate GUI type names"
	aliases = {}
	for item in actual["types"]:
		for alias in {item["name"], item["mark"], item["qualifiedName"]}:
			assert alias and aliases.get(alias, item["name"]) == item["name"], ("Ambiguous GUI tag", alias)
			aliases[alias] = item["name"]
	assert all(not item["base"] or item["base"] in names for item in actual["types"])
	for name in ("gui.svg", "gui_dark.svg", "pluginIcon.svg", "pluginIcon_dark.svg"):
		ET.parse(plugin / "Resources/Assets/Icons" / name)
	base = plugin.parent / "VeroScriptJetBrains"
	for name in ("Gradle-LICENSE.txt", "Gradle-NOTICE.txt"):
		relative = "Resources/Licenses/" + name
		assert (base / relative).read_bytes() == (plugin / relative).read_bytes()
	assert (repo / "LICENSE").is_file()
	print(json.dumps({"kotlin_files": len(sources), "ui_keys_per_locale": len(reference),
					  "documentation_pages": len(docs), "local_links": link_count,
					  "gui_builtin_types": len(actual["types"]), "routed_events": len(actual["routedEvents"]),
					  "validation": "static only; no compilation, tests, JVM or native execution"}, indent=2))


if __name__ == "__main__":
	sys.dont_write_bytecode = True
	check(Path(__file__).resolve().parents[2])
