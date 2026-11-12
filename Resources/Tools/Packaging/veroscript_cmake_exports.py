# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Keep installed curl CMake compatibility exports independent of build paths."""

from pathlib import Path, PurePosixPath
import re
from dataclasses import dataclass


_CONFIG_NAMES = {'curlconfig.cmake', 'curl-config.cmake'}
_PRIVATE_SET = re.compile(r'(?m)^[ \t]*set[ \t]*\([ \t]*CURL_LIBRARIES_PRIVATE\b')
_BRACKET = re.compile(r'\[(=*)\[')
_ABSOLUTE = re.compile(r'(?:^|[;\s"\'\[\]=]|-L|-Wl,-rpath,|\$<(?:LINK_ONLY|BUILD_INTERFACE|INSTALL_INTERFACE):)(?:/|[A-Za-z]:[/\\]|\\\\)')
PRIVATE_DEPENDENCIES = '''# Resolve private libraries from the consumer's imported dependency targets.
get_target_property(CURL_LIBRARIES_PRIVATE CURL::libcurl INTERFACE_LINK_LIBRARIES)
if(NOT CURL_LIBRARIES_PRIVATE)
  set(CURL_LIBRARIES_PRIVATE "")
endif()'''


@dataclass(frozen=True)
class _Assignment:
    start: int
    end: int
    value: str


def _private_assignments(text):
    """Locate complete commands; quoted/bracket path parentheses are data."""
    result = []
    consumed = 0
    for match in _PRIVATE_SET.finditer(text):
        if match.start() < consumed:
            continue
        cursor, depth, quoted = match.end(), 1, False
        while cursor < len(text):
            char = text[cursor]
            if char == '\\':
                cursor += 2
                continue
            if char == '"':
                quoted = not quoted
                cursor += 1
                continue
            if not quoted:
                bracket = _BRACKET.match(text, cursor + (char == '#'))
                if bracket:
                    end = text.find(']' + bracket[1] + ']', bracket.end())
                    if end < 0:
                        raise ValueError('Unterminated bracket argument in CURL private export')
                    cursor = end + len(bracket[1]) + 2
                    continue
                if char == '#':
                    end = text.find('\n', cursor)
                    cursor = len(text) if end < 0 else end + 1
                    continue
                if char == '(':
                    depth += 1
                elif char == ')':
                    depth -= 1
                    if not depth:
                        result.append(_Assignment(match.start(), cursor + 1, text[match.end():cursor]))
                        consumed = cursor + 1
                        break
            cursor += 1
        else:
            raise ValueError('Unterminated CURL private dependency assignment')
    return result


def verify_curl_export_text(text: str, context: str = '') -> None:
    """Reject literal producer/system library paths in the legacy link list.

    OpenSSL remains an explicitly external target SDK dependency. This check
    never invents libraries in the VeroScript SDK or resolves host OpenSSL.
    """
    normalized = text.replace('\r\n', '\n')
    if '# Resolve private libraries from the consumer\'s imported dependency targets.' in normalized:
        if normalized.count(PRIVATE_DEPENDENCIES) != 1:
            raise ValueError('Invalid normalized CURL private dependency export: ' + context)
    for match in _private_assignments(text):
        if _ABSOLUTE.search(match.value):
            raise ValueError('Non-relocatable CURL_LIBRARIES_PRIVATE export: ' + context)


def normalize_curl_export_text(text: str, context: str = '') -> str:
    assignments = _private_assignments(text)
    unsafe = [match for match in assignments if _ABSOLUTE.search(match.value)]
    if not unsafe:
        verify_curl_export_text(text, context)
        return text
    if len(assignments) != 1:
        raise ValueError('Ambiguous CURL private dependency assignments: ' + context)
    if not re.search(r'(?m)^[ \t]*set\([ \t]*CURL_LIBRARIES[ \t]+CURL::libcurl[ \t]*\)', text):
        raise ValueError('CURL config lacks its selected imported target: ' + context)
    newline = '\r\n' if '\r\n' in text else '\n'
    replacement = PRIVATE_DEPENDENCIES.replace('\n', newline)
    match = unsafe[0]
    result = text[:match.start] + replacement + text[match.end:]
    verify_curl_export_text(result, context)
    return result


def normalize_curl_exports(root: Path) -> list[str]:
    """Normalize staged configs before the caller seals file inventories."""
    root = Path(root)
    changed = []
    for path in sorted(root.rglob('*.cmake')):
        if path.name.casefold() not in _CONFIG_NAMES:
            continue
        if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
            raise ValueError('Linked CURL config input: ' + str(path))
        data = path.read_bytes()
        text = data.decode('utf-8')
        updated = normalize_curl_export_text(text, str(path))
        if updated != text:
            path.write_bytes(updated.encode('utf-8'))
            changed.append(path.relative_to(root).as_posix())
    return changed


def verify_curl_exports(root: Path) -> None:
    for path in sorted(Path(root).rglob('*.cmake')):
        if path.name.casefold() in _CONFIG_NAMES:
            verify_curl_export_text(path.read_text(encoding='utf-8-sig'), str(path))


def verify_curl_export_entries(entries, prefix: str = '') -> None:
    """Validate existing archive-entry mappings without extracting SDKs."""
    for name, entry in entries.items():
        if PurePosixPath(name).name.casefold() not in _CONFIG_NAMES:
            continue
        data = entry[0] if isinstance(entry, tuple) else entry
        if not isinstance(data, bytes) or not data:
            raise ValueError('CURL config content was not provided for validation: ' + prefix + name)
        verify_curl_export_text(data.decode('utf-8-sig'), prefix + name)
