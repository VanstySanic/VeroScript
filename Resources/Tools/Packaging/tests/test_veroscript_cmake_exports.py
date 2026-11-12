# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_cmake_exports import (PRIVATE_DEPENDENCIES, normalize_curl_export_text,
    normalize_curl_exports, verify_curl_export_text, verify_curl_export_entries)


class CurlExportRelocationTests(unittest.TestCase):
    def configuration(self, value):
        return 'set(CURL_LIBRARIES CURL::libcurl)\nset(CURL_LIBRARIES_PRIVATE "' + value + '")\n'

    def test_absolute_unix_windows_system_and_link_options_are_repaired(self):
        for value in ('/home/builder/libssl.a;/home/builder/libcrypto.a;-pthread',
                      'D:/private tools/lib/ssl.lib;C:\\private\\crypto.lib',
                      '/usr/lib/x86_64-linux-gnu/libssl.so', '-L/host/lib;-lssl',
                      '$<LINK_ONLY:/private/libssl.a>', '//server/share/libssl.a',
                      r'\\server\share\libssl.lib'):
            with self.subTest(value=value):
                source = self.configuration(value)
                with self.assertRaisesRegex(ValueError, 'Non-relocatable'):
                    verify_curl_export_text(source)
                normalized = normalize_curl_export_text(source)
                self.assertIn(PRIVATE_DEPENDENCIES, normalized)
                self.assertEqual(normalize_curl_export_text(normalized), normalized)

    def test_parentheses_and_bracket_arguments_are_complete_or_safely_rejected(self):
        values = ('"C:/Program Files (x86)/OpenSSL/libssl.lib"',
                  '"/tmp/build (test)/libssl.a"', '[=[/tmp/build (test)/libssl.a]=]')
        for value in values:
            with self.subTest(value=value):
                source = 'set(CURL_LIBRARIES CURL::libcurl)\nset(CURL_LIBRARIES_PRIVATE ' + value + ')\nset(RETAINED sentinel)\n'
                normalized = normalize_curl_export_text(source)
                self.assertEqual(normalized, 'set(CURL_LIBRARIES CURL::libcurl)\n' + PRIVATE_DEPENDENCIES + '\nset(RETAINED sentinel)\n')
        for value in ('"/tmp/unclosed', '[=[/tmp/unclosed]'):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'Unterminated'):
                normalize_curl_export_text('set(CURL_LIBRARIES CURL::libcurl)\nset(CURL_LIBRARIES_PRIVATE ' + value)

    def test_portable_platform_libraries_and_install_prefix_expressions_remain_exact(self):
        for value in ('crypt32;ws2_32;advapi32', 'OpenSSL::SSL;Threads::Threads',
                      '${CMAKE_CURRENT_LIST_DIR}/../../../lib/libssl.a', ''):
            source = self.configuration(value)
            self.assertEqual(normalize_curl_export_text(source), source)
            verify_curl_export_text(source)

    def test_crlf_and_non_dependency_text_are_preserved(self):
        source = '# Third-party copyright\r\n' + self.configuration('/tmp/build/libssl.a').replace('\n', '\r\n') + '# retained\r\n'
        normalized = normalize_curl_export_text(source)
        self.assertTrue(normalized.startswith('# Third-party copyright\r\n'))
        self.assertTrue(normalized.endswith('# retained\r\n'))
        self.assertNotIn('\n', normalized.replace('\r\n', ''))

    def test_ambiguous_or_unsupported_target_export_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            normalize_curl_export_text(self.configuration('/tmp/libssl.a') + 'set(CURL_LIBRARIES_PRIVATE "ws2_32")\n')
        with self.assertRaisesRegex(ValueError, 'selected imported target'):
            normalize_curl_export_text('set(CURL_LIBRARIES_PRIVATE "/tmp/libssl.a")\n')
        malformed = self.configuration('/tmp/libssl.a').replace('CURL::libcurl)', 'CURL::unexpected)')
        with self.assertRaises(ValueError):
            normalize_curl_export_text(malformed)

    def test_archive_validation_rejects_stale_config_even_with_new_inventory(self):
        path = 'sdk/lib/cmake/CURL/CURLConfig.cmake'
        bad = self.configuration('/private/libssl.a').encode()
        with self.assertRaisesRegex(ValueError, 'Non-relocatable'):
            verify_curl_export_entries({path: (bad, 0o644)})
        with self.assertRaisesRegex(ValueError, 'not provided'):
            verify_curl_export_entries({path: (b'', 0o644)})
        verify_curl_export_entries({path: normalize_curl_export_text(bad.decode()).encode()})

    @unittest.skipUnless(shutil.which('cmake'), 'CMake is required for actual export evaluation')
    def test_real_cmake_resolves_legacy_dependencies_from_current_target_graph(self):
        with tempfile.TemporaryDirectory(prefix='relocated curl exports ') as temporary:
            root = Path(temporary)
            folder = root / 'moved sdk/lib/cmake/CURL'
            folder.mkdir(parents=True)
            source = '''add_library(OpenSSL::SSL INTERFACE IMPORTED)
add_library(OpenSSL::Crypto INTERFACE IMPORTED)
add_library(ConsumerExtra::Compression INTERFACE IMPORTED)
add_library(CURL::libcurl_static INTERFACE IMPORTED)
set_target_properties(CURL::libcurl_static PROPERTIES INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:OpenSSL::SSL>;OpenSSL::Crypto;ConsumerExtra::Compression;-pthread")
add_library(CURL::libcurl ALIAS CURL::libcurl_static)
''' + self.configuration('C:/Program Files (x86)/former machine/libssl.a;/tmp/former (host)/libcrypto.a')
            (folder / 'CURLConfig.cmake').write_text(source)
            self.assertEqual(normalize_curl_exports(root / 'moved sdk'), ['lib/cmake/CURL/CURLConfig.cmake'])
            (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.25)
project(CurlRelocation LANGUAGES NONE)
find_package(CURL CONFIG REQUIRED PATHS "${CMAKE_CURRENT_SOURCE_DIR}/moved sdk/lib/cmake/CURL" NO_DEFAULT_PATH)
if(NOT CURL_LIBRARIES_PRIVATE STREQUAL "$<LINK_ONLY:OpenSSL::SSL>;OpenSSL::Crypto;ConsumerExtra::Compression;-pthread")
  message(FATAL_ERROR "Legacy private dependencies lost the actual target graph/order: ${CURL_LIBRARIES_PRIVATE}")
endif()
add_library(Consumer INTERFACE)
target_link_libraries(Consumer INTERFACE ${CURL_LIBRARIES} ${CURL_LIBRARIES_PRIVATE})
''')
            completed = subprocess.run([shutil.which('cmake'), '-S', str(root), '-B', str(root / 'consumer')], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)


if __name__ == '__main__':
    unittest.main()
