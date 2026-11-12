# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Regression checks for GUI source discovery and declaration ownership."""
from pathlib import Path
import runpy
import tempfile
import unittest


GENERATOR = runpy.run_path(str(Path(__file__).resolve().parents[2] / "Resources/Tools/update-catalog.py"))


class CatalogTests(unittest.TestCase):
	def test_internal_widget_bases_keep_public_control_inheritance(self):
		source = '''
        namespace VS :: Gui {
            namespace Internal {
                @GuiWidget export class SelectionView : public VS::Gui::ScrollView {
                    @GuiProperty std::attribute<string> SelectedKey=std::attribute<string>("");
                };
            }
            @GuiWidget(Mark="list-view") export class ListView : public VS::Gui::Internal::SelectionView {};
        }
        '''
		base, view = GENERATOR["widget_types"](source)
		self.assertEqual("SelectionView", base["name"])
		self.assertEqual("VS::Gui::Internal::SelectionView", base["qualifiedName"])
		self.assertEqual("ScrollView", base["base"])
		self.assertEqual("SelectedKey", base["properties"][0]["name"])
		self.assertEqual("SelectionView", view["base"])
		self.assertEqual("VS::Gui::ListView", view["qualifiedName"])
		self.assertEqual("list-view", view["mark"])

	def test_class_bounds_default_marks_and_nested_property_types(self):
		source = '''
        @GuiWidget export class Row : public VS::Gui::Widget {
            @GuiProperty std::attribute<array<string>> Labels = std::attribute<array<string>>();
            void Draw() { string example = "} @GuiProperty"; /* } @GuiEvent */ }
        };
        @GuiWidget(Mark = "list-view") export class List : public VS::Gui::Row {
            @GuiProperty(Mark = "selected-key") std::attribute<string> SelectedKey = std::attribute<string>("");
            @GuiProperty(Mark = "Style") std::attribute<bool> Styled = std::attribute<bool>(false);
            @GuiProperty(Style, Mark = "accent") std::attribute<Color> Accent = std::attribute<Color>();
            @GuiEvent(Mark = "changed") std::multicast_delegate<List, string> Changed =
                std::multicast_delegate<List, string>();
        };
        '''
		row, view = GENERATOR["widget_types"](source)
		self.assertEqual("Row", row["qualifiedName"])
		self.assertEqual("List", view["qualifiedName"])
		self.assertEqual("Row", row["mark"])
		self.assertEqual([{"name": "Labels", "mark": "Labels", "type": "array<string>",
						   "style": False, "bindable": True}], row["properties"])
		self.assertEqual([], row["events"])
		self.assertEqual("Row", view["base"])
		self.assertEqual(["selected-key", "Style", "accent"], [item["mark"] for item in view["properties"]])
		self.assertEqual([False, False, True], [item["style"] for item in view["properties"]])
		self.assertEqual([{"name": "Changed", "mark": "changed"}], view["events"])

	def test_unrecognized_or_incomplete_declarations_fail_instead_of_dropping_metadata(self):
		for source in (
			'@GuiWidget export class Row : public VS::Gui::Widget { @GuiProperty int Count=0; };',
			'@GuiWidget export class Row : public VS::Gui::Widget {',
		):
			with self.assertRaises(ValueError):
				GENERATOR["widget_types"](source)

	def test_framework_discovery_follows_transitive_imports_and_terminates_cycles(self):
		with tempfile.TemporaryDirectory(prefix="veroscript-gui-catalog-") as directory:
			root = Path(directory).resolve()
			(root / "Gui").mkdir()
			(root / "VeroScriptGui.vs").write_text(
				'export import "Gui/Collections.vs"; // import "Gui/Missing.vs";\n', encoding="utf-8")
			(root / "Gui/Collections.vs").write_text(
				'export import "Support.vs"; import "delegate.vs";', encoding="utf-8")
			(root / "Gui/Support.vs").write_text('export import "Collections.vs";', encoding="utf-8")
			(root / "Gui/Unused.vs").write_text("", encoding="utf-8")
			sources = GENERATOR["framework_sources"](root)
			self.assertEqual({"VeroScriptGui.vs", "Gui/Collections.vs", "Gui/Support.vs"},
							 {path.relative_to(root).as_posix() for path in sources})

	def test_missing_owned_imports_are_reported(self):
		with tempfile.TemporaryDirectory(prefix="veroscript-gui-catalog-") as directory:
			root = Path(directory).resolve()
			(root / "VeroScriptGui.vs").write_text('export import "Gui/Missing.vs";', encoding="utf-8")
			with self.assertRaisesRegex(ValueError, "Missing local GUI import"):
				GENERATOR["framework_sources"](root)

	def test_catalog_is_stable_across_line_endings_but_detects_source_edits(self):
		plugin = Path(__file__).resolve().parents[2]
		repo = plugin.parents[3]
		baseline = GENERATOR["catalog"](plugin)
		with tempfile.TemporaryDirectory(prefix="veroscript-gui-line-endings-") as directory:
			root = Path(directory).resolve()
			staged_plugin = root / "Extensions/Editor/JetBrains/VeroScriptGuiJetBrains"
			staged_plugin.mkdir(parents=True)
			for name in baseline["sources"]:
				target = root / name
				target.parent.mkdir(parents=True, exist_ok=True)
				target.write_bytes((repo / name).read_bytes().replace(b"\r\n", b"\n"))
			lf = GENERATOR["catalog"](staged_plugin)
			for name in baseline["sources"]:
				target = root / name
				target.write_bytes(target.read_bytes().replace(b"\n", b"\r\n"))
			crlf = GENERATOR["catalog"](staged_plugin)
			self.assertEqual(lf, crlf)
			name = "Modules/Multimedia/VeroScriptGui/Resources/Scripts/VeroScriptGui.vs"
			target = root / name
			target.write_bytes(target.read_bytes() + b"\r\n// Catalog source edit regression.\r\n")
			edited = GENERATOR["catalog"](staged_plugin)
			self.assertNotEqual(crlf["sources"][name], edited["sources"][name])


if __name__ == "__main__":
	unittest.main()
