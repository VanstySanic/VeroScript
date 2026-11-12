# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Module-owned documentation, integrated collections and offline User assembly.

Only the Python standard library is required (Python 3.11+).
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import html
import unicodedata
from collections import deque
import json
import os
from pathlib import Path, PurePosixPath
from veroscript_paths import module_root, repository_root, module_route, source_route
import re
import string
import stat
import zipfile
import shutil
import tempfile
import time
from urllib.parse import quote, unquote, urlsplit

CATALOG = '$module/Resources/Docs/documentation.json'
DOCS_ROOT = '$module/Resources/Docs'
INTEGRATION_MARKER = '.documentation.json'
OVERVIEW = 'Overview'
STANDARDS = 'Standards'
TUTORIALS = 'Tutorials'
DOC_TEXT_SUFFIXES = {'.md', '.json', '.vs', '.svg', '.txt', '.html', '.css', '.js', '.ts', '.xml', '.yaml', '.yml'}


def read_text(path):
	return path.read_bytes().decode('utf-8-sig')


def require_current_documentation(path):
	"""Keep archived records out of source collections and publication inputs."""
	if any(part.casefold() == 'history' for part in Path(path).parts):
		raise ValueError(f'History documentation is not publishable; use current guides and version control: {path}')


def load_settings(repo):
	"""Expand module-relative catalog routes without tying them to a folder name."""
	settings = json.loads(read_text(module_root(repo) / 'Resources/Docs/documentation.json'))
	if 'integrationRoot' in settings:
		raise ValueError('integrationRoot is obsolete; collections are assembled temporarily at publication')
	if settings.get('standardsRoot') and settings.get('standardsAudience', 'Developer') != 'Developer':
		raise ValueError('Standards are project development and contribution standards; standardsAudience must be Developer')
	optional = set(settings.get('optionalModules', []))
	if not optional.issubset(settings['modules']):
		raise ValueError('Optional documentation modules must be registered modules')
	modules = []
	for route in settings['modules']:
		expanded = source_route(repo, route)
		path = repo / safe_route(expanded)
		# An absent optional component is valid. An existing component with a
		# missing manifest is damaged and must still fail the normal audit.
		if route in optional and not path.exists() and not path.parents[2].exists():
			continue
		modules.append(expanded)
	settings['modules'] = modules
	for item in settings.get('resources', []):
		item['source'] = source_route(repo, item['source'])
	entrypoints = []
	for route in settings.get('entrypoints', []):
		if route.startswith('$valence/'):
			project = valence_project_root(repo)
			if project is None:
				continue  # Standalone VeroScript sources have no Valence navigation.
			route = (project.relative_to(repo.resolve()) / route[len('$valence/'):]).as_posix()
		entrypoints.append(source_route(repo, route))
	settings['entrypoints'] = entrypoints
	for key in ('overviewRoot', 'standardsRoot', 'tutorialsRoot'):
		if settings.get(key):
			settings[key] = source_route(repo, settings[key])
	return settings


def publication_bytes(path, data):
	"""Normalize UTF-8 documentation text; preserve binary assets and originals."""
	if path.suffix.lower() in DOC_TEXT_SUFFIXES:
		try:
			data.decode('utf-8')
		except UnicodeDecodeError:
			return data
		return data.replace(b'\r\n', b'\n').replace(b'\r', b'\n')
	return data


def _erase(text):
	return re.sub(r'[^\r\n]', ' ', text)


def _escaped(text, index):
	count = 0
	while index > 0 and text[index - 1] == '\\':
		count += 1
		index -= 1
	return bool(count % 2)


def prose_mask(text, *, inline=True):
	"""Keep source offsets, excluding code/comments but retaining list prose.

    Recognize quote/list containers before deciding whether indentation is code.
    This is a source-link scanner, not a Markdown-to-HTML renderer.
    """
	result, fence, list_levels = [], None, []
	paragraph = False
	for line in text.splitlines(keepends=True):
		quote_prefix = re.match(r'^(?: {0,3}>[ \t]?)*', line).end()
		body = line[quote_prefix:]
		whitespace = re.match(r'[ \t]*', body)[0]
		indent = len(whitespace.expandtabs(4))
		marker = re.match(r'([ \t]*)(?:[-+*]|\d{1,9}[.)])[ \t]+', body)
		base = list_levels[-1][1] if list_levels else 0
		is_list = marker is not None and indent < base + 4
		if is_list and fence is None:
			while list_levels and indent < list_levels[-1][1]:
				list_levels.pop()
			base = len(marker[0].expandtabs(4))
			list_levels.append((indent, base))
			prefix = quote_prefix + marker.end()
			content = body[marker.end():]
		else:
			if body.strip() and fence is None:
				while list_levels and indent < list_levels[-1][1]:
					list_levels.pop()
			base = list_levels[-1][1] if list_levels else 0
			# Remove only the containing list's indentation (preserve offsets).
			removed, columns = 0, 0
			while removed < len(whitespace) and columns < base:
				columns += 4 - columns % 4 if whitespace[removed] == '\t' else 1
				removed += 1
			prefix = quote_prefix + removed
			content = body[removed:]
		code_marker = re.match(r'^ {0,3}(`{3,}|~{3,})', content)
		hidden = fence is not None or bool(code_marker)
		if code_marker:
			token = code_marker[1]
			if fence is None:
				fence = token
			elif token[0] == fence[0] and len(token) >= len(fence) and not content[code_marker.end():].strip():
				fence = None
		if not hidden and not is_list and indent >= base + 4 and not paragraph:
			hidden = True
		result.append(_erase(line) if hidden else _erase(line[:prefix]) + line[prefix:])
		if not body.strip() or hidden:
			paragraph = False
		else:
			paragraph = not bool(re.match(r'^ {0,3}(?:#{1,6}\s|[-=*]{3,}\s*$)', content))
	masked = ''.join(result)
	# Process code spans/comments in source order so one cannot consume another.
	tokens = re.compile(r'<!--|`+')
	index = 0
	while match := tokens.search(masked, index):
		if match[0] == '<!--':
			close = masked.find('-->', match.end())
			end = len(masked) if close < 0 else close + 3
		elif inline and not _escaped(masked, match.start()):
			close = re.search(r'(?<!`)' + re.escape(match[0]) + r'(?!`)', masked[match.end():])
			if close is None:
				index = match.end()
				continue
			end = match.end() + close.end()
		else:
			index = match.end()
			continue
		masked = masked[:match.start()] + _erase(masked[match.start():end]) + masked[end:]
		index = end
	return masked


def _destination(text, start):
	if start >= len(text):
		return None
	if text[start] == '<':
		end = start + 1
		while end < len(text) and text[end] not in '\r\n':
			if text[end] == '>' and not _escaped(text, end):
				return start + 1, end, end + 1
			end += 1
		return None
	end, depth = start, 0
	while end < len(text):
		c = text[end]
		if c == '\\' and end + 1 < len(text):
			end += 2
			continue
		if c == '(':
			depth += 1
		elif c == ')':
			if depth == 0:
				break
			depth -= 1
		if c.isspace():
			break
		end += 1
	return (start, end, end) if depth == 0 else None


def links(text):
	"""URL spans of inline/image/reference/HTML links, excluding titles/code."""
	mask = prose_mask(text)
	spans = set()
	# Definitions carry the destination of full/collapsed/shortcut references.
	for match in list(re.finditer(r'^[ \t]*\[[^\]\n]+\]:[ \t]*(?:\r?\n[ \t]*)?', mask, re.M)):
		destination = _destination(mask, match.end())
		if destination:
			start, end, consumed = destination
			spans.add((start, end))
			line_end = mask.find('\n', consumed)
			line_end = len(mask) if line_end < 0 else line_end
			mask = mask[:match.start()] + _erase(mask[match.start():line_end]) + mask[line_end:]
	index = 0
	while index < len(mask):
		opening = mask.find('[', index)
		if opening < 0:
			break
		if _escaped(mask, opening):
			index = opening + 1
			continue
		closing, depth = opening + 1, 1
		while closing < len(mask) and depth:
			if not _escaped(mask, closing):
				if mask[closing] == '[':
					depth += 1
				elif mask[closing] == ']':
					depth -= 1
			closing += 1
		if depth or mask[closing:closing + 1] != '(':
			index = opening + 1
			continue
		start = closing + 1
		while start < len(mask) and mask[start].isspace():
			start += 1
		destination = _destination(mask, start)
		if destination is None:
			index = opening + 1
			continue
		start, end, tail = destination
		while tail < len(mask) and mask[tail].isspace():
			tail += 1
		if tail < len(mask) and mask[tail] in '\"\x27(' and tail > destination[2]:
			delimiter = ')' if mask[tail] == '(' else mask[tail]
			tail += 1
			while tail < len(mask) and (mask[tail] != delimiter or _escaped(mask, tail)):
				tail += 1
			tail += 1
			while tail < len(mask) and mask[tail].isspace():
				tail += 1
		if tail < len(mask) and mask[tail] == ')':
			spans.add((start, end))
			# Mask the whole link, including its optional title, before scanning on.
			mask = mask[:opening] + _erase(mask[opening:tail + 1]) + mask[tail + 1:]
			index = tail + 1
		else:
			index = opening + 1
	for tag in re.finditer(r'<[a-zA-Z][^>]*>', mask):
		for match in re.finditer(r'\b(?:href|src)\s*=\s*[\"\x27]([^\"\x27]+)[\"\x27]', tag[0], re.I):
			spans.add((tag.start() + match.start(1), tag.start() + match.end(1)))
	for start, end in sorted(spans):
		yield start, end, text[start:end]


def url_parts(url):
	return urlsplit(html.unescape(re.sub(r'\\([' + re.escape(string.punctuation) + '])', r'\1', url)))


def resolve_link(source, url):
	parts = url_parts(url)
	if parts.scheme or parts.netloc or parts.path.startswith('/'):
		return None
	target = (source.parent / unquote(parts.path)).resolve() if parts.path else source.resolve()
	return target, parts.query, unquote(parts.fragment)


def rewrite_links(text, callback):
	for start, end, url in reversed(list(links(text))):
		new = callback(url)
		if new is not None:
			text = text[:start] + new + text[end:]
	return text


def relative_url(source, target, query='', fragment=''):
	relative = os.path.relpath(target, source.parent).replace('\\', '/')
	return quote(relative, safe='/._-~') + ('?' + query if query else '') + ('#' + quote(fragment, safe='-_') if fragment else '')


def anchors(text):
	result, counts = set(), {}
	block = prose_mask(text, inline=False)
	headings = []
	for match in re.finditer(r'^[ \t]*#{1,6}[ \t]+([^\r\n]*)(?:\r?$)', block, re.M):
		headings.append((match.start(), re.sub(r'[ \t]+#+[ \t]*$', '', match[1])))
	for match in re.finditer(r'^[ \t]*([^\r\n]+)\r?\n[ \t]*(?:=+|-+)[ \t]*\r?$', block, re.M):
		heading = match[1].strip()
		if heading and not re.match(r'^[#|>]', heading):
			headings.append((match.start(), heading))
	for _, heading in sorted(headings):
		# Code-span HTML characters are literal heading text, not HTML tags.
		heading = re.sub(r'(`+)(.*?)(?<!`)\1(?!`)', lambda m: html.escape(m[2]), heading)
		heading = re.sub(r'!?\[([^\]]+)\]\([^)]*\)', r'\1', heading)
		heading = html.unescape(re.sub(r'<[^>]*>', '', heading)).lower()
		slug = ''.join(c for c in heading if unicodedata.category(c)[0] in 'LMN' or c in '_- ').replace(' ', '-')
		original = slug
		while slug in counts:
			counts[original] += 1
			slug = original + '-' + str(counts[original])
		counts[slug] = 0
		result.add(slug)
	for match in re.finditer(r'<[a-zA-Z][^>]*>', block):
		for identifier in re.finditer(r'\b(?:id|name)=[\"\x27]([^\"\x27]+)[\"\x27]', match[0]):
			result.add(html.unescape(identifier[1]))
	return result


def check_links(files, *, boundary=None):
	errors, cached = [], {}
	for source in files:
		if source.suffix.lower() != '.md':
			continue
		for _, _, url in links(read_text(source)):
			resolved = resolve_link(source, url)
			if resolved is None:
				parsed = url_parts(url)
				if boundary is not None and not parsed.scheme and not parsed.netloc and parsed.path.startswith('/'):
					errors.append(f'{source}: root-relative offline link {url}')
				continue
			target, _, fragment = resolved
			if boundary is not None and not target.is_relative_to(boundary.resolve()):
				errors.append(f'{source}: link escapes offline documentation: {url}')
				continue
			if not target.exists():
				errors.append(f'{source}: missing {url}')
			elif fragment and target.suffix.lower() == '.md':
				if target not in cached:
					cached[target] = anchors(read_text(target))
				if fragment not in cached[target]:
					errors.append(f'{source}: missing anchor {url}')
	return errors


def linked_path(path):
	info = path.lstat()
	return stat.S_ISLNK(info.st_mode) or bool(getattr(info, 'st_file_attributes', 0) & 0x400)


def reject_linked_ancestors(path, stop=None):
	for part in [path, *path.parents]:
		if part == stop:
			break
		if part.exists() or part.is_symlink():
			if linked_path(part):
				raise ValueError(f'Linked documentation path: {part}')


def check_tree(root, *, validate_links=True):
	"""Validate an expanded offline tree, including custom SourceRoot packages."""
	root = Path(os.path.abspath(root))
	require_current_documentation(Path(root.name))
	# Read-only traversal permits ancestor aliases (including OS temp paths).
	# Reject a linked root and every entry below it; writers guard their output
	# ancestors separately before creating or replacing files.
	if root.exists() or root.is_symlink():
		if linked_path(root):
			raise ValueError(f'Linked documentation directory: {root}')
	if not root.is_dir():
		raise ValueError(f'Missing documentation directory: {root}')
	files, pending = [], [root]
	while pending:
		for path in pending.pop().iterdir():
			require_current_documentation(path.relative_to(root))
			if linked_path(path):
				raise ValueError(f'Linked documentation entry: {path}')
			if path.is_dir():
				pending.append(path)
			elif path.is_file():
				files.append(path)
			else:
				raise ValueError(f'Unsupported documentation entry: {path}')
	if validate_links and not files:
		raise ValueError(f'Empty documentation directory: {root}')
	errors = check_links(files, boundary=root) if validate_links else []
	if errors:
		raise ValueError('\n'.join(errors))
	return files


def check_repository_links(files, repo, settings):
	"""Validate our own online source routes against the checkout, before upload."""
	errors = []
	base = settings.get('repositoryBaseUrl', '').rstrip('/')
	if not base:
		return errors
	prefixes = [base + '/' + kind + '/' + quote(settings['repositoryRef'], safe='') + '/' for kind in ('blob', 'tree')]

	def current_document(path):
		if not path.resolve().is_relative_to(repo.resolve()):
			return False
		parts = tuple(part.casefold() for part in path.resolve().relative_to(repo.resolve()).parts)
		return any(parts[index:index + 2] == ('resources', 'docs')
			and parts[index + 2] in {'user', 'developer', 'overview', 'tutorials', 'standards'}
			for index in range(len(parts) - 2))
	for source in files:
		if source.suffix.lower() != '.md':
			continue
		for _, _, url in links(read_text(source)):
			prefix = next((p for p in prefixes if url.startswith(p)), None)
			if prefix is None:
				continue
			parsed = urlsplit(url[len(prefix):])
			target = (repo / safe_route(source_route(repo, unquote(parsed.path)))).resolve()
			if current_document(source) and current_document(target):
				errors.append(f'{source}: use a relative link to current documentation: {url}')
			if not target.exists():
				errors.append(f'{source}: missing repository route {url}')
			elif parsed.fragment and target.suffix.lower() == '.md' and unquote(parsed.fragment) not in anchors(read_text(target)):
				errors.append(f'{source}: missing repository anchor {url}')
	return errors



def check_localized_prose(files, locales):
	"""Find substantial English prose copied unchanged into a translated page.

    This catches missing translation work, not translation quality. Code,
    inline API identifiers are excluded.
    """
	def paragraphs(path):
		visible = prose_mask(read_text(path))
		result = set()
		for block in re.split(r'\n\s*\n', visible):
			# Generated module/language navigation retains stable identity labels.
			without_links = re.sub(r'\[[^\]\n]*\]\([^\n)]*\)', '', block)
			if not re.search(r'[^\W\d_]', without_links, re.UNICODE):
				continue
			# Link destinations are paths/protocol tokens, not natural prose.
			block = re.sub(r'\]\([^\n)]*\)', ']', block)
			normalized = ' '.join(block.split())
			if (len(re.findall(r'[A-Za-z]', normalized)) >= 120
					and len(re.findall(r"[A-Za-z]+(?:['’-][A-Za-z]+)*", normalized)) >= 16):
				result.add(normalized)
		return result

	errors = []
	sources = {Path(source).resolve() for source in files if Path(source).suffix.lower() == '.md'}
	for source in sorted(sources):
		if 'en-US' not in source.parts:
			continue
		index = source.parts.index('en-US')
		english = paragraphs(source)
		if not english:
			continue
		for locale in locales:
			if locale == 'en-US':
				continue
			translated = Path(*source.parts[:index], locale, *source.parts[index + 1:])
			if translated not in sources or not translated.is_file():
				continue
			for paragraph in sorted(english & paragraphs(translated)):
				errors.append(f'{translated}: untranslated English prose: {paragraph[:100]}')
	return errors


def check_documentation_dates(files):
	"""Reject calendar dates in authored pages, without mistaking tool versions.

    Dotted versions such as Rider 2026.2.2 and ABI/product versions are valid.
    Review logs belong in temporary outputs, not permanent documentation.
    """
	months = (
		r'(?:Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?'
		r'|Sep(?:t(?:ember)?)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?'
		r'|janvier|janv|février|fevrier|févr|fevr|fév|fev|mars|avril|avr|mai|juin|juillet|juill?'
		r'|août|aout|septembre|sept|octobre|novembre|décembre|decembre|déc|dec)'
	)
	year = r'(?:19|20)\d{2}'
	month = r'(?:0?[1-9]|1[0-2])'
	day = r'(?:0?[1-9]|[12]\d|3[01])'
	word_start = r'(?:\b|(?<=_))'
	word_end = r'(?:\b|(?=_))'
	patterns = [
		rf'(?<!\d){year}[-_/]{month}[-_/]{day}(?!\d)',
		rf'(?<!\d){day}[-_/]{month}[-_/]{year}(?!\d)',
		rf'(?<!\d){year}[-_/]{month}(?!\d)',
		rf'(?<=[_-]){year}(?:0[1-9]|1[0-2])(?:0[1-9]|[12]\d|3[01])(?!\d)',
		rf'(?<!\d){year}\s*年\s*{month}\s*月(?:\s*{day}\s*日)?',
		rf'(?<!\d){year}\s*년\s*{month}\s*월(?:\s*{day}\s*일)?',
		rf'{word_start}{months}\.?[\s_-]+\d{{1,2}}(?:st|nd|rd|th)?,?[\s_-]+{year}{word_end}',
		rf'{word_start}\d{{1,2}}(?:st|nd|rd|th|er)?[\s_-]+{months}\.?[\s_-]+{year}{word_end}',
		rf'{word_start}{months}\.?[\s_-]+{year}{word_end}',
	]
	calendar = re.compile('|'.join(patterns), re.I)
	# Dated JSON Schema dialect identifiers are stable specification versions.
	schema_dialect = re.compile(r'\bdraft[ /-](?:2019-09|2020-12)\b(?![-_/]\d)', re.I)
	errors = []
	for source in dict.fromkeys(files):
		if source.suffix.lower() != '.md':
			continue
		if calendar.search(source.name):
			errors.append(f'{source}: calendar date in documentation filename')
		for number, line in enumerate(read_text(source).splitlines(), 1):
			if calendar.search(schema_dialect.sub('schema-dialect', line)):
				errors.append(f'{source}:{number}: calendar date in documentation; use a stable topic/version')
	return errors


def documentation_entrypoints(repo, settings):
	"""Find source entrypoints without scanning staged/ignored source snapshots."""
	result = []
	generated = {'.tmp', '.git', 'build', 'release', 'dist', 'node_modules', '__pycache__'}

	def expand(directory, parts):
		if not parts:
			if directory.is_file():
				yield directory
			return
		if not directory.is_dir():
			return
		segment, *remaining = parts
		if segment == '**':
			yield from expand(directory, remaining)
			for child in directory.iterdir():
				if child.name.casefold() not in generated and child.is_dir() and not linked_path(child):
					yield from expand(child, parts)
		else:
			for child in directory.glob(segment):
				if child.name.casefold() not in generated:
					yield from expand(child, remaining)

	for pattern in settings.get('entrypoints', []):
		result.extend(expand(repo, safe_route(pattern).parts))
	return list(dict.fromkeys(result))


def module_entrypoints(repo, settings, manifests):
	"""Validate source landing pages and localized module-guide selectors."""
	result = []
	for manifest, definition in manifests:
		hub = manifest.parent / 'README.md'
		landing = manifest.parent.parent.parent / 'README.md'
		for source in (landing, hub):
			reject_linked_ancestors(source, repo)
			if not source.is_file() or not read_text(source).strip():
				raise ValueError(f'Missing/empty module README entrypoint: {source}')
			result.append(source)
		landing_targets = {target[0] for _, _, url in links(read_text(landing))
						   if (target := resolve_link(landing, url)) is not None}
		base = settings.get('repositoryBaseUrl', '').rstrip('/')
		online_hub = (base + '/blob/' + quote(settings['repositoryRef'], safe='') + '/' +
			quote(hub.resolve().relative_to(repo.resolve()).as_posix(), safe='/._-~')) if base else None
		online_links = {url for _, _, url in links(read_text(landing))}
		if hub.resolve() not in landing_targets and (online_hub is None or online_hub not in online_links):
			raise ValueError(f'Missing documentation index link in module README: {landing}')
		hub_targets = {target[0] for _, _, url in links(read_text(hub))
					   if (target := resolve_link(hub, url)) is not None}
		roles = ('User', 'Developer') if definition.get('developer') else ('User',)
		for role in roles:
			for locale in settings['locales']:
				expected = (manifest.parent / role / locale / 'index.md').resolve()
				if expected not in hub_targets:
					raise ValueError(f'Missing {role}/{locale} documentation entry in module README: {hub}')
	return list(dict.fromkeys(result))


def safe_route(value):
	path = PurePosixPath(value)
	if not value or path.is_absolute() or '..' in path.parts or '\\' in value or ':' in value:
		raise ValueError(f'Unsafe documentation path: {value}')
	return path


def catalog(repo):
	repo = repo.resolve()
	settings = load_settings(repo)
	if settings['schemaVersion'] != 1:
		raise ValueError('Unsupported documentation catalog schema')
	locales = settings['locales']
	if not locales or len(locales) != len(set(locales)):
		raise ValueError('Empty/duplicate documentation locales')
	user, developer, manifests = {}, [], []
	seen_modules = set()
	for entry in settings['modules']:
		path = repo / safe_route(entry)
		reject_linked_ancestors(path, repo)
		manifest = json.loads(read_text(path))
		if manifest['schemaVersion'] != 1:
			raise ValueError(f'Unsupported module manifest: {path}')
		if manifest['module'] in seen_modules:
			raise ValueError(f'Duplicate module: {manifest["module"]}')
		seen_modules.add(manifest['module'])
		if not path.resolve().is_relative_to(repo) or path.is_symlink():
			raise ValueError(f'Linked/escaping module manifest: {path}')
		manifests.append((path, manifest))
		for rule in manifest['user']:
			inventories = []
			for locale in locales:
				source = path.parent / safe_route(rule['source'].replace('{locale}', locale))
				output = safe_route(rule['output'].replace('{locale}', locale))
				if not source.is_relative_to(path.parent / 'User' / locale):
					raise ValueError(f'User manifest may only include its owning locale tree: {source}')
				reject_linked_ancestors(source, repo)
				if not source.exists() or source.is_symlink():
					raise ValueError(f'Missing/linked User documentation: {source}')
				files = [source] if source.is_file() else sorted(check_tree(source, validate_links=False))
				inventory = set()
				for file in files:
					reject_linked_ancestors(file, repo)
					if not file.is_file():
						continue
					if file.is_symlink() or not file.resolve().is_relative_to(path.parent.resolve()):
						raise ValueError(f'Linked/escaping documentation: {file}')
					suffix = Path() if source.is_file() else file.relative_to(source)
					destination = str(output / suffix)
					if file.resolve() in user or destination.casefold() in {v.casefold() for v in user.values()}:
						raise ValueError(f'Duplicate documentation source/output: {file} -> {destination}')
					if file.suffix.lower() == '.md' and not read_text(file).strip():
						raise ValueError(f'Empty documentation page: {file}')
					user[file.resolve()] = destination
					inventory.add(suffix.as_posix())
				inventories.append(inventory)
			if any(pages != inventories[0] for pages in inventories[1:]):
				raise ValueError(f'User locale page parity mismatch: {path} / {rule}')
		actual_user = {p.resolve() for p in check_tree(path.parent / 'User', validate_links=False)}
		declared_user = {p for p in user if p.is_relative_to((path.parent / 'User').resolve())}
		if actual_user != declared_user:
			raise ValueError(f'Unlisted User documentation or locale in manifest: {path}')
		if manifest.get('developer'):
			trees = []
			for locale in locales:
				root = path.parent / 'Developer' / locale
				reject_linked_ancestors(root, repo)
				if not (root / 'index.md').is_file():
					raise ValueError(f'Missing Developer locale index: {root}')
				files = [f for f in check_tree(root, validate_links=False) if f.suffix.lower() == '.md']
				for file in files:
					reject_linked_ancestors(file, repo)
				if any(f.is_symlink() or not f.resolve().is_relative_to(repo) or not read_text(f).strip() for f in files):
					raise ValueError(f'Linked/empty/escaping Developer page: {root}')
				trees.append({f.relative_to(root).as_posix() for f in files})
				developer.extend(files)
			if any(tree != trees[0] for tree in trees[1:]):
				raise ValueError(f'Developer locale page parity mismatch: {path}')
	return settings, user, developer, manifests


def user_mapping(repo):
	"""Resolve portable User sources at stable published routes.

    Linked examples/assets/tutorial pages are a dependency closure. Developer
    documents, project standards and implementation files are forbidden in this public bundle.
    """
	repo = repo.resolve()
	settings, mapping, _, _ = catalog(repo)
	standards_root = (repo / safe_route(settings['standardsRoot'])).resolve() if settings.get('standardsRoot') else None
	if settings.get('tutorialsRoot'):
		_, tutorials = localized_bundle(repo, settings['tutorialsRoot'], TUTORIALS, settings['locales'], 'index.md')
		for source, output in tutorials.items():
			if source in mapping or output.casefold() in {v.casefold() for v in mapping.values()}:
				raise ValueError(f'Duplicate tutorial output: {source}')
			mapping[source] = output
	support = [(repo / safe_route(item['source']), item['output']) for item in settings['resources']]
	for root, _ in support:
		reject_linked_ancestors(root, repo)
		if not root.resolve().is_relative_to(repo) or root.is_symlink():
			raise ValueError(f'Linked/escaping documentation resource root: {root}')
	queue = deque(mapping)
	while queue:
		source = queue.popleft()
		if source.suffix.lower() != '.md':
			continue
		for _, _, url in links(read_text(source)):
			parsed = url_parts(url)
			if not parsed.scheme and not parsed.netloc and parsed.path.startswith('/'):
				raise ValueError(f'Root-relative User link cannot be read offline: {source}: {url}')
			resolved = resolve_link(source, url)
			if resolved is None:
				continue
			target, _, _ = resolved
			if standards_root is not None and target.is_relative_to(standards_root):
				raise ValueError(f'User link targets project development and contribution standards: {source}: {url}')
			reject_linked_ancestors(source.parent / unquote(parsed.path), repo)
			if not target.is_file():
				raise ValueError(f'Offline document has missing/directory link: {source}: {url}')
			if target in mapping:
				continue
			for root, output in support:
				if target.is_relative_to(root.resolve()):
					if any(p in {'.verocache', '.veroscript', '__pycache__'} for p in target.parts) or target.suffix in {'.vsc', '.vsbc', '.pyc'}:
						raise ValueError(f'Generated documentation dependency: {target}')
					destination = str(safe_route(output) / target.relative_to(root.resolve()))
					if destination.casefold() in {v.casefold() for v in mapping.values()}:
						raise ValueError(f'Duplicate resource output: {destination}')
					mapping[target] = destination
					queue.append(target)
					break
			else:
				raise ValueError(f'User link targets source-only content: {source}: {url}')
	errors = check_links(mapping)
	if errors:
		raise ValueError('\n'.join(errors))
	routes = {value.casefold() for value in mapping.values()}
	if 'readme.md' in routes:
		raise ValueError('README.md is reserved for the generated User language selector')
	routes.add('readme.md')
	for name in mapping.values():
		for parent in PurePosixPath(name).parents:
			if parent.as_posix().casefold() in routes:
				raise ValueError(f'Documentation output file/directory conflict: {name}')
	for source, output in mapping.items():
		require_current_documentation(source.relative_to(repo))
		require_current_documentation(output)
	for locale in settings['locales']:
		if locale + '/index.md' not in mapping.values():
			raise ValueError(f'Missing published User locale entrypoint: {locale}/index.md')
	return settings, mapping


def source_repository_navigation(settings, locale=None):
	"""Render the configured source link on generated documentation entry pages."""
	repository = settings.get('repositoryBaseUrl')
	if not repository:
		return ''
	labels = {'en-US': 'Source code on GitHub', 'fr-FR': 'Code source sur GitHub',
			  'ja-JP': 'GitHub のソースコード', 'ko-KR': 'GitHub 소스 코드',
			  'zh-CN': 'GitHub 源码仓库', 'zh-TW': 'GitHub 原始碼儲存庫'}
	return '[' + labels.get(locale, 'Source code / 源码') + '](' + repository.rstrip('/') + ')\n\n'


def user_bundle(repo):
	"""Return portable, self-contained User bytes at stable published routes."""
	settings, mapping = user_mapping(repo)
	result = {}
	index = '# VeroScript\n\n' + source_repository_navigation(settings) + '\n'.join('- [' + locale + '](' + locale + '/index.md)' for locale in settings['locales']) + '\n'
	if settings.get('tutorialsRoot'):
		index += '\n[Tutorials / 教程](Tutorials/README.md)\n'
	index = re.sub(r'(?<![A-Za-z])Script/', module_route(repo) + '/', index)
	result['README.md'] = index.encode('utf-8')
	for source, output in mapping.items():
		data = source.read_bytes()
		if source.suffix.lower() == '.md':
			text = data.decode('utf-8')
			def convert(url):
				resolved = resolve_link(source, url)
				if resolved is None:
					return None
				target, query, fragment = resolved
				return relative_url(Path(output), Path(mapping[target]), query, fragment)
			data = rewrite_links(text, convert).encode('utf-8')
		result[output] = publication_bytes(source, data)
	return result


def overview_bundle(repo, locales):
	"""Read the editable cross-module overview inside the integrated Docs root."""
	settings = load_settings(repo)
	route = settings.get('overviewRoot', source_route(repo, DOCS_ROOT + '/' + OVERVIEW))
	if route != source_route(repo, DOCS_ROOT + '/' + OVERVIEW):
		raise ValueError(f'Integrated overview may only use {DOCS_ROOT}/{OVERVIEW}')
	root = repo / route
	reject_linked_ancestors(root, repo)
	files = check_tree(root, validate_links=False)
	inventories = {locale: set() for locale in locales}
	result, sources = {}, {}
	for file in files:
		relative = file.relative_to(root)
		if len(relative.parts) < 2 or relative.parts[0] not in inventories:
			raise ValueError(f'Unlisted overview locale/path: {file}')
		if file.suffix.lower() == '.md' and not read_text(file).strip():
			raise ValueError(f'Empty overview page: {file}')
		inventories[relative.parts[0]].add(Path(*relative.parts[1:]).as_posix())
		output = OVERVIEW + '/' + relative.as_posix()
		result[output] = publication_bytes(file, file.read_bytes())
		sources[file.resolve()] = output
	if any('index.md' not in pages for pages in inventories.values()):
		raise ValueError('Missing overview locale index')
	if any(pages != inventories[locales[0]] for pages in inventories.values()):
		raise ValueError('Overview locale page parity mismatch')
	return result, sources


def localized_bundle(repo, route, output, locales, entrypoint):
	"""Validate a complete localized current-document tree and shared navigation."""
	root = repo / safe_route(route)
	reject_linked_ancestors(root, repo)
	files = check_tree(root, validate_links=False)
	inventories = {locale: set() for locale in locales}
	result, sources = {}, {}
	for file in files:
		relative = file.relative_to(root)
		if relative.parts[0] in inventories and len(relative.parts) >= 2:
			inventories[relative.parts[0]].add(Path(*relative.parts[1:]).as_posix())
		elif relative.as_posix() != 'README.md':
			raise ValueError(f'Unlisted {output} locale/path: {file}')
		if file.suffix.lower() == '.md' and not read_text(file).strip():
			raise ValueError(f'Empty {output} page: {file}')
		name = output + '/' + relative.as_posix()
		result[name] = publication_bytes(file, file.read_bytes())
		sources[file.resolve()] = name
	if not (root / 'README.md').is_file() or any(entrypoint not in pages for pages in inventories.values()):
		raise ValueError(f'Missing {output} locale entrypoint')
	if any(pages != inventories[locales[0]] for pages in inventories.values()):
		raise ValueError(f'{output} locale page parity mismatch')
	return result, sources


def collection_bundle(repo):
	"""Extract module User/Developer docs into one portable integrated tree."""
	repo = repo.resolve()
	settings, users = user_mapping(repo)
	_, _, _, manifests = catalog(repo)
	module_entrypoints(repo, settings, manifests)
	mapping = {source: output if output.startswith(TUTORIALS + '/') else 'User/' + output
			   for source, output in users.items()}
	routes = {output.casefold() for output in mapping.values()}
	def add(source, output):
		output = safe_route(output).as_posix()
		require_current_documentation(source.relative_to(repo))
		require_current_documentation(output)
		if source in mapping or output.casefold() in routes:
			raise ValueError(f'Duplicate integrated documentation: {source} -> {output}')
		mapping[source] = output
		routes.add(output.casefold())
	authored = {}
	if settings.get('standardsRoot'):
		if settings['standardsRoot'] != source_route(repo, DOCS_ROOT + '/' + STANDARDS):
			raise ValueError('Standards must be maintained in the integrated Docs/Standards root')
		authored, standards_sources = localized_bundle(repo, settings['standardsRoot'], STANDARDS, settings['locales'], 'README.md')
		for source, output in standards_sources.items():
			add(source, output)
	for manifest_path, manifest in manifests:
		module = safe_route(manifest['module']).as_posix()
		docs = manifest_path.parent
		hub = docs / 'README.md'
		reject_linked_ancestors(hub, repo)
		if not hub.is_file() or not read_text(hub).strip():
			raise ValueError(f'Missing/empty module documentation entrypoint: {hub}')
		add(hub.resolve(), f'_modules/{module}/README.md')
		if manifest.get('developer'):
			for locale in settings['locales']:
				source = docs / 'Developer' / locale
				for file in check_tree(source, validate_links=False):
					add(file.resolve(), f'Developer/{locale}/{module}/' + file.relative_to(source).as_posix())
	_, overview_sources = overview_bundle(repo, settings['locales'])
	for source, output in overview_sources.items():
		add(source, output)
	# User's existing dependencies keep their published locations. Dependencies
	# added by Developer or Overview pages live beside the audiences, outside User.
	support = [(repo / safe_route(item['source']), item['output']) for item in settings['resources']]
	pending = deque(mapping)
	while pending:
		source = pending.popleft()
		if source.suffix.lower() != '.md':
			continue
		for _, _, url in links(read_text(source)):
			resolved = resolve_link(source, url)
			if resolved is None or resolved[0] in mapping:
				continue
			target = resolved[0]
			for root, output in support:
				if target.is_file() and target.is_relative_to(root.resolve()):
					reject_linked_ancestors(target, repo)
					if any(p in {'.verocache', '.veroscript', '__pycache__'} for p in target.parts) or target.suffix in {'.vsc', '.vsbc', '.pyc'}:
						raise ValueError(f'Generated integrated documentation dependency: {target}')
					add(target, str(safe_route(output) / target.relative_to(root.resolve())))
					pending.append(target)
					break
	errors = check_links(mapping) + check_repository_links(mapping, repo, settings)
	if errors:
		raise ValueError('\n'.join(errors))
	selector = '# VeroScript\n\n' + source_repository_navigation(settings) + '\n'.join('- [' + locale + '](' + locale + '/index.md)' for locale in settings['locales']) + '\n'
	if settings.get('tutorialsRoot'):
		selector += '\n[Tutorials / 教程](../Tutorials/README.md)\n'
	result = {'User/README.md': selector.encode('utf-8')}
	for source, output in mapping.items():
		if output in result:
			continue
		data = source.read_bytes()
		if source.suffix.lower() == '.md':
			def convert(url):
				resolved = resolve_link(source, url)
				if resolved is None:
					parsed = url_parts(url)
					if not parsed.scheme and not parsed.netloc and parsed.path.startswith('/'):
						raise ValueError(f'Root-relative integrated documentation link: {source}: {url}')
					return None
				target, query, fragment = resolved
				if target == repo / source_route(repo, DOCS_ROOT) / 'README.md':
					return relative_url(Path(output), Path('README.md'), query, fragment)
				if target in mapping:
					return relative_url(Path(output), Path(mapping[target]), query, fragment)
				if target.is_dir():
					index = target / 'index.md'
					if index in mapping:
						return relative_url(Path(output), Path(mapping[index]), query, fragment)
				base = settings.get('repositoryBaseUrl')
				if not base or not target.is_relative_to(repo):
					raise ValueError(f'Integrated source link needs a repository URL: {source}: {url}')
				kind = 'tree' if target.is_dir() else 'blob'
				return base.rstrip('/') + '/' + kind + '/' + quote(settings['repositoryRef'], safe='') + '/' + quote(target.relative_to(repo).as_posix(), safe='/._-~') + ('?' + query if query else '') + ('#' + quote(fragment, safe='-_') if fragment else '')
			data = rewrite_links(data.decode('utf-8'), convert).encode('utf-8')
		result[output] = publication_bytes(source, data)
	titles = {'en-US': 'Developer documentation', 'fr-FR': 'Documentation développeur',
			  'ja-JP': '開発者文書', 'ko-KR': '개발자 문서', 'zh-CN': '开发文档', 'zh-TW': '開發文件'}
	language_labels = {'en-US': 'Language', 'fr-FR': 'Langue', 'ja-JP': '言語',
					   'ko-KR': '언어', 'zh-CN': '语言', 'zh-TW': '語言'}
	overview_labels = {'en-US': 'Integrated overview and reading paths', 'fr-FR': 'Vue d’ensemble et parcours de lecture',
					   'ja-JP': '総合概要と読む順序', 'ko-KR': '통합 개요와 읽기 순서',
					   'zh-CN': '综合总览与阅读路线', 'zh-TW': '綜合總覽與閱讀路線'}
	standards_labels = {'en-US': 'Project development and contribution standards',
					'fr-FR': 'Règles de développement et de contribution au projet',
					'ja-JP': '本プロジェクトの開発・貢献規約', 'ko-KR': '이 프로젝트의 개발 및 기여 규약',
					'zh-CN': '本项目开发与贡献规范', 'zh-TW': '本專案開發與貢獻規範'}
	for locale in settings['locales']:
		index = '# VeroScript — ' + titles.get(locale, 'Developer documentation') + '\n\n' + source_repository_navigation(settings, locale)
		index += '[' + overview_labels.get(locale, 'Integrated overview') + '](../../Overview/' + locale + '/index.md)\n\n'
		if settings.get('standardsRoot'):
			index += '[' + standards_labels.get(locale, 'Project development and contribution standards') + '](../../Standards/' + locale + '/README.md)\n\n'
		for _, manifest in manifests:
			if manifest.get('developer'):
				module = manifest['module']
				index += '- [' + module + '](' + quote(module, safe='/._-~') + '/index.md)\n'
		index += '\n' + language_labels.get(locale, 'Language') + ': ' + ' · '.join('[' + other + '](' + ('index.md' if other == locale else '../' + other + '/index.md') + ')' for other in settings['locales']) + '\n'
		result[f'Developer/{locale}/index.md'] = index.encode('utf-8')
	result['Developer/README.md'] = ('# VeroScript — Developer\n\n' + source_repository_navigation(settings) + '\n'.join('- [' + locale + '](' + locale + '/index.md)' for locale in settings['locales']) + '\n').encode('utf-8')
	docs_route = module_route(repo, 'Resources/Docs')
	tools_route = module_route(repo, 'Resources/Tools/Packaging')
	index = ('# VeroScript — Integrated documentation / 综合文档\n\n' + source_repository_navigation(settings) +
			 'Start with the overview for task-based reading paths, then follow the User references or Developer maintenance guides.\n\n'
			 '从综合总览选择使用场景和阅读路线，再进入用户参考或开发维护文档。\n\n'
			 '| Language / 语言 | Overview / 综合总览 | User / 用户参考 | Developer / 开发维护 |\n'
			 '| --- | --- | --- | --- |\n' + '\n'.join('| ' + locale + ' | [Overview](Overview/' + locale + '/index.md) | [User](User/' + locale + '/index.md) | [Developer](Developer/' + locale + '/index.md) |' for locale in settings['locales']) + '\n\n'
			 '## Maintenance / 维护\n\n'
			 f'Maintain shared User and Developer entry guides, Overview, Standards and Tutorial originals in `{docs_route}`. Maintain module details in their own `Resources/Docs`. Publication assembles and validates the current originals in `.tmp`, then exports the requested audience. No generated collection is stored beside the sources.\n\n'
			 f'在 `{docs_route}` 维护共享 User、Developer 入口、Overview、Standards 和 Tutorials 正本；模块细节维护在各模块自己的 `Resources/Docs`。出版时在 `.tmp` 临时集成并校验当前正本，再导出所选受众；源码目录不保存生成集合。\n\n'
			 f'```powershell\npython "{tools_route}/veroscript_docs.py" check\npython "{tools_route}/veroscript_docs.py" check-collection\n```\n\n'
			 'Run commands from the repository root / 从仓库根目录运行。\n\n'
			 'Generated UTF-8 documentation text uses LF for reproducible Windows/Linux checkouts; binary assets and module originals retain their bytes.\n\n'
			 '生成的 UTF-8 文档文本使用 LF，保证 Windows/Linux 检出一致；二进制资源和模块正本保持原始字节。\n\n'
			 '## Publication / 出版\n\n'
			 'Packages assemble and validate this integrated collection from current author originals. All includes Overview, User, Developer, Standards, complete Tutorials and dependencies. Standards apply only to development of and contributions to this project, are classified as Developer by standardsAudience, and add no license conditions. User includes user references, complete Tutorials and dependencies, without Standards or developer records. The separate Resources release carries the User publication; Runtime/SDK/extension archives and product installers retain functional payloads without Standards, authored documentation or examples. Source copyright and license notices remain intact. Each publication rebuilds the collection automatically.\n\n'
			 '文档包从当前正本临时集成并校验综合文档。All 包含 Overview、User、Developer、Standards、完整 Tutorials 及依赖；Standards 仅适用于本项目开发与贡献，由 standardsAudience 归为 Developer，不增加许可条件。User 包含用户参考、完整教程及依赖，不包含 Standards 与开发记录。独立 Resources 发行提供 User 出版视图；Runtime、SDK、扩展发行包和产品安装器保留功能载荷，不附带 Standards、编写的文档与示例。源码版权与许可声明继续保留。每次出版自动重新集成。\n\n'
			 f'```powershell\n& "./{tools_route}/package-veroscript-docs.ps1" -OutRoot .tmp/docs-collection-package\n& "./{tools_route}/package-veroscript-docs.ps1" -Audience User -OutRoot .tmp/docs-user-package\n```\n\n'
			 '## Modules / 模块\n\n' + '\n'.join('- [' + manifest['module'] + '](_modules/' + quote(manifest['module'], safe='/._-~') + '/README.md)' for _, manifest in manifests) + '\n\n'
			 f'User contains current user references. Developer retains current module maintenance guides; source-code references open the online repository. Historical plans and validation records belong in version control or temporary evidence and are not publication inputs. Tutorials are shared lessons; Standards are this project\'s development and contribution standards, included only in All for developers. Both are publication copies of originals maintained in {docs_route}.\n\n'
			 f'User 汇总现行用户参考；Developer 按模块汇总现行维护指南，源码引用跳转在线仓库。历史计划和验收记录由版本控制或临时证据保存，不属于出版输入。Tutorials 是共享教程；Standards 是本项目开发与贡献规范，仅在 All 中供开发者阅读。两者均为 {docs_route} 中正本的出版副本。\n')
	for section, key, entrypoint in ((STANDARDS, 'standardsRoot', 'README.md'), (TUTORIALS, 'tutorialsRoot', 'index.md')):
		if settings.get(key):
			title = 'Standards — Project development and contributions / 本项目开发与贡献' if section == STANDARDS else section
			index += '\n## ' + title + '\n\n' + '\n'.join('- [' + locale + '](' + section + '/' + locale + '/' + entrypoint + ')' for locale in settings['locales']) + '\n'
	result['README.md'] = index.encode('utf-8')
	routes = {name.casefold() for name in result}
	if len(routes) != len(result):
		raise ValueError('Duplicate integrated documentation output')
	for name in result:
		if any(parent.as_posix().casefold() in routes for parent in PurePosixPath(name).parents):
			raise ValueError(f'Integrated output file/directory conflict: {name}')
	marker = {'schemaVersion': 1, 'product': 'VeroScriptDocumentationCollection',
			  'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(result.items())},
			  'sources': {source.relative_to(repo).as_posix(): output for source, output in sorted(mapping.items())}}
	result[INTEGRATION_MARKER] = (json.dumps(marker, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8')
	return result


def authored_snapshot(repo):
	"""Track all author inputs, including unlinked localized pages and assets."""
	root = repo / source_route(repo, DOCS_ROOT)
	config_path = repo / source_route(repo, CATALOG)
	reject_linked_ancestors(config_path, repo)
	settings = load_settings(repo)
	result = {CATALOG: config_path.read_bytes()}
	roots = [root]
	roots.extend((repo / safe_route(route)).parent for route in settings['modules'])
	roots.extend((repo / safe_route(route)).parent.parent.parent / 'README.md' for route in settings['modules'])
	roots.extend(repo / safe_route(item['source']) for item in settings['resources'])
	for source in dict.fromkeys(roots):
		reject_linked_ancestors(source, repo)
		if not source.exists():
			result[source.relative_to(repo).as_posix()] = None
			continue
		for path in check_tree(source, validate_links=False) if source.is_dir() else [source]:
			result[path.relative_to(repo).as_posix()] = path.read_bytes()
	return result


def verified_collection(repo):
	"""Build and validate a fresh collection without modifying author originals."""
	repo = repo.resolve()
	snapshot = authored_snapshot(repo)
	bundle = collection_bundle(repo)
	scratch = repo / '.tmp'
	reject_linked_ancestors(scratch, repo)
	scratch.mkdir(exist_ok=True)
	work = Path(tempfile.mkdtemp(prefix='docs-collection-', dir=scratch))
	stage = work / 'Docs'
	try:
		for name, data in bundle.items():
			path = stage / safe_route(name)
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(data)
		check_tree(stage)
		settings = load_settings(repo)
		pages = list(stage.rglob('*.md'))
		errors = check_repository_links(pages, repo, settings)
		errors.extend(check_localized_prose(pages, settings['locales']))
		if settings.get('dateFreeDocumentation'):
			errors.extend(check_documentation_dates(pages))
		if errors:
			raise ValueError('\n'.join(errors))
		if authored_snapshot(repo) != snapshot:
			raise ValueError('Authored documentation changed during collection assembly; retry publication')
		return bundle
	finally:
		reject_linked_ancestors(work, repo)
		shutil.rmtree(work)


def check_collection(repo):
	return len(verified_collection(repo))


def sync_standards(repo):
	"""Refresh the IDE standards reference independently of publication."""
	repo = repo.resolve()
	settings = load_settings(repo)
	if not settings.get('standardsRoot'):
		return 0
	files, sources = localized_bundle(repo, settings['standardsRoot'], STANDARDS, settings['locales'], 'README.md')
	errors = check_links(sources) + check_repository_links(sources, repo, settings)
	if errors:
		raise ValueError('\n'.join(errors))
	sync_standards_reference(repo, {name.removeprefix('Standards/'): data for name, data in files.items()})
	verify_standards_reference(repo)
	return len(files)


def publication_bundle(repo, audience='All'):
	if audience not in {'All', 'User'}:
		raise ValueError(f'Unsupported documentation audience: {audience}')
	bundle = verified_collection(repo)
	if audience == 'All':
		return bundle
	return user_projection(bundle)


def user_projection(bundle):
	"""Project an intact All collection into one complete offline User tree."""
	routes = {name: name.removeprefix('User/') for name in bundle
			  if name.startswith(('User/', TUTORIALS + '/'))}
	outputs = {name.casefold() for name in routes.values()}
	if len(outputs) != len(routes):
		raise ValueError('Duplicate User publication output')
	for output in routes.values():
		if any(parent.as_posix().casefold() in outputs for parent in PurePosixPath(output).parents):
			raise ValueError(f'User output file/directory conflict: {output}')
	result = {}
	for name, output in routes.items():
		data = bundle[name]
		if name.endswith('.md'):
			def convert(url):
				parts = url_parts(url)
				if parts.scheme or parts.netloc:
					return None
				if parts.path.startswith('/'):
					raise ValueError(f'Root-relative User publication link: {name}: {url}')
				target = os.path.normpath(str(PurePosixPath(name).parent / unquote(parts.path))).replace('\\', '/') if parts.path else name
				if target not in routes:
					raise ValueError(f'User publication links outside its audience: {name}: {url}')
				return relative_url(Path(output), Path(routes[target]), parts.query, unquote(parts.fragment))
			data = rewrite_links(data.decode('utf-8'), convert).encode('utf-8')
		result[output] = data
	return result


def validate_collection_metadata(actual):
	"""Validate a complete exported All collection independently of its location."""
	for route in actual:
		require_current_documentation(route)
	marker = json.loads(actual[INTEGRATION_MARKER])
	if marker.get('schemaVersion') != 1 or marker.get('product') != 'VeroScriptDocumentationCollection':
		raise ValueError('Unrecognized documentation collection marker')
	recorded = {name: hashlib.sha256(data).hexdigest() for name, data in actual.items() if name != INTEGRATION_MARKER}
	if recorded != marker.get('files'):
		raise ValueError('All collection contains stale/edited/unexpected files')


def select_user_source(repo, source):
	source = Path(os.path.abspath(source))
	files = check_tree(source)
	actual = {p.relative_to(source).as_posix(): p.read_bytes() for p in files}
	source = source.resolve()
	if '.documentation-generated.json' in actual:
		raise ValueError('Old documentation ownership markers are unsupported; republish the collection')
	if INTEGRATION_MARKER not in actual:
		return source
	validate_collection_metadata(actual)
	bundle = user_projection(actual)
	scratch = repo.resolve() / '.tmp'
	reject_linked_ancestors(scratch, repo.resolve())
	scratch.mkdir(exist_ok=True)
	key = hashlib.sha256(actual[INTEGRATION_MARKER]).hexdigest()
	destination = scratch / ('docs-user-view-' + key)
	reject_linked_ancestors(destination, repo.resolve())
	if destination.exists():
		current = {p.relative_to(destination).as_posix(): p.read_bytes() for p in check_tree(destination)}
		if current != bundle:
			raise ValueError('Cached User documentation contains edits; preserve it before selecting again')
		return destination
	stage = Path(tempfile.mkdtemp(prefix='docs-user-select-', dir=scratch))
	try:
		for name, data in bundle.items():
			path = stage / name
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(data)
		check_tree(stage)
		try:
			os.replace(stage, destination)
		except OSError:
			# Parallel packagers may atomically publish the same verified view.
			if not destination.exists() or {p.relative_to(destination).as_posix(): p.read_bytes() for p in check_tree(destination)} != bundle:
				raise
	finally:
		if stage.exists():
			shutil.rmtree(stage)
	return destination


def replace_reference_tree(source, destination):
	"""Allow brief Windows scanner locks to clear before failing safely."""
	for delay in (0.1, 0.2, 0.4, None):
		try:
			os.replace(source, destination)
			return
		except PermissionError as error:
			if delay is None or getattr(error, 'winerror', None) not in {5, 32, 33}:
				raise
			time.sleep(delay)


def replace_reference_files(source, destination, scratch):
	"""Update a Windows-held standards reference with atomic files and a marker last.

    The caller keeps a complete backup. A partial tree cannot pass reference
    verification; unchanged files retain mtimes.
    """
	source_files = {path.relative_to(source): path for path in check_tree(source, validate_links=False)}
	existing = check_tree(destination, validate_links=False)
	for path in existing:
		if path.relative_to(destination) not in source_files:
			path.unlink()
	# Clear only empty directories, including old directory-to-file routes.
	for path in sorted(destination.rglob('*'), key=lambda p: len(p.parts), reverse=True):
		if path.is_dir() and not any(path.iterdir()):
			path.rmdir()
	order = sorted(source_files, key=lambda name: (
		name.as_posix() == '.standards.json', name.as_posix()))
	for relative in order:
		original, target = source_files[relative], destination / relative
		if target.is_file() and target.read_bytes() == original.read_bytes():
			continue
		target.parent.mkdir(parents=True, exist_ok=True)
		descriptor, temporary = tempfile.mkstemp(prefix='docs-file-', dir=scratch)
		os.close(descriptor)
		temporary = Path(temporary)
		try:
			shutil.copy2(original, temporary)
			replace_reference_tree(temporary, target)
		finally:
			temporary.unlink(missing_ok=True)


def tree_contents(root):
	"""Snapshot exact bytes, retaining the distinction between missing and empty."""
	if not root.exists():
		return None
	return {p.relative_to(root).as_posix(): p.read_bytes()
			for p in check_tree(root, validate_links=False)}


def valence_project_root(repo):
	"""Discover the consumer from its catalog identity, independent of its name."""
	candidates = []
	for project in repo.iterdir():
		catalog = project / 'Resources/docs/documentation.json'
		if not catalog.is_file():
			continue
		reject_linked_ancestors(catalog, repo)
		if json.loads(read_text(catalog)).get('product') == 'ValenceStudio':
			candidates.append(project)
	if len(candidates) > 1:
		raise ValueError(f'Ambiguous Valence standards reference destinations: {candidates}')
	return candidates[0] if candidates else None


def valence_reference_root(repo):
	project = valence_project_root(repo)
	return project / 'Resources/docs/standards' if project is not None else None


def sync_standards_reference(repo, files):
	"""Maintain an owned IDE reference tree without introducing another rule source."""
	destination = valence_reference_root(repo)
	if destination is None:
		return  # Standalone VeroScript trees have no Valence consumer.
	reject_linked_ancestors(destination, repo)
	marker_name = '.standards.json'
	marker = {'schemaVersion': 1, 'product': 'VSProjectCodingStandardsReference',
			  'source': module_route(repo, 'Resources/Docs/Standards'),
			  'files': {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}}
	expected = {**files, marker_name: (json.dumps(marker, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8')}
	actual = {p.relative_to(destination).as_posix(): p.read_bytes() for p in check_tree(destination, validate_links=False)} if destination.exists() else {}
	if actual == expected:
		return
	if actual:
		if marker_name not in actual:
			raise ValueError('IDE standards reference is not generator-owned; preserve its files before sync')
		recorded = json.loads(actual[marker_name])
		if recorded.get('product') != marker['product'] or recorded.get('schemaVersion') != 1 or recorded.get('source') != marker['source']:
			raise ValueError('Unrecognized IDE standards reference marker')
		if any(hashlib.sha256(data).hexdigest() != recorded['files'].get(name) for name, data in actual.items() if name != marker_name):
			raise ValueError('IDE standards reference contains local edits/unexpected files; preserve them before sync')
	scratch = repo / '.tmp'
	reject_linked_ancestors(scratch, repo)
	scratch.mkdir(exist_ok=True)
	work = Path(tempfile.mkdtemp(prefix='standards-reference-', dir=scratch))
	stage, backup = work / 'standards', work / 'previous'
	published = False
	try:
		for name, data in expected.items():
			path = stage / name
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(data)
		destination.parent.mkdir(parents=True, exist_ok=True)
		current = {p.relative_to(destination).as_posix(): p.read_bytes() for p in check_tree(destination, validate_links=False)} if destination.exists() else {}
		if current != actual:
			raise ValueError('IDE standards reference changed during synchronization; retry sync')
		if destination.exists():
			try:
				replace_reference_tree(destination, backup)
			except PermissionError as error:
				if getattr(error, 'winerror', None) not in {5, 32, 33}:
					raise
				if tree_contents(destination) != actual:
					raise ValueError('IDE standards reference changed during synchronization; retry sync')
				shutil.copytree(destination, backup, copy_function=shutil.copy2)
				if tree_contents(backup) != actual or tree_contents(destination) != actual:
					shutil.rmtree(backup)
					raise ValueError('IDE standards reference changed during synchronization; retry sync')
				try:
					replace_reference_files(stage, destination, work)
				except Exception as failure:
					try:
						replace_reference_files(backup, destination, work)
					except Exception as restore:
						raise RuntimeError(f'IDE standards rollback failed; recover reference from {backup}: {restore}') from failure
					shutil.rmtree(backup)
					raise
				published = True
				return
		try:
			if backup.exists() and tree_contents(backup) != actual:
				raise ValueError('IDE standards reference changed during synchronization; retry sync')
			replace_reference_tree(stage, destination)
			published = True
		except Exception as error:
			if backup.exists():
				try:
					replace_reference_tree(backup, destination)
				except OSError as restore:
					raise RuntimeError(f'IDE standards rollback failed; recover reference from {backup}: {restore}') from error
			raise
	finally:
		if not backup.exists() or published:
			shutil.rmtree(work)


def verify_standards_reference(repo):
	settings = load_settings(repo)
	if not settings.get('standardsRoot'):
		return
	authored, _ = localized_bundle(repo, settings['standardsRoot'], STANDARDS, settings['locales'], 'README.md')
	files = {name.removeprefix('Standards/'): data for name, data in authored.items()}
	root = valence_reference_root(repo)
	if root is None:
		return
	actual = {p.relative_to(root).as_posix(): p.read_bytes() for p in check_tree(root, validate_links=False)}
	marker = json.loads(actual.pop('.standards.json', b'{}'))
	if actual != files or marker.get('schemaVersion') != 1 or marker.get('product') != 'VSProjectCodingStandardsReference' or marker.get('source') != settings['standardsRoot'] or marker.get('files') != {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}:
		raise ValueError('Missing/stale/unexpected IDE standards reference; run sync-standards')


def documentation_output(repo, destination):
	# Direct exports follow the same boundary as component packagers: managed
	# releases must only change through the transactional business publisher.
	from veroscript_release import safe_output
	destination = safe_output(repo, destination, component=True)
	if destination.exists() and any(destination.iterdir()):
		raise ValueError(f'Documentation output must be empty: {destination}')
	return destination


def write_documentation(bundle, destination):
	for name, data in bundle.items():
		path = destination / name
		path.parent.mkdir(parents=True, exist_ok=True)
		path.write_bytes(data)
	check_tree(destination)
	return len(bundle)


def build_docs(repo, destination):
	"""Preview extraction from module originals; publishing uses publish_docs."""
	destination = documentation_output(repo, destination)
	errors = check_links(list(catalog(repo)[1]))
	if errors:
		raise ValueError('\n'.join(errors))
	bundle = user_bundle(repo)
	return write_documentation(bundle, destination)


def publish_docs(repo, destination, audience='All'):
	destination = documentation_output(repo, destination)
	return write_documentation(publication_bundle(repo, audience), destination)


def verify_publication(repo, destination, audience='All'):
	destination = Path(os.path.abspath(destination))
	expected = publication_bundle(repo, audience)
	actual = {path.relative_to(destination).as_posix(): path.read_bytes()
			  for path in check_tree(destination, validate_links=False)}
	if actual != expected:
		changed = sorted(name for name in set(actual) | set(expected) if actual.get(name) != expected.get(name))
		raise ValueError('Missing/stale/unexpected published documentation: ' + ', '.join(changed[:20]))
	check_tree(destination)


def verify_bundle(repo, destination):
	destination = Path(os.path.abspath(destination))
	expected = user_bundle(repo)
	files = check_tree(destination, validate_links=False)
	actual = {p.relative_to(destination).as_posix(): p.read_bytes() for p in files}
	if expected != actual:
		different = sorted(k for k in set(expected) | set(actual) if expected.get(k) != actual.get(k))
		raise ValueError('Missing/stale/unexpected User documentation: ' + ', '.join(different[:20]))
	check_tree(destination)


def archive_docs(source, output):
	"""Archive a validated offline tree, preserving linked empty directories."""
	source = Path(os.path.abspath(source))
	files = check_tree(source)
	output = Path(output)
	reject_linked_ancestors(output)
	if output.resolve().is_relative_to(source.resolve()):
		raise ValueError('Documentation archive cannot be written inside its source')
	with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
		for path in sorted(files):
			archive.write(path, path.relative_to(source.parent).as_posix())
		# ZIP readers create parent directories, but need explicit leaf entries
		# for empty directories that Markdown links may point to.
		for path in sorted(source.rglob('*')):
			if path.is_dir() and not any(path.iterdir()):
				archive.write(path, path.relative_to(source.parent).as_posix())
	return len(files)


def portable_mapping(repo):
	# A process may publish more than once after sources/manifests are edited.
	# A repository path alone cannot identify the current document mapping.
	mapping = {source: 'Resources/Docs/' + output for source, output in user_mapping(repo)[1].items()}
	return mapping


def portable_bytes(repo, source):
	"""Convert source asset links to the published Resources layout."""
	source = source.resolve()
	data = source.read_bytes()
	if source.suffix.lower() != '.md':
		return data
	# Integrated copies already have their final portable paths. Reapplying
	# source-asset conversion would turn contained document links into URLs.
	for parent in source.parents:
		marker = parent / INTEGRATION_MARKER
		if marker.is_file():
			metadata = json.loads(read_text(marker))
			if metadata.get('product') == 'VeroScriptDocumentationCollection':
				reject_linked_ancestors(source, parent)
				name = source.relative_to(parent).as_posix()
				if metadata.get('schemaVersion') != 1 or metadata.get('files', {}).get(name) != hashlib.sha256(data).hexdigest():
					raise ValueError(f'Edited/unrecognized publication document: {source}')
				return data
	user = portable_mapping(repo.resolve())
	roots = [(module_root(repo) / 'Resources/Assets', 'Resources/Assets'),
			 (module_root(repo) / 'VeroScript/Resources/Scripts', 'Resources/Scripts')]
	def location(path):
		# Shared assets/scripts also appear as documentation dependencies. Their
		# loose release copies use the physical Resources origin, not that copy.
		for root, prefix in roots:
			if path.is_relative_to(root.resolve()):
				return Path(prefix) / path.relative_to(root.resolve())
		if path in user:
			return Path(user[path])
		return None
	origin = location(source)
	settings = load_settings(repo)
	def convert(url):
		resolved = resolve_link(source, url)
		if resolved is None:
			return None
		target, query, fragment = resolved
		output = location(target)
		if origin is not None and output is not None:
			return relative_url(origin, output, query, fragment)
		# Source README files can also be shipped without a docs component.
		# Keep local example links, and direct source-only references online.
		if origin is None and target.exists() and target.is_relative_to(repo.resolve()):
			is_doc = 'Docs' in target.parts and 'Resources' in target.parts
			if target.is_relative_to(source.parent) and not is_doc:
				return None
			base = settings.get('repositoryBaseUrl')
			if base:
				kind = 'tree' if target.is_dir() else 'blob'
				return base.rstrip('/') + '/' + kind + '/' + quote(settings['repositoryRef'], safe='') + '/' + quote(target.relative_to(repo.resolve()).as_posix(), safe='/._-~') + ('?' + query if query else '') + ('#' + quote(fragment, safe='-_') if fragment else '')
		return None
	return rewrite_links(data.decode('utf-8'), convert).encode('utf-8')


def portable_tree(repo, source, destination):
	# The destination was staged by the caller. Touch only already copied Markdown.
	if (source / INTEGRATION_MARKER).is_file():
		actual = {p.relative_to(source).as_posix(): p.read_bytes() for p in check_tree(source)}
		validate_collection_metadata(actual)
		copied = {p.relative_to(destination).as_posix(): p.read_bytes()
				  for p in check_tree(destination, validate_links=False)}
		if copied != actual:
			raise ValueError('All collection contains changed/missing copied files; retry export')
		return  # Exported All documents already use their final portable links.
	from veroscript_release import portable_asset_bytes
	for target in destination.rglob('*.md'):
		original = source / target.relative_to(destination)
		if original.is_file() and not original.is_symlink() and not target.is_symlink():
			target.write_bytes(portable_asset_bytes(original))



def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('command', choices=('check', 'build', 'verify', 'publish', 'verify-publication', 'sync-standards', 'check-collection', 'select-user', 'portable', 'portable-file', 'asset-file', 'sdk', 'check-tree', 'archive', 'resources-index'))
	parser.add_argument('--repo', type=Path, default=repository_root())
	parser.add_argument('--output', type=Path)
	parser.add_argument('--source', type=Path)
	parser.add_argument('--audience', choices=('All', 'User'), help='For publish/verify-publication; default: All')
	parser.add_argument('--examples', action='store_true')
	parser.add_argument('--structure-only', action='store_true', help='For check-tree: reject linked or History entries before staging')
	args = parser.parse_args()
	args.repo = args.repo.resolve()
	if args.audience is not None and args.command not in {'publish', 'verify-publication'}:
		parser.error('--audience is only used by publish/verify-publication')
	if args.command in {'publish', 'verify-publication'}:
		if args.output is None or args.source is not None:
			parser.error('--output is required; publication assembles current author originals; do not pass --source')
		audience = args.audience or 'All'
		if args.command == 'publish':
			count = publish_docs(args.repo, args.output, audience)
			print(f'Integrated {audience} documentation published: {count} files -> {args.output}')
		else:
			verify_publication(args.repo, args.output, audience)
			print(f'Integrated {audience} publication verified: {args.output}')
	elif args.command in {'sync-standards', 'check-collection'}:
		if args.output is not None or args.source is not None:
			parser.error('sync-standards/check-collection read author originals; do not pass --source/--output')
		if args.command == 'sync-standards':
			print(f'Standards reference synchronized: {sync_standards(args.repo)} files')
		else:
			print(f'Temporary documentation collection checked: {check_collection(args.repo)} files')
	elif args.command == 'select-user':
		if args.source is None or args.output is not None:
			parser.error('select-user requires --source; its projection is cached under .tmp')
		print(select_user_source(args.repo, args.source))
	elif args.command == 'archive':
		if args.source is None or args.output is None:
			parser.error('--source and --output are required')
		count = archive_docs(args.source, args.output)
		print(f'Documentation archived: {count} files -> {args.output}')
	elif args.command == 'resources-index':
		from veroscript_release import release_asset_files, portable_asset_bytes
		inventory = {relative: hashlib.sha256(portable_asset_bytes(file)).hexdigest() for relative, file in release_asset_files(args.repo)}
		inventory.update({'Resources/Docs/' + relative: hashlib.sha256(data).hexdigest() for relative, data in publication_bundle(args.repo, 'User').items()})
		print(json.dumps(inventory, sort_keys=True))
	elif args.command == 'asset-file':
		if args.source is None:
			parser.error('--source is required')
		from veroscript_release import portable_asset_bytes
		print(base64.b64encode(portable_asset_bytes(args.source)).decode('ascii'))
	elif args.command in {'sdk', 'check-tree'}:
		if args.output is None:
			parser.error('--output is required')
		if args.command == 'sdk':
			from veroscript_release import sdk_asset_tree
			sdk_asset_tree(args.output, examples=args.examples)
		else:
			files = check_tree(args.output, validate_links=not args.structure_only)
			print(f'Offline documentation checked: {len(files)} files -> {args.output}')
	elif args.command == 'portable-file':
		if args.source is None:
			parser.error('--source is required')
		print(base64.b64encode(portable_bytes(args.repo, args.source)).decode('ascii'))
	elif args.command == 'portable':
		if args.output is None or args.source is None:
			parser.error('--source and --output are required')
		portable_tree(args.repo, args.source, args.output)
	elif args.command == 'check':
		settings, user, developer, manifests = catalog(args.repo)
		entrypoints = module_entrypoints(args.repo, settings, manifests)
		entrypoints.extend(documentation_entrypoints(args.repo, settings))
		tutorials = list((args.repo / settings.get('tutorialsRoot', module_route(args.repo, 'Resources/Docs/Tutorials'))).rglob('*.md'))
		if settings.get('standardsRoot'):
			localized_bundle(args.repo, settings['standardsRoot'], STANDARDS, settings['locales'], 'README.md')
			verify_standards_reference(args.repo)
		sources = list(user) + developer + entrypoints + tutorials
		errors = check_links(sources) + check_repository_links(sources, args.repo, settings)
		errors.extend(check_localized_prose(sources, settings['locales']))
		if settings.get('dateFreeDocumentation'):
			errors.extend(check_documentation_dates(sources))
		if errors:
			raise ValueError('\n'.join(errors))
		user_bundle(args.repo)
		print(f'Documentation checked: {len(manifests)} modules, {len(settings["locales"])} locales, {len(user)} User pages/assets, {len(developer)} Developer pages.')
	else:
		if args.output is None:
			parser.error('--output is required')
		if args.command == 'build':
			print(f'User documentation assembled: {build_docs(args.repo, args.output)} files -> {args.output}')
		else:
			verify_bundle(args.repo, args.output)
			print(f'User documentation verified: {args.output}')


if __name__ == '__main__':
	main()
