# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_csharp_consumer import FRAMEWORKS, RESULT_PREFIX, parse_result, validate_runtime


class CSharpConsumerInputsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="CSharp SDK with spaces ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        pe = bytearray(70)
        pe[:2] = b"MZ"
        struct.pack_into("<I", pe, 60, 64)
        pe[64:68] = b"PE\0\0"
        struct.pack_into("<H", pe, 68, 0x8664)
        for framework in FRAMEWORKS:
            folder = self.root / "lib/dotnet" / framework
            folder.mkdir(parents=True)
            (folder / "VeroScriptInterop.dll").write_bytes(pe)
            (folder / "VeroScriptCSharp.dll").write_bytes(b"managed SDK fixture")
            (folder / "dependency.dll").write_bytes(b"dependency fixture")
            (folder / "localization").mkdir()
            (folder / "localization/en.json").write_text("{}")

    def test_all_frameworks_and_nested_runtime_resources_are_inputs(self):
        inputs = validate_runtime(self.root, "x64")
        self.assertEqual(set(inputs), set(FRAMEWORKS))
        self.assertIn("dependency.dll", inputs["net8.0"]["files"])
        self.assertIn("localization/en.json", inputs["net9.0"]["files"])

    def test_missing_second_framework_native_library_fails(self):
        (self.root / "lib/dotnet/net9.0/VeroScriptInterop.dll").unlink()
        with self.assertRaisesRegex(ValueError, "Missing packaged"):
            validate_runtime(self.root, "x64")

    def test_wrong_architecture_and_invalid_pe_fail_before_build(self):
        with self.assertRaisesRegex(ValueError, "architecture does not match"):
            validate_runtime(self.root, "x86")
        (self.root / "lib/dotnet/net8.0/VeroScriptInterop.dll").write_bytes(b"not PE")
        with self.assertRaisesRegex(ValueError, "Not a PE"):
            validate_runtime(self.root, "x64")

    def test_zero_exit_without_real_result_or_wrong_architecture_is_not_passed(self):
        for text in ("Build succeeded", RESULT_PREFIX + json.dumps({"processArchitecture": "x86", "answer": 42}),
                     RESULT_PREFIX + json.dumps({"processArchitecture": "x64", "answer": 0})):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_result(text, "x64")
        text = RESULT_PREFIX + json.dumps({"processArchitecture": "x64", "answer": 42})
        self.assertEqual(parse_result(text, "x64")["answer"], 42)
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_result(text + "\n" + text, "x64")


if __name__ == "__main__":
    unittest.main()
